#!/usr/bin/env python3
"""
plot_two_camera_sync.py — analyze a two-camera flash-sync capture.

Input: two per-frame CSVs from `zwo_benchmark --frame-log` (columns
label,seq,server_ts_ns,client_epoch_s,mean_count), captured with both
cameras imaging the same blinking light (~1 Hz is fine).

Because both cameras see the *same* physical flashes, any difference in
when a flash appears in the two time series is a timestamp
misalignment. We measure it on two clocks:

  * client_epoch_s : the single client (NUC) wall clock recorded at
        frame receipt — common to both cameras, so a delay here is the
        arrival/transport skew between the two streams.
  * server_ts_ns   : each camera HOST's own clock (the value science
        would use). A delay here is the misalignment of the two host
        clocks (+ any fixed readout-offset difference) — the number
        that matters for cross-correlating the cameras.

No GPS/absolute time needed: the flash is the shared fiducial and only
relative timing is measured. Outputs a summary and a PNG.

Usage:
    python3 plot_two_camera_sync.py camA.csv camB.csv [-o out.png]
"""

import argparse
import csv
import sys
import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load(path):
    label, seq, sts, cts, flux = [], [], [], [], []
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh):
            label.append(row.get("label", ""))
            seq.append(int(row["seq"]))
            sts.append(int(row["server_ts_ns"]))
            cts.append(float(row["client_epoch_s"]))
            flux.append(float(row["mean_count"]))
    if not seq:
        sys.exit(f"{path}: no frames")
    d = dict(
        name=label[0] if label[0] else path,
        seq=np.array(seq),
        server_s=np.array(sts, dtype=np.float64) / 1e9,  # 0 if pre-1.0.5
        client_s=np.array(cts, dtype=np.float64),
        flux=np.array(flux),
    )
    d["has_server_ts"] = bool(np.any(d["server_s"] > 0))
    return d


def rising_edges(t, f):
    """Sub-sample rising-edge crossing times of flux f(t)."""
    lo, hi = np.percentile(f, 5), np.percentile(f, 95)
    if hi - lo < 1e-6 * max(abs(hi), 1.0):
        return np.array([]), (lo + hi) / 2.0  # no flash contrast
    thr = 0.5 * (lo + hi)
    below = f < thr
    idx = np.where(below[:-1] & ~below[1:])[0]  # i below, i+1 at/above
    out = []
    for i in idx:
        df = f[i + 1] - f[i]
        frac = (thr - f[i]) / df if df != 0 else 0.0
        out.append(t[i] + frac * (t[i + 1] - t[i]))
    return np.array(out), thr


def match_edges(ea, eb, max_dt):
    """Pair each A-edge to nearest B-edge within max_dt; return deltas B-A."""
    if ea.size == 0 or eb.size == 0:
        return np.array([]), np.array([])
    deltas, at = [], []
    for t in ea:
        j = np.argmin(np.abs(eb - t))
        if abs(eb[j] - t) <= max_dt:
            deltas.append(eb[j] - t)
            at.append(t)
    return np.array(at), np.array(deltas)


def xcorr_delay(ta, fa, tb, fb, dt=1e-3):
    """Cross-correlation delay (B lags A by +lag) on a uniform grid."""
    t0 = max(ta[0], tb[0])
    t1 = min(ta[-1], tb[-1])
    if t1 - t0 < 5 * dt:
        return None
    grid = np.arange(t0, t1, dt)
    a = np.interp(grid, ta, fa)
    b = np.interp(grid, tb, fb)
    a = a - a.mean()
    b = b - b.mean()
    if a.std() < 1e-9 or b.std() < 1e-9:
        return None
    corr = np.correlate(b, a, mode="full")
    lags = np.arange(-len(a) + 1, len(a))
    k = int(np.argmax(corr))
    # parabolic sub-sample refinement around the peak
    if 0 < k < len(corr) - 1:
        y0, y1, y2 = corr[k - 1], corr[k], corr[k + 1]
        denom = y0 - 2 * y1 + y2
        shift = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
    else:
        shift = 0.0
    return (lags[k] + shift) * dt


