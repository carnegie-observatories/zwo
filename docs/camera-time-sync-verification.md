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

## First on-hardware validation — 2026-07-16 (non-GPS flasher)

First run of the pipeline above, as a baseline before the GPS-PPS LED.

Cameras at test time (recorded 2026-07-16 via `open` +
`ASIGetSerialNumber`, before the planned swap to a matched pair):

| host | IP | model | sensor (full) | bitDepth | serial (hex) |
|---|---|---|---|---|---|
| zwoserver01 | 10.8.80.225 | ZWO_ASI294MM_Pro | 8288×5644 | 12 | `2a354b041d010900` |
| zwoserver02 | 10.8.80.218 | ZWO_ASI1600MM_Pro | 4656×3520 | 12 | `313084041f070900` |

Note the two are **different models** (294MM Pro vs 1600MM Pro —
different sensors and readout), which is the main confound below.

- **Setup:** both cameras rebuilt from the repo (server v1.0.6,
  identical build), captured simultaneously from zwo-nuc with
  `two_camera_capture.sh` (bin 2, 10% ROI, 10 ms exposure, gain 200,
  ~97 fps each, 60 s). Light: a plain ~2.9 Hz LED flasher (no GPS)
  imaged by both cameras; 171 flashes. Analyzed with
  `plot_two_camera_sync.py`.
- **Result — cross-camera timestamp alignment (server clocks):
  +1.75 ± 0.87 ms** (median ± per-flash std; median SE ~0.07 ms),
  stable over 60 s with no drift. Client-arrival skew (common zwo-nuc
  clock): +1.13 ± 0.93 ms; cross-correlation +0.92 ms. Per-camera
  frame-interval jitter 0.34 ms each (matches the 0.35 ms single-camera
  figure).
- **Interpretation:** the ~0.6 ms gap between the server-clock (1.75)
  and client-arrival (1.13) numbers is the residual between the two
  Pis' clocks. Both are NTP clients of zwo-nuc, so this is
  NTP-over-LAN client-to-client residual, **not** independent drift.
- **Caveats on this baseline:**
  1. The two cameras were **different models** at test time (294MM Pro
     vs 1600MM Pro; see the serial table above), so part of the offset
     is a fixed readout-timing difference between sensors, not clock
     error. A re-test with **two identical cameras** (planned) removes
     that confound and isolates the clock term.
  2. Upgrading 218 from the old v1.0.4 to v1.0.6 alone halved the
     arrival skew (2.77 → 1.13 ms) — the old server's 5 ms poll and
     missing latency fixes were a large part of the first measurement.
- **Takeaway:** cross-camera timestamps are already usable at the
  ~1–2 ms level; proper clock discipline (per-host GPS or PTP, below)
  plus identical cameras should bring this to sub-ms. (Confirmed — see
  the matched-pair re-test below.)

## Re-test with matched cameras — 2026-07-16 (non-GPS flasher)

218's camera was swapped so both hosts run the **same model**
(ASI294MM Pro). Serials re-recorded via `open` + `ASIGetSerialNumber`:

| host | IP | model | sensor | bitDepth | serial (hex) |
|---|---|---|---|---|---|
| zwoserver01 | 10.8.80.225 | ZWO_ASI294MM_Pro | 8288×5644 | 12 | `2a354b041d010900` (unchanged) |
| zwoserver02 | 10.8.80.218 | ZWO_ASI294MM_Pro | 8288×5644 | 12 | `3924d3032a010900` (new) |

Identical capture config to the baseline (both v1.0.6; bin 2, 10% ROI,
10 ms, **gain 200 on both**, ~98 fps, 60 s; same ~2.9 Hz flasher,
173 flashes).

![Two ASI294MM Pro cameras on a common bracket, both imaging a blinking
headlamp LED; a Raspberry Pi sits beneath the mount.](images/two-camera-sync-setup.jpg)

*Setup: the two matched guiders side by side, both pointed at the same
blinking headlamp LED (foreground). No GPS — the shared flash is the
fiducial; only relative timing is measured.*

![Per-flash inter-camera delay: flux traces, delay vs time, and
histogram. Server-clock delay (red) hugs zero and is tighter than the
client-arrival delay (blue).](images/two-camera-sync-matched-result.png)

- **Result — cross-camera timestamp alignment (server clocks):
  −0.036 ± 0.19 ms (≈36 µs median, SE ~0.014 ms)**, no drift over 60 s.
  Client-arrival skew: −0.17 ± 0.40 ms; cross-correlation −0.30 ms.
  Per-camera jitter 0.28 / 0.55 ms.
- **The offset dropped from +1.75 ms (different models) to ~36 µs
  (matched)** — confirming the baseline was dominated by the fixed
  readout-timing difference between the 294MM Pro and 1600MM Pro, not
  clock error. With that confound gone, the residual is just the
  NTP-client-to-client alignment between the two Pis, which is
  **sub-100 µs** on this LAN.
- The server-clock delay is **tighter than the client-arrival delay**
  (0.19 vs 0.40 ms std) because the server ns timestamp removes
  transport/network jitter — exactly what the timestamps are for.
