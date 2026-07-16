# Pushing the ASI294MM Pro past the SDK — exploration

**Status:** exploration / feasibility notes, not a committed plan.
**Date:** 2026-07-17
**Question:** the ~5 ms frame floor and the ~30 ms lost-wakeup stalls at
193 fps are both imposed by `libASICamera2` (closed source). What could
be gained by (a) staying in the SDK but using features we haven't
touched, or (b) bypassing the SDK to talk to the camera directly over
libusb / a kernel DMA buffer? Kept separate from the validated
[camera-time-sync-verification.md](../camera-time-sync-verification.md)
and the [200 Hz report](../ASI294MM-P_200Hz_ROI_report.md) so the
speculative work does not contaminate the measured results.

## What we already know about the SDK internals

From the gdb backtraces taken during the stall investigation, the SDK's
acquisition path is:

```
ASIGetVideoData
  └ CCameraS492MM_Pro::GetImage
      └ CirBuf::ReadBuff              ← pthread_cond_timedwait (the stall)
CCameraFX3::startAsyncXfer
  └ libusb_handle_events_timeout      ← libusb async bulk transfers
     (+ internal threads: MyThr, WorkingFunc, SetGainExpFunc, AutoTempFunc)
```

So concretely: the camera is a **Cypress EZ-USB FX3** USB-3 bridge; the
SDK drives it with **libusb async bulk transfers**; a worker thread
fills a **circular buffer**; `ASIGetVideoData` is a consumer that waits
on a condition variable — and *that* wait is where the lost-wakeup
30–65 ms stalls come from. This tells us the two independent levers:
the **transfer path** (FX3 + libusb) and the **buffer/wakeup path**
(CirBuf). Both are things a direct implementation could own.

## Track A — unused features *inside* the provided SDK (low risk, do first)

The header (`ASICamera2.h`) exposes far more than we've used. Ranked by
likely payoff for "push to the limit":

### A1. Soft-trigger mode — the big one
`ASISetCameraMode(ASI_MODE_TRIG_SOFT_EDGE)` + `ASISendSoftTrigger()`
turns off free-running video and makes the camera expose **one frame
per software trigger**. No hardware pin is needed (that is the *soft*
mode; the RISE/FALL/LEVEL modes need the trigger port). Why it may help
the stalls and the timing:
- The ~30 ms lost-wakeup is a race in the *free-running* video consumer
  (`CirBuf::ReadBuff` waiting for the next auto-produced frame). In
  trigger mode the host controls when each frame is produced, so the
  producer/consumer timing is deterministic — the lost-wakeup class of
  stall may simply not occur.
- The exposure *start* becomes host-timed (the `ASISendSoftTrigger`
  call), giving a much better per-frame time reference than
  "whenever ASIGetVideoData returned".
- Cost: throughput. A trigger→expose→readout→fetch round trip is likely
  slower than the pipelined free-running video, so this trades peak fps
  for determinism. Worth measuring: soft-trigger fps and stall rate vs
  the free-running numbers.
- **Gated on `IsTriggerCam` and the supported-mode list** — verified by
  probe (results appended below).

### A2. GPS hardware timestamps — if the sensor supports it
`ASIGetVideoDataGPS()` / `ASIGetDataAfterExpGPS()` return an
`ASI_GPS_DATA` (UTC datetime + lat/long/alt/sats) *per frame*, and
there are `ASI_GPS_SUPPORT / ASI_GPS_START_LINE / ASI_GPS_END_LINE`
controls. On a GPS-equipped ASI body this is **hardware, in-camera,
UTC-referenced frame timestamping** — exactly the absolute-time upgrade
the sync doc calls out as the open item, and it would beat the flash-LED
method outright. **Almost certainly not present on the plain 294MM Pro**
(no GPS receiver in the body), but the control is worth probing, and it
argues for a GPS-capable body (or the ASI GPS models) if absolute UTC
becomes a requirement.

### A3. `ASI_ROLLING_INTERVAL`
Directly addresses the open "rolling-shutter row-dependent exposure
offset" question — it exposes the per-row readout interval, so the
row-to-row time skew across the ROI can be *computed* rather than
guessed. Cheap to read; useful for the PSD timing model regardless of
anything else here.

### A4. `ASI_OVERCLOCK` and `ASI_HARDWARE_BIN`
- `ASI_OVERCLOCK` — pushes the sensor/readout clock above nominal. Could
  shave the ~5.15 ms bin-2 floor, at a cost in read noise / thermal /
  stability. Sweepable exactly like the usb/highspeed test we already
  ran; the honest expectation is "small, risky".