def summarize_delay(name, at, deltas):
    if deltas.size == 0:
        print(f"  {name}: no matched flashes")
        return None
    med = np.median(deltas)
    mad = np.median(np.abs(deltas - med))
    print(f"  {name}: {deltas.size} flashes  "
          f"median={med * 1e3:+.3f} ms  "
          f"std={deltas.std() * 1e3:.3f} ms  "
          f"MAD={mad * 1e3:.3f} ms  "
          f"range=[{deltas.min() * 1e3:+.2f},{deltas.max() * 1e3:+.2f}] ms")
    return med


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_a")
    ap.add_argument("csv_b")
    ap.add_argument("-o", "--out", default="two_camera_sync.png")
    ap.add_argument("--zoom", type=float, default=5.0,
                    help="seconds of flux to show in the zoom panel (default 5)")
    args = ap.parse_args()

    A, B = load(args.csv_a), load(args.csv_b)
    # common origin for readable time axes (client clock is shared)
    t0 = min(A["client_s"][0], B["client_s"][0])
    for d in (A, B):
        d["ct"] = d["client_s"] - t0
        d["st"] = d["server_s"] - (t0 if d["has_server_ts"] else 0)

    print(f"camera A = {A['name']}  ({A['seq'].size} frames)")
    print(f"camera B = {B['name']}  ({B['seq'].size} frames)")
    for tag, d in (("A", A), ("B", B)):
        dt = np.diff(d["ct"])
        fps = 1.0 / np.median(dt) if dt.size else float("nan")
        # per-camera latency: client arrival minus server stamp
        print(f"  {tag}: ~{fps:.1f} fps client-side"
              + ("" if not d["has_server_ts"] else
                 f"; client-server offset "
                 f"median={np.median(d['ct'] - d['st']) * 1e3:.2f} ms "
                 f"(transport+clock, jitter "
                 f"{(d['ct'] - d['st']).std() * 1e3:.3f} ms)"))

    # flash period from camera A edges (client clock)
    ea_c, thrA = rising_edges(A["ct"], A["flux"])
    eb_c, thrB = rising_edges(B["ct"], B["flux"])
    if ea_c.size < 2:
        print("\nWARNING: no clear flash detected in camera A — check the "
              "light is blinking and both cameras see it (flux contrast low).")
    period = np.median(np.diff(ea_c)) if ea_c.size > 1 else 1.0
    max_dt = 0.4 * period  # match window: fraction of a flash period

    print(f"\nflash period ~ {period:.3f} s")
    print("inter-camera delay (B - A), by flash edge matching:")
    at_c, dc = match_edges(ea_c, eb_c, max_dt)
    med_client = summarize_delay("client clock (arrival skew)", at_c, dc)

    med_server = None
    at_s, ds = np.array([]), np.array([])
    if A["has_server_ts"] and B["has_server_ts"]:
        ea_s, _ = rising_edges(A["st"], A["flux"])
        eb_s, _ = rising_edges(B["st"], B["flux"])
        at_s, ds = match_edges(ea_s, eb_s, max_dt)
        med_server = summarize_delay(
            "server clocks (host-clock misalignment)", at_s, ds)
    else:
        print("  server timestamps absent (server < v1.0.5) — skipping")

    xc = xcorr_delay(A["ct"], A["flux"], B["ct"], B["flux"])
    if xc is not None:
        print(f"cross-correlation check (client clock): B - A = "
              f"{xc * 1e3:+.3f} ms")

    # ---- plots ----
    fig, ax = plt.subplots(3, 1, figsize=(11, 10))

    # (1) flux zoom — see the flashes line up
    zt = A["ct"][0]
    for d, c in ((A, "tab:blue"), (B, "tab:orange")):
        m = d["ct"] <= zt + args.zoom
        ax[0].plot(d["ct"][m], d["flux"][m], ".-", ms=3, lw=0.8,
                   color=c, label=d["name"])
    ax[0].set(xlabel="client time [s]", ylabel="mean ROI count",
              title=f"Flash flux (first {args.zoom:g}s) — both cameras, "
                    f"shared client clock")
    ax[0].legend(loc="upper right", fontsize=8)
    ax[0].grid(alpha=0.3)

    # (2) per-flash delay vs time — drift / jitter
    if dc.size:
        ax[1].axhline(0, color="k", lw=0.6)
        ax[1].plot(at_c, dc * 1e3, "o-", ms=4, color="tab:blue",
                   label="client clock")
        if med_server is not None and at_s.size:
            ax[1].plot(at_s, ds * 1e3, "s-", ms=4, color="tab:red",
                       label="server clocks")
        ax[1].set(xlabel="time [s]", ylabel="B − A delay [ms]",
                  title="Per-flash inter-camera delay")
        ax[1].legend(loc="best", fontsize=8)
        ax[1].grid(alpha=0.3)
    else:
        ax[1].text(0.5, 0.5, "no matched flashes", ha="center",
                   transform=ax[1].transAxes)

    # (3) histogram of per-flash delays
    if dc.size:
        ax[2].hist(dc * 1e3, bins=max(8, dc.size // 3),
                   color="tab:blue", alpha=0.7, label="client clock")
        if med_client is not None:
            ax[2].axvline(med_client * 1e3, color="tab:blue", ls="--",
                          label=f"median {med_client * 1e3:+.2f} ms")
        if med_server is not None and ds.size:
            ax[2].hist(ds * 1e3, bins=max(8, ds.size // 3),
                       color="tab:red", alpha=0.5, label="server clocks")
            ax[2].axvline(med_server * 1e3, color="tab:red", ls="--",
                          label=f"server median {med_server * 1e3:+.2f} ms")
        ax[2].set(xlabel="B − A delay [ms]", ylabel="flashes",
                  title="Delay distribution (spread = alignment jitter)")
        ax[2].legend(fontsize=8)
        ax[2].grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig(args.out, dpi=130)
    print(f"\nwrote plot to {args.out}")


if __name__ == "__main__":
    main()
