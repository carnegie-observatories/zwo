# gcam: geometry mode `g` — 500×500 at bin 2 for the legacy guider cameras — Plan

**Status:** implemented on branch `feature/gcam-legacy-geometry`, stacked
on [PR #44](https://github.com/carnegie-observatories/zwo/pull/44)
(`feature/gcam-gframe`); built and tested against the emulator on macOS
(§ Verification). A scratch build with the same mode ran end to end on
the bench camera on 2026-11-20. Line references are to this branch.

## What this is

A geometry mode for zwogcam that fits the old OCIW guider camera (the
CCD camera with its DSP readout, used at Baade, Clay and Swope) when
`gcamserver` serves it. `gcamserver` is in the gcam-linux repository,
branch `gcamserver`, and speaks the zwoserver protocol on port 52311.

```
gcamzwo -mg ...          # or "mode g" in the .ini
```

The mode makes the GUI send `setup 0 0 500 500 2 16`: the whole chip at
bin 2, shown 1:1 in a 500-pixel image panel.

The branch also adds a check in `zwo_setup()`. The GUI now reads the
server's answer to `setup` and stops with a message when the server
refused the window or changed its size. Without that check, mode `g`
on a ZWO camera desynchronises the frame stream (§ 2).

## Why a new mode

`gcamserver` answers `open` with `1000 1002 1 0 16 GCAM`: 1000 × 1002
unbinned-equivalent pixels. The DSP reads 500 × 501 at bin 2 and
250 × 251 at bin 4 (gcam-linux `docs/plans/gcamserver-decisions.md`,
entries 8 and 52). The server rejects with `-Eerr=26` any window it
cannot read (entry 7). It accepts bin 2 or 4 only, 16 bits only, and a
window with x, y ≥ 0, x + w ≤ 1000/bin and y + h ≤ 1002/bin.

`zwo_setup()` centres a square window of `baseD` binned pixels on the
sensor ([zwotcp.c:200-202](../../src/gcam/zwotcp.c#L200-L202)):
x = offx + (sensorW/bin − baseD)/2, y = offy + (sensorH/bin − baseD)/2.
Every existing mode is wider than the chip at bin 2:

| mode | baseD (binned px) | bin | GUI image | x on this camera |
|---|---|---|---|---|
| default | 1800 | 2 | 600 | −650 |
| `1` | 504 | 2 | 504 | −2 |
| `2` (PFS) | 1000 | 2 | 500 | −250 |
| `3` (MIKE) | 1512 | 2 | 504 | −506 |
| `4` | 2000 | 2 | 500 | −750 |
| `5` (MagE) | 2520 | 2 | 504 | −1010 |
| `h` | 2048 | 1 | 512 | bin 1 refused |
| `f` | 2400 | 2 | 600 | −950 |
| **`g`** | **500** | **2** | **500** | **0** (y = (501 − 500)/2 = 0) |

## Design

### 1. The mode — `setup_m_switch()`, zwogcam.c

```c
case 'g': baseD= 500; baseB=2; baseI=500; pHIGH=82;  break; /* legacy gcam */
```

- `baseI=500, pHIGH=82` are the values modes `2` and `4` use, so the
  window layout is one that is already in production.
- `baseD == baseI` gives a frame-to-window scale of 1, a path mode `1`
  (504/504) already exercises.
- The letter `g` stands for gcam. It is also the letter the bench run
  used, so the .ini from that run keeps working.
- One mode serves all three telescopes: the camera and its readout
  geometry are the same at Baade, Clay and Swope (gcam-linux
  `docs/plans/gcamserver-swope-plan.md`, "Camera").

### 2. `zwo_setup()` reads the answer — zwotcp.c

On `main`, `zwo_setup()` sends `setup x y w h bin 16`, ignores the
answer and sets `aoiW = aoiH = dim`; `run_cycle` then reads
`dim × dim × 2` bytes per frame. (On this branch the check sits between
the two: [zwotcp.c:200-214](../../src/gcam/zwotcp.c#L200-L214).) Two
answers break that without any message:

- **Refused.** `gcamserver` answers `-Eerr=26` to a window outside the
  chip, and the GUI goes on to `start` video anyway. Mode `1` asks for
  exactly such a window.
- **Resized.** zwoserver rounds the width down to a multiple of 8
  ([zwoserver.c:575](../../src/server/zwoserver.c#L575)) and echoes the
  geometry it set. Every existing mode is a multiple of 8, but 500 is
  not. With mode `g` on a ZWO camera, the server sends 496 × 500 frames
  while gcam reads 500 × 500, so the binary stream desynchronises. On
  the emulator, the GUI reports "incomplete frame" and video drops from
  4.2 to 0.33 frames/s.

After the change, `zwo_setup()` fails with `E_zwo_setpar` when either of
these is true:

- the answer starts with `-E`;
- the answer parses as six numbers and its width, height or bit depth
  differs from the request.

It also logs `setup <request>: server answered '<answer>'` in red.
`run_init` then reports "setup failed" and does not start video. An
answer that is not six numbers is accepted as before. Every server we
know (zwoserver, `gcamserver`, `src/py/zwo_emulator.py`) echoes six
numbers, but accepting other answers means the change can only turn
silent failures into reported ones.

The offsets are not checked: a different x or y moves the window but
does not change the frame size.

### 3. Manual — docs/ZWO/zwogcam.html

A `-mg` line in the [`-m`](../ZWO/zwogcam.html#switch_m) list.

## Using the camera from the GUI (unchanged here)

These work as they are, but read differently with this camera. Set them
in the .ini, or see the follow-ups below.

- **Offsets.** In mode `g` the window is the whole chip, so `offx` must be
  0 and `offy` 0 or 1. The server refuses any other value, and with § 2
  the GUI now reports it.
- **Pixel scale `px`** (milli-arcsec per binned pixel): 184 at Magellan
  and 320 at Swope. The DOS programs use 0.092″ and 0.160″ per unbinned
  pixel.
- **Guide box `bx`** is in binned pixels. 41 px is 2.2″ at MIKE's 54 mas
  but 7.5″ at 184 mas, so scale it to the angular size wanted.
- **Gain.** The presets `lo/med/hi` are ASI294 numbers (0/150/300,
  [zwogcam.c:1706-1709](../../src/gcam/zwogcam.c#L1706-L1709)), and the
  default is `hi` ([zwogcam.c:345](../../src/gcam/zwogcam.c#L345)).
  `gcamserver` passes the number to the amplifier-gain DAC (0–4095;
  gcam-linux decision 17). Put a number in the .ini: the DOS programs
  default to 0 at Magellan and 1 at Swope (gcam-linux
  `gcamserver-swope-plan.md`). `offset` has no effect on this camera.
- **Frame rate.** The camera has no shutter, so the frame period is
  readout + exptime. A full frame at bin 2 takes 0.75 s to read, which
  limits the camera to about 1.3 frames/s. The GUI's `next` waits at most
  2 s ([zwotcp.c:400](../../src/gcam/zwotcp.c#L400)). With exptime above
  about 1.25 s, every frame costs one `-Enodata` round trip: a "no data"
  line on stdout and up to 0.35 s of extra latency
  ([zwotcp.c:409-419](../../src/gcam/zwotcp.c#L409-L419)). This is
  harmless and left as it is.
- **Bad-pixel mask.** The mask file is named by serial and width,
  `zwo<serial>_500.mask`
  ([zwogcam.c:2373](../../src/gcam/zwogcam.c#L2373)). `gcamserver` takes
  the serial from its rc key `serial`, which defaults to 0. Give each head
  its own serial, or every head will share one mask file.
- **Pixel values.** `gcamserver` sends the 16-bit ADC values. The GUI's
  `>> 2` puts them on the 14-bit scale that the display, the saturation
  colour and the FITS consumers expect (gcam-linux decision 11). The
  display defaults (`pct`, `bkg`, `span`) were tuned on ZWO frames;
  check them when commissioning.
- **FITS `PSIZE`** is the constant 4.63 µm, the ASI294 pixel
  ([zwogcam.c:521](../../src/gcam/zwogcam.c#L521)). It is wrong for this
  CCD (13 µm; [ASI294MM-P_guider_swap_summary.md](../ASI294MM-P_guider_swap_summary.md)).
  This is a follow-up.

## Out of scope (follow-ups)

- **Guide-window readout.** The DOS guide mode reads 36 × 36 at bin 2 at
  10–40 frames/s. The GUI always reads its whole window, so it guides at
  1.3 Hz at most on this camera; a 50 × 50 window runs at about
  27 frames/s. Getting that speed needs the GUI to re-`setup` around the
  guide star while guiding. That is a design change of its own.
- **A bin-4 mode** (250 × 250, 0.26 s per frame) for faster full-field
  video. It would show a 250-pixel frame in a 500-pixel panel, a scale
  below 1 that no current mode uses.
- **A `psize` .ini key** for the FITS header.
- **`message()` overflow.** On the bench, gcamzwo crashed at start-up
  with a buffer overflow in `message()`, reached from `load_mask()`, when
  `GCAMZWOPATH` was a long path. This is a separate bug that also affects
  ZWO cameras.
- **`gcamserver`, server side.** Its `setup` stores the requested window
  (in globals and in the rc file) before it validates the window, and
  keeps it after rejecting it. A later `start` then sizes its video
  buffers from the rejected window while the hardware still reads the old
  one. The GUI check in § 2 stops gcam from sending `start` after a
  refusal, but the server should not depend on that.

## Verification

Run on macOS on 2026-11-21: gcamzwo built with the make overrides (no
makefile change), started with `-t 0` and gnum 2.

1. **Emulator with this camera's geometry.** A wrapper around
   `src/py/zwo_emulator.py` (test harness, not committed) with a
   1000 × 1002 sensor, model `GCAM`, and `gcamserver`'s `setup` rules
   (no rounding, `-Eerr=26`):
   - mode `g`: `setup 0 0 500 500 2 16` was accepted and video ran
     (`status`: `init=1 loop=1`, cursor at 250, 250). `write` saved a
     FITS with `NAXIS1 = NAXIS2 = 500`, `BINNING = '2x2'`,
     `INSTRUME = 'GCAM'`, `SCALE = 0.1840`;
   - mode `1`: the GUI sent `setup -2 -1 504 504 2 16`, showed
     `setup -2 -1 504 504 2 16: server answered '-Eerr=26'` and then
     `setup failed`, and did not start video.
2. **The stock emulator** (4656 × 3520, rounds widths to a multiple of 8):
   - mode `g`: the GUI showed `setup 914 630 500 500 2 16: server answered
     '914 630 496 500 2 16'` and stopped. The same case built without
     § 2 started video and reported "incomplete frame" at 0.33 frames/s;
   - mode `2`: ran as before (`loop=1`, 4.2 frames/s, cursor at 500, 500).
3. **Bench camera** (`lco-gcam-dev`): to be run with William. A scratch
   build with the same `case 'g'` line ran video, close, reset/reconnect
   and the cooler end to end on 2026-11-20 (gcam-linux decision 52).
   § 2 has not run against `gcamserver` itself.

## Deliverables in the PR

- `src/gcam/zwogcam.c`: `case 'g'` in `setup_m_switch()`.
- `src/gcam/zwotcp.c`: `zwo_setup()` checks the answer.
- `docs/ZWO/zwogcam.html`: `-mg` in the `-m` list.
- This plan.
