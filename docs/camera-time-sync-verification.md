# Verifying frame-timestamp synchronization across ZWO guiders

**Date:** 2026-07-09
**Purpose:** Validate that independently-hosted ASI294MM Pro guiders
(one Raspberry Pi each) have frame timestamps aligned to ≲1 ms, so
their small-ROI 193 Hz streams can be cross-correlated for fast
tip-tilt / ground-layer seeing. Written for the current two-guider
setup; the method scales to N cameras (e.g. a third on Clay's AUX2).
Companion to
[ASI294MM-P_200Hz_ROI_report.md](ASI294MM-P_200Hz_ROI_report.md).

## Why host timestamps alone are not enough

Our server stamps each frame with `CLOCK_REALTIME` at USB delivery
(zwoserver ≥ 1.0.5). That gives excellent *precision* — 59 µs rms
frame-interval jitter, measured — but says nothing about two things
that a cross-camera measurement depends on:

1. **Absolute offset.** The stamp is taken *after* readout + USB
   transfer, an unknown, mode-dependent delay from the true photon
   epoch. The AO-camera literature is explicit that this latency is
   **not a fixed constant** — it varies with readout mode, ROI, and
   trigger [Kulcsár et al. 2018 SPIE 10703]. So the offset can differ
   between two cameras even if both are configured "the same."
2. **Host-clock alignment.** The two Pis' clocks must agree.
   NTP-disciplined PC clocks have been measured wrong by **tens to
   hundreds of ms** in real astronomy software (802 ms, 79 ± 17 ms,
   up to 1 s) [Barry et al. 2015 PASA 32 e014]. In our lab the two
   camera Pis are NTP clients of the zwo-nuc client host — so they
   share a time source and are *mutually* disciplined (their absolute
   time follows zwo-nuc, which is itself free-running, but their
   relative alignment is set by NTP-over-LAN residual, ~ms, not by
   independent drift). That residual is still an unknown at the ms
   level until checked.

The established fix in every relevant community — occultation timing
(IOTA), high-speed photometry, adaptive optics — is an **independent
optical check with a GPS-referenced light source**, never trusting the
host clock at the ms level without it [Barry 2015; Dhillon et al. 2021
MNRAS, HiPERCAM; Layden, Burdge et al. 2026 PASP, proto-Lightspeed].

## Precedent (what the literature achieves)

| System | Method | Accuracy | Source |
|---|---|---|---|
| proto-Lightspeed (**Magellan Clay**, our site) | GPS-PPS hardware trigger + TTL end-of-readout time-tag; validated on-sky vs Crab pulsar | **≤30 µs absolute** | Layden/Burdge 2026 PASP (arXiv:2601.16268) |
| HiPERCAM | GPS PPS-driven LED on focal-plane mask, phase-folded on 1 s | **~100 µs** | Dhillon 2021 (arXiv:2107.10124) |
| proto-Lightspeed lab | GPS-PPS-pulsed LED, mid-exposure recovery | **<50 µs** (LED-rise-limited) | arXiv:2601.16268 |
| SEXTA (occultation) | 500-LED sweep, 1 lit/2 ms from UTC second | **2 ms** frames; device <0.2 ms to UTC | Barry 2015 (arXiv:1503.05705) |

Key architectural lesson from multi-camera AO (CANARY on-sky, DRAGON
lab): align **exposure mid-points**, not data-arrival times — two
cameras with different readout times expose at different epochs even
with identical timestamps; both systems switched from aligning
pixel-arrival to a shared trigger for this reason [Basden et al.,
arXiv:1603.07527].

**Caveat on transfer:** those sub-100 µs numbers are what *dedicated
GPS-trigger hardware* achieves. Our server-side-over-USB approach will
be worse; how much worse for the ASI294MM Pro at 193 Hz is
uncharacterized in the literature and is exactly what this test
measures.

**Hardware triggering is not available on these cameras.** The
ASI294MM Pro guiders have **no external hardware sync-trigger input**
(no trigger/PPS pin, no TTL exposure I/O) — so the hardware-trigger
methods above (proto-Lightspeed's GPS-PPS trigger + TTL
end-of-readout, an external common trigger à la CANARY/DRAGON) are
**ruled out**. Frames are free-running and stamped in software at USB
delivery; the only synchronization check we can do is the **optical
flash** method. This is the fundamental reason we time-stamp
server-side and validate optically rather than triggering the sensors.

## Recommended test — shared GPS-PPS LED, phase-folded

A single GPS-PPS-driven LED placed so **both cameras image it
simultaneously**. This is the cheapest recipe from the literature and
uniquely fits our need: because both cameras see the *same physical
flash*, it measures their **relative** alignment directly — which is
what cross-correlation needs (relative, not absolute UTC) — and it
validates the host-clock discipline (NTP vs PTP) end-to-end.

### Hardware

**Any cheap GPS module with a PPS pin is sufficient.** The PPS edge of
even a bargain u-blox receiver is accurate to tens of ns against UTC —
~10⁵× below our 1 ms goal — so the GPS is never the limiting factor;
LED rise time and the camera are. Do **not** pay for a timing-grade
receiver for *this* test (that class matters only for host NTP/PTP
discipline, a separate purchase — see below). Cost-effective options
with a broken-out PPS pin (approx. prices, verify at retailer):

| module | chipset | PPS | ~price | notes |
|---|---|---|---|---|
| **GT-U7** | u-blox NEO-6M | yes | ~$10 | what the Stamp-of-Approval flasher uses; proven for this exact job |
| **VK2828U7G5LF** | u-blox G7020 | yes | ~$12 | tiny, ceramic antenna, UART+PPS |
| BN-220 / BN-880 | u-blox M8 | yes | ~$15–20 | ubiquitous drone module, PPS on a pad |
| ATGM336H | AT6668 (non-ublox) | yes | ~$6 | multi-GNSS, cheapest; PPS output |

- **LED + driver** switched by the PPS edge. A single logic gate /
  MOSFET is enough; keep LED rise time « target (a plain LED is
  <1 µs, far below our 1 ms goal). Optionally stretch the pulse to a
  few ms so it lands cleanly inside one 5 ms frame.
- LED positioned in both cameras' fields (diffuser / shared fiber /
  simply both looking at the same lab wall spot). No optical precision
  needed — only that both see the same on/off transition.

