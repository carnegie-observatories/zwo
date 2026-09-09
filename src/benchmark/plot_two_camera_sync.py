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
relative timing is measured. Reports a per-frame stall/glitch census
and a delay drift-rate fit (both matter for long runs). Outputs a
summary and a 4-panel PNG.

Usage:
    python3 plot_two_camera_sync.py camA.csv camB.csv [-o out.png]
        [--guard SEC] [--reject-outliers] [--stall-thresh MS]
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


def xcorr_delay(ta, fa, tb, fb, dt=1e-3, max_win=120.0, max_lag=0.5):
    """Cross-correlation delay (B lags A by +lag), bounded lag search.

    Only a short window (max_win s) and a bounded lag range (±max_lag s)
    are used — the delay is constant, so a full O(n²) correlate over a
    long capture is both unnecessary and prohibitively slow."""
    t0 = max(ta[0], tb[0])
    t1 = min(min(ta[-1], tb[-1]), t0 + max_win)
    if t1 - t0 < 5 * dt:
        return None
    grid = np.arange(t0, t1, dt)
    a = np.interp(grid, ta, fa)
    b = np.interp(grid, tb, fb)
    a = a - a.mean()
    b = b - b.mean()
    if a.std() < 1e-9 or b.std() < 1e-9:
        return None
    L = int(max_lag / dt)
    lags = np.arange(-L, L + 1)
    n = len(a)
    corr = np.empty(lags.size)
    for m, k in enumerate(lags):          # sum a[i]*b[i+k], bounded k
        if k >= 0:
            corr[m] = np.dot(a[:n - k], b[k:])
        else:
            corr[m] = np.dot(a[-k:], b[:n + k])
    j = int(np.argmax(corr))
    k = lags[j]
    # parabolic sub-sample refinement around the peak
    if 0 < j < len(corr) - 1:
        y0, y1, y2 = corr[j - 1], corr[j], corr[j + 1]
        denom = y0 - 2 * y1 + y2
        shift = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
    else:
        shift = 0.0
    return (k + shift) * dt


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


def stall_census(name, t, thresh_ms):
    """Per-frame inter-arrival gaps; report frames delayed beyond thresh."""
    dt = np.diff(t) * 1e3  # ms
    if dt.size == 0:
        return
    med = np.median(dt)
    thr = thresh_ms if thresh_ms else max(2.5 * med, med + 15.0)
    big = np.where(dt > thr)[0]
    dur = (t[-1] - t[0]) / 60.0  # minutes
    rate = len(big) / dur if dur > 0 else 0.0
    print(f"  {name}: {t.size} frames, median dt={med:.2f} ms, "
          f"max={dt.max():.1f} ms; stalls>{thr:.0f}ms: {len(big)} "
          f"({rate:.2f}/min)")
    for i in big[np.argsort(-dt[big])][:8]:
        print(f"      t={t[i] - t[0]:7.2f}s  dt={dt[i]:.1f} ms")
    return t[1:][big] - t[0], dt[big]  # times, sizes


def drift_fit(at, d):
    """Linear fit delay-vs-time; return (slope_us_per_min, intercept_ms)."""
    if at.size < 3:
        return None, None
    p = np.polyfit(at, d, 1)          # d in seconds, at in seconds
    slope_us_per_min = p[0] * 1e6 * 60.0
    return slope_us_per_min, p[1] * 1e3


def apply_guard(at, d, t_lo, t_hi, guard):
    """Drop matched flashes within `guard` seconds of the run edges."""
    if guard <= 0:
        return at, d, 0
    keep = (at >= t_lo + guard) & (at <= t_hi - guard)
    return at[keep], d[keep], int((~keep).sum())