- **Bottom line:** two matched ASI294MM Pro guiders, NTP-synced to a
  common host, already cross-align to ~tens of µs — comfortably under
  the ≲1 ms goal, with server timestamps demonstrably better than
  arrival-time analysis. The GPS-PPS LED (below) would pin this to
  absolute UTC and validate it against a known reference, but for
  relative cross-camera correlation the requirement is already met.

## 30-minute stability run — 2026-07-16 (drift + glitch census)

Same matched pair and config, extended to **30 min** (~176,800 frames
per camera, 5196 flashes) to look for slow drift and rare software
glitches that a 60 s snapshot cannot see.

![30-min run: delay vs time (banded), bimodal delay histogram, and
per-frame delivery interval with rare stall spikes.](images/two-camera-sync-30min.png)

- **Clock drift: none.** Server-clock inter-camera delay drift is
  **−0.02 µs/min over 30 min** (client-clock +3.7 µs/min) — flat, no
  walk or NTP sawtooth at our resolution. Median stays −0.30 ms,
  consistent with the 60 s value. **The NTP-client-of-a-common-host
  setup holds the two cameras aligned with no measurable drift for at
  least half an hour.**
- **Software glitches: rare, brief, independent.** 5 delivery stalls
  total (A: 2, B: 3), each **~30 ms** (≈3 frame intervals), rate
  **~0.1/min per camera** — one hiccup per ~10 min. They occur at
  *different* times on A vs B (independent, not coincident), so a stall
  momentarily perturbs one camera only. This is why the 60 s runs saw
  zero. A fast-guiding loop must tolerate/mask a ~30 ms gap on one
  camera roughly every 10 min.
- **A measurement-method beat the long run exposed.** The per-flash
  delay is **bimodal** (bands at ≈−1 ms and ≈+1.5 ms; see the middle
  and histogram panels), so the robust spread grew from 0.13 ms (60 s)
  to ~0.96 ms MAD. This is **not** a clock effect — the median and
  drift are unchanged. It is a **beat between the ~2.9 Hz flasher and
  the ~98.8 fps frame sampling**: edge-crossing timing has a residual
  frame-quantization that cycles as the two cameras' grids slide
  against the flash. The 60 s run happened to catch one beat phase
  (hence its misleadingly tight 0.19 ms). The true alignment is the
  drift-free median (~0.3 ms); the ~1 ms spread is method, reducible
  with a higher frame rate (193 fps ≈ halves it) or a sharp-edged
  GPS-PPS LED.
- **Takeaway:** over 30 min the two matched guiders stay aligned with
  **no drift**, median ~0.3 ms, punctuated only by rare independent
  ~30 ms stalls. The apparent sub-ms→~1 ms jitter growth is a flasher/
  sampling beat, not the clocks. This closes the drift and glitch
  questions for the current (NTP + simple flasher) configuration.

### Stall mechanism — two regimes (seq-step evidence)

Checking whether each stall dropped a frame (seq jumps) or delivered it
late (seq contiguous) separates two distinct causes, which dominate at
different frame rates:

| | 98 fps (10 ms) | 193 fps (5 ms) |
|---|---|---|
| stall rate | ~0.1 / min | **~15 / min** |
| size | ~30 ms | ~65 ms (fixed) |
| seq-step | 3 → **2 frames dropped** | 1 → **frame late, not dropped** |
| cause | **client** misses its pull deadline (double-buffer overwritten); likely aggravated by the per-frame `--frame-log` I/O to NVMe on zwo-nuc | **server-side SDK lost-wakeup** — `CirBuf::ReadBuff` sleeps the full `ASIGetVideoData` timeout (the ~65 ms = 50 ms floor + exposure) |

Key point: at 193 fps the frames are **delivered late, not lost**
(seq-step 1) and each keeps a correct ns timestamp — so for
timestamp-based science (PSD/cross-correlation) the data is not
corrupted, only non-uniformly sampled. A **fixed-cadence guiding loop**
at 193 fps, however, would hit a ~65 ms gap every ~4 s.

### 193 fps run — beat test defeated by the SDK stalls

Repeating the 30-min run at ~197 fps (5 ms, 5% ROI, gain 300) to test
whether the higher sampling rate halves the flasher/sampling beat:

![193 fps 30-min run: the per-frame panel shows a dense ~65 ms stall
band; the delay panel is peppered with ±60-80 ms stall-contaminated
outliers.](images/two-camera-sync-30min-193fps.png)

It does the opposite. The ~15/min × 65 ms SDK stalls contaminate the
flash-edge measurement so heavily (MAD 3.3 ms even after 5σ rejection;
apparent median −4.9 ms and −114 µs/min "drift" are artifacts of A
stalling more than B, 17 vs 13/min) that any beat is unmeasurable.
**Higher frame rate makes the *sync measurement* worse, not better** —
the beat is only cleanly seen at 98 fps, where the SDK stalls are rare.
The true clock alignment is unchanged (it does not depend on camera
fps); only the flash-edge method degrades.

Mitigations for the 65 ms SDK stalls: lower the `ASIGetVideoData`
timeout floor (50→~15 ms) to shorten each stall; test whether
`ASI_BANDWIDTHOVERLOAD` / `ASI_HIGH_SPEED_MODE` reduce the stall *rate*
(untested); for guiding, run ~98 fps (stalls rare) or mask the gaps;
for PSD, they are already tolerable (correctly timestamped).

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