**Strongly consider not building this from scratch:** the occultation
community's **Stamp of Approval** (ChasinSpin, MIT-licensed open
hardware) is *exactly* this device — GT-U7 GPS + constant-current LED
driver, hardware-gated so the GPS→LED delay is ~53 ns, purpose-built
to test camera frame-timestamp accuracy (validated to ≤0.1 ms at
30 fps). Either buy/build it as-is, or lift its LED-driver + PPS-gating
schematic. https://github.com/ChasinSpin/StampOfApproval

*Separate concern — host clock discipline:* for continuously
disciplining the two Pi clocks (NTP/PTP, the ansible task), a
timing-grade receiver like the **u-blox NEO-M8T GNSS Timing HAT**
(~$50) is the right class — it has the single-satellite timing mode
the navigation modules above lack. That is not needed for the flash
test but is the module to standardize on for the sync rollout.

### Capture

Use the existing benchmark's timestamp path — no new capture code:

```
# on each Pi host, simultaneously, small ROI around the LED:
zwo_benchmark --host <cam> --bins 2 --bits 8 --rois 5 \
    --exptimes 0.005 --duration 120 -v 2> cam<N>_dt.log
```

`--verbose` already logs per-frame `ts=<ns>` (the server
CLOCK_REALTIME stamp) plus the ROI pixels are in the stream. For this
test add a tiny capture that records, per frame, `(ts_ns, mean_ROI_flux)`
— the LED shows up as a flux step. *(If preferred I can add a
`--flux-log` option that appends mean-ROI intensity to the verbose
line; ~10 lines.)*

### Analysis

1. In each camera's series, find the frame where LED flux crosses
   its half-max — that frame's `ts_ns` is the camera's measured "LED
   lit" time.
2. **Relative alignment** = difference between the two cameras'
   crossing timestamps, per PPS second. Fold over many seconds (120 s
   → ~120 flashes) → mean gives the fixed inter-camera offset,
   scatter gives the alignment jitter. Target: |mean| and σ both
   ≲ 1 ms.
3. **Absolute check** (optional, needs the GPS UTC): each camera's
   crossing `ts_ns mod 1 s` vs 0 → the host-clock-to-UTC error,
   directly exposing an NTP offset like the ones the papers document.
4. Sub-frame interpolation: with a ~ms LED pulse and 5 ms frames,
   interpolate the flux rise across 1–2 frames to beat the 5 ms
   sampling — this is the phase-folding step HiPERCAM/proto-Lightspeed
   use to reach µs from coarse frames.