def flag_outliers(d, k=5.0):
    """Boolean mask of |d-median| > k*sigma_MAD."""
    if d.size == 0:
        return np.zeros(0, bool)
    med = np.median(d)
    mad = np.median(np.abs(d - med)) * 1.4826
    if mad == 0:
        return np.zeros(d.size, bool)
    return np.abs(d - med) > k * mad


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv_a")
    ap.add_argument("csv_b")
    ap.add_argument("-o", "--out", default="two_camera_sync.png")
    ap.add_argument("--zoom", type=float, default=5.0,
                    help="seconds of flux to show in the zoom panel (default 5)")
    ap.add_argument("--guard", type=float, default=0.0,
                    help="drop matched flashes within N seconds of the run "
                         "start/end (default 0 = keep all)")
    ap.add_argument("--reject-outliers", action="store_true",
                    help="exclude >5-sigma_MAD flashes from summary stats")
    ap.add_argument("--stall-thresh", type=float, default=0.0,
                    help="frame-gap [ms] counted as a stall (default auto: "
                         "max(2.5x median, median+15ms))")
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

    # per-frame stall census (direct glitch detection over the whole run)
    print("\nper-frame timing (stall / glitch census):")
    stallA = stall_census("A", A["st"] if A["has_server_ts"] else A["ct"],
                          args.stall_thresh)
    stallB = stall_census("B", B["st"] if B["has_server_ts"] else B["ct"],
                          args.stall_thresh)

    t_lo = max(A["ct"][0], B["ct"][0])
    t_hi = min(A["ct"][-1], B["ct"][-1])

    print(f"\nflash period ~ {period:.3f} s")
    if args.guard > 0:
        print(f"guard: dropping flashes within {args.guard:g}s of run edges")
    print("inter-camera delay (B - A), by flash edge matching:")
    at_c, dc = match_edges(ea_c, eb_c, max_dt)
    at_c, dc, ng = apply_guard(at_c, dc, t_lo, t_hi, args.guard)
    out_c = flag_outliers(dc)
    if out_c.any():
        print(f"  outliers (>5 sigma_MAD): {out_c.sum()} at "
              + ", ".join(f"{t:.1f}s" for t in at_c[out_c]))
    stat_c = at_c[~out_c] if args.reject_outliers else at_c
    dstat_c = dc[~out_c] if args.reject_outliers else dc
    med_client = summarize_delay("client clock (arrival skew)", stat_c, dstat_c)
    sl, _ = drift_fit(at_c, dc)
    if sl is not None:
        print(f"    drift: {sl:+.2f} µs/min over "
              f"{(t_hi - t_lo) / 60:.1f} min")

    med_server = None
    at_s, ds = np.array([]), np.array([])
    if A["has_server_ts"] and B["has_server_ts"]:
        ea_s, _ = rising_edges(A["st"], A["flux"])
        eb_s, _ = rising_edges(B["st"], B["flux"])
        at_s, ds = match_edges(ea_s, eb_s, max_dt)
        at_s, ds, _ = apply_guard(at_s, ds, t_lo, t_hi, args.guard)
        out_s = flag_outliers(ds)
        stat_s = at_s[~out_s] if args.reject_outliers else at_s
        dstat_s = ds[~out_s] if args.reject_outliers else ds
        med_server = summarize_delay(
            "server clocks (host-clock misalignment)", stat_s, dstat_s)
        sl_s, _ = drift_fit(at_s, ds)
        if sl_s is not None:
            print(f"    drift: {sl_s:+.2f} µs/min over "
                  f"{(t_hi - t_lo) / 60:.1f} min")
    else:
        print("  server timestamps absent (server < v1.0.5) — skipping")

    xc = xcorr_delay(A["ct"], A["flux"], B["ct"], B["flux"])
    if xc is not None:
        print(f"cross-correlation check (client clock): B - A = "
              f"{xc * 1e3:+.3f} ms")

    # ---- plots ----
    fig, ax = plt.subplots(4, 1, figsize=(11, 13))

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

    # (2) per-flash delay vs time — drift / jitter (with linear drift fit)
    if dc.size:
        ax[1].axhline(0, color="k", lw=0.6)
        ax[1].plot(at_c, dc * 1e3, "o-", ms=3, lw=0.6, color="tab:blue",
                   alpha=0.8, label="client clock")
        if med_server is not None and at_s.size:
            ax[1].plot(at_s, ds * 1e3, "s-", ms=3, lw=0.6, color="tab:red",
                       alpha=0.8, label="server clocks")
            sl_s, ic_s = drift_fit(at_s, ds)
            if sl_s is not None:
                ax[1].plot(at_s, (np.polyval([sl_s / 6e7, ic_s / 1e3], at_s))
                           * 1e3, "-", color="darkred", lw=1.5,
                           label=f"server drift {sl_s:+.1f} µs/min")
        if out_c.any():
            ax[1].plot(at_c[out_c], dc[out_c] * 1e3, "x", color="k",
                       ms=9, label="outlier (>5σ)")
        ax[1].set(xlabel="time [s]", ylabel="B − A delay [ms]",
                  title="Per-flash inter-camera delay (drift + outliers)")
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

    # (4) per-frame inter-arrival gaps over the whole run — glitch/stall census
    for d, c, tag in ((A, "tab:blue", "A"), (B, "tab:orange", "B")):
        tt = d["st"] if d["has_server_ts"] else d["ct"]
        ax[3].plot(tt[1:] - tt[0], np.diff(tt) * 1e3, ".", ms=1.5,
                   color=c, alpha=0.5, label=f"cam {tag}")
    ax[3].set(xlabel="time [s]", ylabel="frame gap Δt [ms]",
              title="Per-frame delivery interval (spikes = stalls)",
              yscale="log")
    ax[3].legend(loc="upper right", fontsize=8, markerscale=4)
    ax[3].grid(alpha=0.3, which="both")

    fig.tight_layout()
    fig.savefig(args.out, dpi=130)
    print(f"\nwrote plot to {args.out}")


if __name__ == "__main__":
    main()
