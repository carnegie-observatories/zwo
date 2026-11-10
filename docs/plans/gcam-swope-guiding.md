# gcam: guide corrections for the Swope TCS (gframe) — Plan

**Status:** implemented on `feature/gcam-gframe`, based on v1.1.1, 2026-11-09:
the module, its bench and the camera-emulator rig pass on macOS and Linux;
nothing has run against the Swope TCS or on the sky yet.
[Implementation](#implementation-2026-11-09) lists the files, the checks and
what is left. The module's own reference is
[src/gcam/gframe/README.md](../../src/gcam/gframe/README.md); this file is the
why and the way to the sky.

Paths outside this repository are sibling checkouts, as in the other plans:
- `Swope-TCS/` = [lco-software/Swope-TCS](../../../lco-software/Swope-TCS)
  (HEAD 4e8992f; GitLab `mfiaschi/Swope-TCS`, Marco Fiaschi). It runs on a
  Raspberry Pi 4 at 10.8.80.53.
- `GCAM_C40/` = [lco-software/old_removed/GCAM_C40](../../../lco-software/old_removed/GCAM_C40),
  the disk of the Swope guider PC; `AUTOEXEC.BAT:51-52` starts
  `GCOPTV1.J1/GCAM.EXE`.
- `c100/` = [lco-software/c100-tcs/c100](../../../lco-software/c100-tcs/c100),
  the du Pont TCS.
- `swope/` = [swope](../../../swope), the Swope instrument app and its mock TCS.

## What this is

The swap test of [ASI294MM-P_guider_swap_summary.md](../ASI294MM-P_guider_swap_summary.md)
puts a ZWO camera, run by gcamzwo, on a Swope guider port. The camera side is
what Magellan already runs; the telescope side is not:

- The Swope TCS takes guide corrections the way the DOS guider PC sends them:
  `G xx yy CR LF`, seven bytes on a serial line, through a StarTech
  serial-to-Ethernet converter (10.8.80.54:10001) that the TCS **connects to**
  as a TCP client. No replies.
- gcamzwo sends corrections to a Magellan **TCSIS** as `aeg A E`
  ([telio.c](../../src/gcam/telio.c), `telio_aeg`), over a socket it opens
  itself, and polls the same server for some twenty status values.

So the two do not meet, and the gap is a role reversal, not a byte format:
both expect to be the client. One side has to serve.

## The two sides

**The frame** (`GCAM_C40/GCOPTV1.J1/GCTCS.F:99-125`): `'G'`, two digits of X,
two of Y, CR, LF. A count from -49 to +50, negatives written as `100+n`. The DOS
PC writes hundredths of an arcsecond, clamped there, so at most ±0.49" per
frame, about once a second (`GCIM1.F:896-964`: gain `0.35·sn·(err +
(av+1)·Δerr)`, sign inverted to correct).

**The Swope TCS** (`Swope-TCS/`):

- `GuiderClient.cpp:70-134` parses: bytes <= 12 dropped, `G` and four digits,
  >50 is negative, a frame with -49 or +50 on either axis ignored, **one count
  = 0.05"** (five times what the DOS PC means), X to RA, Y to Dec. A 3 s gap
  clears its buffer (`:57-63`). An axis that arrives while the previous
  correction on it runs is dropped, and the value **replaces** the last one,
  it is not added (`:120-134`).
- `Swope2.cpp:835-929` acts on an axis above `Guider_deadband` (0.1", strictly
  greater): RA as a tracking-rate pulse of `Pulse_ms` = 950 ms through a PID
  (`[Guider_RA]`, `:142-145`); Dec as a move of the slow Dec motor, closed loop
  unless `GUIDER_OPENLOOP`. Every 100 ms.
- `GUIDING false` (`CommandHandler.cpp:157-170`) pauses the processing; the
  Swope app does that around T+G offsets (`swope/src/Swope/TelescopeService.m`).
- It connects at power-on and retries every ~1.1 s while that fails
  (`Swope2.cpp:393-399, 569`). **It never notices a connection it had being
  dropped**: `GuiderClient::IsConnected()` is never polled and
  `state.guider_connected` stays true, so only `GUIDE` false then true
  (`CommandHandler.cpp:369-395`) or cycling the key reconnects it. That is
  [STI-831](https://jira.lco.cl/browse/STI-831) (2024-05, open): the
  converter dropped the session and the cure was the key. `GUIDE` also zeroes
  the differential tracking rates of `TRACKRATE` (`:370-374`). Neither TPS
  nor the Swope app sends `GUIDE`.

**The du Pont** reads the same frame from a tty, one per guider PC
(`c100/src/guider.cpp:105-127`, `c100.ini:171-176`), negates both axes and
hands the pair to the control loop (`updatethread.cpp:467-474`); its line is
two-way, the TCS writes `GBOX x y` to move the guide box
(`c100.cpp:1602-1607`). The same frame, the same kind of guider PC
([STI-88](https://jira.lco.cl/browse/STI-88)), its own sign and scale.

**gcamzwo** with `-t 0` (the default) has no TCS: every telio call sleeps a
few ms and returns 0, the FITS telescope block stays empty, and `aeg` goes
nowhere. That is the mode it runs in at Swope.

## Options

| | What | gcamzwo | Swope TCS | Elsewhere | Serves the du Pont |
|---|---|---|---|---|---|
| A | a bridge: TCSIS for gcamzwo, G frames for the TCS | a TCS host key | `Swope.ini` | ~200 lines of Python and a host to keep it on | the G half |
| B | a TCSIS server in the TCS | Swope entry, host key, EDS off | ~450 lines of C++, a release | | no |
| C | gcamzwo speaks the TCS's JSON | ~250 lines | the JSON server frames on braces with a 1 s timeout, not a feed | | no |
| **D** | **gcamzwo serves the G frames; the TCS connects to it** | **one module, 50 lines of hooks** | **`Swope.ini`** | | **yes** |

**D, chosen.** No TCS release: the TCS's parser, actuation, pause and tuning
stay exactly as they are, and so do the Swope app and its tests. The same
module serves the du Pont when its cameras change, and the direction matches
the rest of this repository: one GUI in front of every camera, speaking each
telescope's dialect (gcam-linux's `gcamserver` plan does the same for the old
camera heads). Its costs: 0.05" steps and at most 2.4" per frame, no telescope
data in the guider's FITS headers at Swope, and the TCS stays the connecting
side, so a gcamzwo restart needs a relink until the TCS polls its socket.

B stays the step for later (below).

## Design

All of it is in [src/gcam/gframe/](../../src/gcam/gframe/), a module with
standard headers only and nothing from gcamzwo, like `edstail/`. Outside it,
a table of one-line hooks ([README, In gcamzwo](../../src/gcam/gframe/README.md#in-gcamzwo)):
a `send_correction()` in `guider.c` that the three guide loops and mouse mode
3 call instead of `telio_aeg()`, and in `zwogcam.c` the `.ini` keys, the start
in `run_init()`, `gframe_clear()` on F1 and the `status` fields. When the TCS
takes corrections another way, the directory goes and the hooks are reverted
([Removing gframe](../../src/gcam/gframe/README.md#removing-gframe)).

What it does:

- **Sum, then send at a bounded rate.** gcamzwo computes a correction on every
  frame ([guider.c](../../src/gcam/guider.c), `run_guider1`), the DOS PC about
  once a second, and the TCS drops what arrives during its 950 ms pulse. So
  corrections are added to a pending pair, and at most one frame per
  `gframe_period` (1.5 s) carries it: the TCS sees the cadence it was tuned
  for, and nothing computed is lost to a busy axis.
- **In the unit the TCS reads.** `gframe_unit 0.05`: a frame means what the
  TCS applies. At most 48 counts (2.4"), never -49/+50; what does not fit is
  carried to the next frame. An axis at or below the TCS's dead band (0.1")
  waits for more instead of being sent and ignored.
- **Dropped when it cannot apply.** On F1, after 3 s without a correction (the
  TCS forgets after 3 s too), and while no client is connected, so a TCS that
  connects late does not receive a burst.
- **A server any client can watch.** Up to four clients get the same frames;
  `gframe_tcs.py` parses them with the TCS's rules. Keepalive on each
  client; connects and drops in the message window and log; `status` gains
  `gframe= gpend= gsent= gdrop=`.

## The Swope TCS: configuration and requests

Configuration, by Marco on the Pi, then a TCS restart:

```
[Config]
Guider_addr=<the gcamzwo host>
Guider_port=10001
```

Operating it: start gcamzwo before or after the TCS; the TCS retries until the
first connection. After a gcamzwo restart, relink from any host the TCS
accepts, and set `TRACKRATE` again if a moving target is being tracked:

```
src/gcam/gframe/gframe_tcs.py --relink 10.8.80.53
```

Going back to the DOS PC is `Guider_addr=10.8.80.54` and a restart; the
converter stays wired through the test.

Requests to the TCS, none needed for gframe:

1. Poll `guider.IsConnected()` in the 1.1 s loop and clear
   `guider_connected` when it is false: the TCS then reconnects by itself, to
   gcamzwo or the converter. One line; it closes STI-831.
2. TCP keepalive on the guider socket, for a peer that vanishes silently.
3. Add frames that arrive during a correction instead of dropping them; then
   `gframe_period` can come down to the pulse length.
4. Guider state in `STATUS` (link, `GUIDING`, last correction), as requested
   in [swope/docs/plans/telescope_control.md §9.2](../../../swope/docs/plans/telescope_control.md)
   item 3.

## To the sky

1. **Camera.** The ASI294MM-P on a guider port per the swap summary; its Pi
   deployed like the Magellan ones (lco-ansible `zwo_server_deploy.yml`, a
   `swope_zwo_servers` group), named in `etc/ini/swope_gcam/gcam01.ini`'s
   `host`. Geometry: the camera has 2.3 µm pixels (8288×5644), so bin 2 gives
   0.058"/px (`px 58`) and mode 5, 2520×2520 binned, shows 2.4' of the 2.78'
   illuminated patch. The whole patch would need a 2822 mode
   (`setup_m_switch`), a separate one-line change if wanted.
2. **gcamzwo** on swope-obs (XQuartz is there) or a Linux box with a fixed
   address, `-f gcam01.ini`, no `-t`. `Swope.ini` points at it (above).
3. **Static.** `gframe_tcs.py --host <gcamzwo host> --port 10001` connects as a
   second client; F3 on a star: frames in it, and `Guider X.. Y..` lines in the
   TCS's `tmux` (STI-831, comment 1).
4. **Signs and scale**, tracking, guiding off: mouse mode 3 ("star at mouse to
   box") a few arcsec in camera +x, then +y: watch which way the star moves,
   set `angle` and `parity` (`pa`, `parity` work at run time) until F3 with
   `sn 0.3` converges on both axes. If the camera is right on the sky but the
   TCS's sign is the other way, `gframe_xsign`/`gframe_ysign`. Note the
   telescope's step for a 2" frame: the first calibration of the TCS's RA pulse
   in real arcseconds.
5. **Tuning and comparison.** `exptime` 0.5-1 s, `av` 0-2, `sn` 0.3 up to about
   0.6; watch `gpend` and the `az`/`el` graphs. The same night, switch
   `Guider_addr` back to the DOS PC on the same star and compare the centroid
   rms. Pass: no worse than the DOS PC, no `Ignored guide command` in the TCS
   log, no drops.
6. **The app's offsets.** T+G and keyboard guiding with gcamzwo guiding: the
   pause and resume work as with the DOS PC (frames keep coming; the TCS
   ignores them while `GUIDING` is false).

Then the DOS PC and the converter can go, the Swope manual's guider section is
rewritten for gcamzwo, and STI-831 is closed by request 1.

## Later: a TCSIS in the TCS (option B)

When telescope data in the guider frames, `apa`, sub-0.05" steps, or mouse
offsets through `ofra/ofdc/offp` are wanted, the TCS serves TCSIS on 5801: the
status subset gcamzwo polls (`ra dec epoch ut st ha airmass telaz telel zd
telpa telfocus telguide mountmv`, the rotator and probe keywords as zeros),
`aeg` into an accumulator that adds and clamps, `0` to every command it
accepts (a `1` makes gcamzwo drop its socket for 350 ms), and the `STATUS`
fields of request 4. Requests 1-3 are its first pieces, so nothing done for D
is thrown away. On this side: a Swope entry in `telio.h`, a host and port key
(with #38's `tcs` key for the telescope), and the [Removing
gframe](../../src/gcam/gframe/README.md#removing-gframe) steps.

## Decisions

| # | Question | Decided |
|---|---|---|
| D1 | where the conversion lives | **D**, gframe in gcamzwo; B later |
| D2 | containment | **one directory**, `src/gcam/gframe/`, plus the listed hooks |
| D3 | base | **v1.1.1**, the latest release, not `main` (the edstail work is not in it) |
| D4 | corrections arriving between frames | **summed**, sent every `gframe_period` 1.5 s, carried over |
| D5 | wire unit | **0.05"**, the TCS's; `gframe_unit` can say otherwise |
| D6 | port | **10001**, the converter's, so `Swope.ini` changes only its host |
| D7 | gcamzwo's TCSIS at Swope | **off** (`-t 0`): corrections go to the frames instead of `aeg` |
| D8 | mouse mode 3 | **through the frames** |
| D9 | relink after a restart | **`gframe_tcs.py --relink`** until the TCS polls its socket |

Open: who runs gcamzwo for operations (swope-obs or a box of its own); the
second probe's camera (each instance its own port, the TCS connects to one);
the du Pont's `GBOX` on the same socket when the du Pont wants it.

## Risks

1. **Cadence against actuation.** Summed corrections a pulse late can
   oscillate with a high gain: start at `sn 0.3`, 1-2 Hz; request 3 shortens the
   lag.
2. **A gcamzwo restart** leaves the TCS deaf until relinked (above).
3. **2.4" per frame** is 1.6"/s: enough to guide, too slow for an acquisition
   offset by mouse; large offsets stay with the Swope app.
4. **Anyone on the subnet** can read the frames, and could be the TCS's peer
   if `Guider_addr` points at it; the same as the converter today.

## Implementation (2026-11-09)

Branch `feature/gcam-gframe` from v1.1.1 (18235f3), the latest release, so that
v1.1.1 plus this branch is what a Swope build would be. `main` has the edstail
work on top of v1.1.1; the branch merges into it without conflicts (the
makefile's `gframe` lines sit where `main` did not change it), and the merged
tree builds with both modules and passes the bench.

| File | Change |
|---|---|
| `src/gcam/gframe/gframe.h`, `gframe.c` | the module (320 lines of code) |
| `src/gcam/gframe/gframe_test.c` | the bench |
| `src/gcam/gframe/gframe_tcs.py` | TCS stand-in, `--relink` |
| `src/gcam/gframe/README.md` | frame, keys, behaviour, hooks, removal |
| `src/gcam/guider.c`, `guider.h`, `zwogcam.c`, `makefile`, `.gitignore` | the hooks |
| `src/gcam/eds.c` | `eds_send()` returns at once with no EDS: it slept 70 ms three times a second inside the guide loop's mutex at `-t 0`. Not gframe's, but every Swope guider runs that way |
| `etc/ini/swope_gcam/gcam01.ini` | the Swope guider, first values |
| `docs/ZWO/zwogcam.html` | the keys and the `status` fields |

Checks:

| Check | Result |
|---|---|
| `make gframe_test`, macOS (clang, `-Wall -Wextra`) and Ubuntu 24.04 amd64 (gcc 13, the CI image) | 40 of 40: encoding table, ±5" sweep never writes 50/51, signs, carry-over, period, two clients, clear, 3 s drop, client limit, stop |
| gcamzwo builds, macOS and Linux | no new warnings |
| Rig: `zwo_emulator.py` → gcamzwo with `gcam01.ini` (host 127.0.0.1) → `gframe_tcs.py` | guiding at 1.46 Hz; small corrections summed into 0.15" frames; box 12 px off the star: a frame every 1.51 s, RA -0.20 to -0.40"; F1: nothing more sent; stand-in killed and restarted: `gframe=0`, then `gframe=1` within a second and frames again on F3; connects and drops in the message log |
| `--relink` against `swope/simulator/tcs_mock.py` | `GUIDE false`, `GUIDE true`: both `ok` |

Found on the way, not changed here: on macOS, `message()` writes into a
128-byte window line with `sprintf`, so a message longer than about 115
characters aborts the program (`__sprintf_chk`); a `GCAMZWOPATH` longer than
about 80 characters does it at start-up through the "mask not found" message.

Not done: anything on the TCS or the sky (steps above); the Swope app's guider
light and the manual (in `swope`); a release.