### Pass criteria

- Inter-camera offset stable and < 1 ms after whatever clock
  discipline is deployed (NTP now; PTP later — see below).
- Offset **repeatable** across ROI/exposure changes, or if not,
  characterized as a lookup (the mode-dependent-latency pitfall).

## Measured results — best configuration (2026-07-16)

Two matched **ASI294MM Pro** guiders (serials `2a354b041d010900` on
zwoserver01/10.8.80.225 and `3924d3032a010900` on
zwoserver02/10.8.80.218), both running server v1.0.6 with the tuning
below, captured simultaneously from zwo-nuc against a plain ~2.9 Hz
LED flasher (no GPS) — `two_camera_capture.sh -m -P`, analyzed with
`plot_two_camera_sync.py`. Two operating points, each a 30-min run:

| | **~100 fps** (10 ms, 10% ROI) | **~200 fps** (5 ms, 5% ROI) |
|---|---|---|
| cross-camera alignment (server median) | **−0.05 ms** | **+1.0 ms**¹ |
| per-flash spread (MAD) | 1.05 ms | 3.1 ms¹ |
| drift over 30 min | −35 µs/min | −5 µs/min |
| stalls | **0** | ~16/min, 30 ms, all SDK, **0 client** |
| per-camera frame jitter | 0.37 ms | 0.44 ms |

¹ the 200 fps median/MAD are inflated by the SDK stalls contaminating
the flash-edge measurement (see below), not a real clock difference.

![Two ASI294MM Pro guiders on a common bracket, both imaging a blinking
headlamp LED.](images/two-camera-sync-setup.jpg)

**Figure 1.** Lab setup: two matched ASI294MM Pro guiders on a common
bracket, both pointed at the same blinking headlamp LED (foreground).
No GPS — the shared flash is the fiducial; only relative timing is
measured.

![100 fps 30-min: delay flat around zero, zero stalls in the per-frame
panel.](images/sync-100fps.png)

**Figure 2.** Best config at **~100 fps**, 30 min. Top→bottom: flash
flux (first 5 s), per-flash inter-camera delay vs time (server-clock
red, client-arrival blue; drift −35 µs/min), delay histogram (median
−0.05 ms), and per-frame delivery interval (flat at 10.18 ms — zero
stalls).

![200 fps 30-min: same alignment but a dense ~30 ms SDK-stall band and
stall-contaminated delay outliers.](images/sync-200fps.png)

**Figure 3.** Best config at **~200 fps**, 30 min, same panels. The
per-frame panel shows the ~30 ms SDK-lost-wakeup stall band (~16/min);
those late frames contaminate the delay panels (MAD 3.1 ms), while the
underlying alignment is unchanged from Figure 2.

### What the runs show

- **Alignment: sub-100 µs at 100 fps** (Figure 2). The two cameras'
  server timestamps cross-align to −0.05 ms (median SE ~15 µs) — the
  two Pis are NTP clients of the common zwo-nuc host, so this is
  NTP-over-LAN residual, and it is well inside the ≲1 ms goal.
- **No drift.** −35 / −5 µs/min over 30 min at 100/200 fps: the
  NTP-client-of-a-common-host discipline holds the cameras aligned with
  no walk or sawtooth for at least half an hour.
- **The ~1 ms MAD is a measurement beat, not the clocks.** The
  per-flash delay is bimodal — a beat between the ~2.9 Hz flasher and
  the frame sampling; the *median* is the true alignment. A short
  (1-min) window catches one beat phase and looks misleadingly tight
  (e.g. −0.5 ms), while 30 min averages the full beat. Reduce it with a
  sharper (GPS-PPS) LED, not more frames — see next point.
- **200 fps is limited by SDK stalls, not sync** (Figure 3). At 5 ms
  the SDK lost-wakeup (below) fires ~16/min; those ~30 ms late frames
  contaminate the flash-edge method (MAD 3.1 ms, apparent +1 ms median
  = A stalls slightly more than B). The underlying alignment is the
  same sub-ms as at 100 fps; the *measurement* just degrades. For the
  cleanest sync check, run at ~100 fps.

### Client/server tuning that got here (condensed)

Getting to the numbers above required removing two stall mechanisms,
separated by whether the frame's sequence number jumps (dropped) or
stays contiguous (delivered late):