- `ASI_HARDWARE_BIN` — bin on-sensor instead of in the SDK. We use
  `bin 2`; if it is currently *software* binning, switching to hardware
  bin could cut both readout time and USB payload. Worth an A/B.

### A5. Trigger *output* (`ASISetTriggerOutputIOConf`)
Even if the camera can't be triggered *in*, it may drive a **TTL output
pin high during exposure**. That signal, fed to a cheap GPS-PPS logger
(or the Stamp-of-Approval box), gives an external hardware timestamp of
every exposure — a middle path between the flash-LED method and full
GPS, and it sidesteps the "no trigger input" limitation noted earlier.
Probe `IsTriggerCam` / the IO-conf call to see if the pin exists.

### A6. Cheap hygiene we haven't done
- `ASIGetDroppedFrames` — the SDK's own dropped-frame counter; cross-check
  against our seq-gap census.
- Move the periodic `tempcon` (temperature/cooler poll) fully off the
  acquisition thread — it currently runs inline in `run_video` every
  30 s and is a candidate micro-stall source.

## Track B — bypass the SDK (high risk / high effort)

### B1. Direct libusb re-implementation
Feasible *in principle* — the SDK is just libusb underneath — but it
means reverse-engineering ZWO's undocumented, proprietary USB protocol:
the vendor control transfers that configure the FX3/sensor (ROI, gain,
exposure, clocking) and the bulk-IN framing. Approach would be:
`usbmon`/Wireshark capture of the SDK talking to the camera, plus static
analysis of `libASICamera2.so`, to recover the register writes and the
bulk transfer geometry.
- **Upside that is real:** own the completion path, so the
  `CirBuf::ReadBuff` lost-wakeup simply doesn't exist; timestamp each
  frame at the *URB completion* instant (tighter than the SDK's delivery
  point); queue many transfers deep to eliminate drops.
- **Upside that is *not* real:** the ~5 ms sensor readout floor is set
  by the sensor+FX3, not the SDK — direct USB won't move it.
- **Costs:** large effort; brittle against firmware/model changes; the
  ZWO SDK EULA very likely prohibits reverse engineering (legal
  review needed before any distribution); a maintenance burden the
  observatory would then own. Prior art is thin — the open ecosystems
  (INDI, oacapture) almost all wrap the official SDK rather than
  reimplement it, which is itself a signal about the effort/reward.

### B2. Zero-copy / kernel DMA buffer
This one is a **smaller, cleaner win and does not require reverse
engineering** — it can be done in a direct-libusb build *or*, if ZWO
ever exposed it, inside the SDK:
- libusb can allocate DMA-capable buffers with `libusb_dev_mem_alloc`
  (backed by `usbdevfs` `mmap`), so the kernel USB stack DMAs frame data
  straight into user memory with **no intermediate memcpy**. On a Pi 4
  at ~100 MB/s this saves real CPU and a copy's worth of latency.
- USB-3 **bulk streams** (`USBDEVFS_ALLOC_STREAMS`) let the FX3 pipeline
  multiple outstanding transfers on one endpoint — higher sustained
  throughput and fewer gaps than single-buffered bulk.
- These only matter once B1 exists (you need to own the transfer setup),
  so they're a *reason* B1 might be worth it if raw throughput — not
  just determinism — turns out to be the wall.

## Recommendation / order of attack

1. **Probe the camera** for `IsTriggerCam`, supported modes, GPS,
   overclock, hardware-bin, rolling-interval (done — see below).
2. **Track A first, in this order:** soft-trigger determinism test (A1)
   → hardware-bin A/B (A4) → rolling-interval readout model (A3) →
   overclock sweep (A4) → trigger-output timestamp path (A5). All are
   low-risk, days not weeks, and any one could beat the current
   free-running behaviour.
3. **Only consider Track B** if Track A can't deliver the determinism
   *and* raw throughput needed, and there is appetite for the effort +
   legal review. Even then, do B2 (zero-copy) inside a minimal direct
   path aimed only at the frames endpoint, not a full SDK replacement.
4. If **absolute UTC** ever becomes the requirement, a GPS-capable body
   (A2) or the trigger-output-to-GPS-logger path (A5) is a far better
   investment than reverse engineering.

## Probe results (this camera)

<!-- filled in from asi_probe on zwoserver01 when connectivity allows -->
_Pending — `asi_probe` run queued against 10.8.80.225._