- **Client-pull drops** (seq-step > 1, ~30 ms, rare) — the client
  missed its pull deadline and the server's 2-frame double buffer was
  overwritten. Cause was per-frame frame-log I/O to NVMe on zwo-nuc;
  **staging the log in tmpfs (`-m`) drove these to zero**, and pinning
  each client to its own core + best-effort real-time priority (`-P`)
  removes scheduler jitter. Bandwidth is not involved — two cameras use
  only ~85 Mbps of the gigabit link, and these are client-side anyway.
- **SDK lost-wakeups** (seq-step 1, frame late-not-lost) — the SDK's
  `CirBuf::ReadBuff` sleeps the full `ASIGetVideoData` timeout even
  though the frame is ready; the stall length tracks that timeout.
  `ASI_BANDWIDTHOVERLOAD` and `ASI_HIGH_SPEED_MODE` had no effect on the
  rate, but **lowering the server's timeout floor 50 → 15 ms halved the
  stall duration (65 → 30 ms)** with no spurious timeouts. The residual
  ~30 ms × ~16/min at 200 fps is inherent to the closed SDK — but the
  frames are **late, not lost, and correctly timestamped**, so
  PSD/cross-correlation science tolerates them (non-uniform sampling
  only); a fixed-cadence 200 fps guiding loop would see a 30 ms gap
  ~every 4 s, so run ~100 fps (stalls rare/absent) or mask.

Earlier baseline note: the first run used two *different* camera models
(294MM Pro + 1600MM Pro) and read +1.75 ms — that offset was the fixed
readout-timing difference between sensors, which vanished with the
matched pair, confirming it was not a clock effect.

## Open questions this test answers (added to the report)

1. Relative timestamp accuracy of the ASI294MM Pro server-timestamped
   at USB delivery vs a hardware trigger — the number no published
   source provides for this chip.
2. Rolling-shutter row-dependent exposure offset across the small ROI
   at 5 ms: fixed per-ROI constant, or a row-by-row effect that
   matters for correlation?
3. Does the chosen clock discipline actually deliver sub-ms host
   alignment in the field? The shared PPS-LED flash is the independent
   validator. Preferred discipline is per-host GPS, not PTP — see the
   clock-architecture section (Pi 4B has no hardware PTP timestamping).
4. Is relative alignment sufficient for the science, or is absolute
   UTC also needed? The shared LED gives relative directly; absolute
   needs the GPS UTC reference.

## Host clock architecture — three viable routes

The clock that matters is each host's `CLOCK_REALTIME` — the one
zwoserver reads to stamp frames. It must be (a) disciplined to a stable
reference and (b) mutually aligned across the guider hosts. Three
routes, **all of which clear the ≲1 ms science requirement by a wide
margin** — so choose on operational grounds (cabling, uniformity,
robustness, scaling to the AUX2 camera and beyond), not on raw
accuracy.

| route | per-host accuracy | GPS units | needs | robustness | best when |
|---|---|---|---|---|---|
| **A. Per-host GPS** | ~1 µs, any Pi model | one **per host** | M8T HAT + gpsd + chrony each | no shared master, no net dependency | few cameras; want independence |
| **B. Pi 5 grandmaster + SW-PTP clients** | ~tens of µs (client-gated) | one (master) | Pi 5 master, `ptp4l` net, Pi 4 clients OK | single master; net-dependent | reusing existing Pi 4 guiders |
| **C. CM4 (PoE) hardware-PTP** | sub-µs | one (grandmaster) | CM4+carrier per host, PoE net | single master (mitigable) | standardizing observatory timing |

### A. Per-host GPS (model-agnostic, most independent)

One u-blox NEO-M8T GNSS Timing HAT per host (~$50): PPS on a GPIO via
`dtoverlay=pps-gpio`, NMEA as the coarse anchor, chrony disciplines
`CLOCK_REALTIME` to GPS/UTC at ~1 µs. Independently GPS-locked hosts
are mutually aligned to ~µs **with no PTP or NTP between them** — no
grandmaster to fail, no reliance on a quiet network. The same HAT's PPS
also drives the flash-test LED. Cost is one GPS per host.

### B. Pi 5 grandmaster + software-PTP clients

A Pi 5 *can* be a PTP master (it has hardware timestamping), but
**PTP accuracy is gated by the worse end of each link**: the client
stamps two of the four PTP timestamps itself, so a Pi 4 client doing
*software* timestamping caps the link at ~tens of µs regardless of the
master. That still clears 1 ms comfortably, so this is the cheap way to
reuse existing Pi 4 guiders — but it is neither the most accurate nor
the most robust (single master, network-dependent). Note the Pi 5, for
all its hardware timestamping, brings PPS in only over **GPIO**
(disciplining the system clock), not into the NIC's hardware clock.

### C. CM4 on a PoE carrier — hardware PTP (recommended if refreshing)

Best-suited hardware of the three. The **CM4 (and CM5) uniquely have a
dedicated hardware PPS input wired to the NIC's PTP hardware clock**
(the `SYNC_IN` pin) — feed GPS-PPS straight into the PHC and discipline
it in hardware via `ts2phc`, so the clock the PTP packets are stamped
against is itself GPS-locked at the silicon level. Even the Pi 5 lacks
this. Toolchain: `ts2phc` (GPS-PPS → PHC) → `ptp4l` (distribute) →
`phc2sys` (PHC → system clock, so `CLOCK_REALTIME` follows)
[jclark rpi-cm4-ptp-guide].

Architecture: one CM4+GPS as hardware grandmaster; each guider a CM4 on
a **PoE** carrier as a hardware-PTP client → sub-µs everywhere and
**one cable per camera** (power + data + time) at the telescope. Scales
to AUX2 by adding one PoE drop.

Carrier-board selection — three requirements:
1. **Route the native CM4 gigabit Ethernet to the (PoE) magjack** — the
   CM4's PTP + PPS live on its built-in PHY; avoid boards that add the
   timed link via a USB/PCIe NIC (those usually lack HW timestamping).
2. **PoE** — built-in PD, or PoE magjack + PD chip.
3. **Expose `SYNC_IN`** (grandmaster only, for GPS-PPS); clients can
   fall back to GPIO-PPS.

Board options:
- **Official CM4 IO Board** (+ PoE add-on) — exposes `SYNC_IN/OUT`
  (pin 9 wired); ideal grandmaster, bulky as a host.
- **Waveshare CM4 PoE Board** — compact, PoE built in, native GbE; good
  client board (verify `SYNC_IN` breakout; GPIO-PPS otherwise).
- **Switchberry** (TimeAppliances) — purpose-built timing appliance
  (5-port GbE switch + DPLL, grandmaster or client) if timing becomes
  observatory-wide infrastructure.

Trade-off vs A: one GPS instead of one-per-host and sub-µs instead of
~1 µs, but reintroduces a network + single-grandmaster dependency
(mitigate with BMCA failover or a second GPS-CM4). Also a hardware
refresh (replaces the current Pi hosts).

### Deciding

Confirm the current hosts first: `cat /proc/device-tree/model` and
`ethtool -T eth0` (hardware timestamping shows as
`hardware-transmit`/`hardware-receive`). If the guiders stay Pi 4,
choose **A** (per-host GPS) over **B** — more accurate and more robust
for the same rough effort. If the observatory is standardizing timing
and can absorb a hardware refresh, **C** (CM4 + PoE) is the clean,
scalable, single-cable answer. **B** is only the "reuse what's here
cheaply" fallback. Because all three meet 1 ms, this is an
infrastructure/operations decision, not an accuracy one.

## Relation to other work

- Continuous host time sync (chrony/PTP) is a prerequisite and is
  tracked separately for the ansible rollout (see project memory).
- If sub-ms proves impossible over USB, the fallback is the
  proto-Lightspeed architecture (GPS PCIe/HAT with PPS trigger + TTL
  end-of-readout tag) — same telescope, proven ≤30 µs — at the cost of
  added hardware and losing the ZWO's simple USB streaming.

## Primary sources

- Barry et al. 2015, PASA 32 e014 — SEXTA (arXiv:1503.05705)
- Dhillon et al. 2021, MNRAS — HiPERCAM timing (arXiv:2107.10124)
- Layden, Burdge et al. 2026, PASP — proto-Lightspeed, Magellan Clay
  (arXiv:2601.16268)
- Basden et al. 2016 — CANARY/DRAGON WFS sync (arXiv:1603.07527)
- Kulcsár et al. 2018, SPIE 10703 — WFS camera latency measurement
- Geerling 2022 — PTP hardware timestamping on the Pi CM4 (jeffgeerling.com)
- jclark, rpi-cm4-ptp-guide — CM4/CM5 hardware PTP + PPS-disciplined PHC
- linuxptp — `ts2phc` (GPS-PPS → PHC discipline) man page
- Switchberry (TimeAppliances) — CM4 timing appliance, GM/client
