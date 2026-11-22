# gcam: mouse modes 2 and 3 at Swope (offsets through the Swope app) — Plan

**Status:** plan, 2026-11-22; mode 3 joined the same night. Decisions P2 and
P7-P10 answered by William the same night, the rest built as proposed
([Decisions](#decisions)). Built on branches `feature/gcam-swope-offsets`
(zwo, worktree `zwo-offset`) and `feature/tcp-offsets` (swope, worktree
`swope-offset`), both from upstream main, and tested against the camera
emulator, `simulator/tcs_mock.py` and the app's Galil simulator: steps 1-4
of [Steps](#steps). Not yet on swope-obs or the sky.

Paths outside this repository are sibling checkouts:
- `zwo/` = this repository at v1.2 (7b77331). Line numbers are v1.2's.
- `swope/` = [swope](../../../swope), the Swope instrument app. Line numbers
  are upstream main's (5144801); v2.3.0-8bbd438 runs on swope-obs.
- `TCS/` = [lco-software/Swope-TCS](../../../lco-software/Swope-TCS) (df000ae).
  Nothing changes there.
- `ansible/` = [lco-ansible](../../../lco-ansible), the `gcam` role and the
  Swope inventory.

The G frames and the way gcamzwo got to Swope are in
`gcam-swope-guiding.md` (on `main`, zwo#44) and
[src/gcam/gframe/README.md](../../src/gcam/gframe/README.md). The
measurements quoted here are in `swope/docs/plans/guider_calibration.md` §8
(in that working tree, not committed yet).

## What this is

gcamzwo has three mouse modes (`mm`): a click in the image moves the guide box
(1), the guide probe (2) or the telescope (3), so that the star under the
mouse ends in the box. A plain click covers a tenth of the distance, a click
with the right button held the whole of it.

At Swope neither 2 nor 3 does that.

- **Mode 2 does nothing.** gcamzwo sends the probe offset as `gpaer<gnum> A E`
  to a Magellan TCSIS (`zwogcam.c:1150-1168`, `telio.c:361-372`). Swope has
  none, gcamzwo runs with `-t 0`, and with no TCSIS every telio call sleeps
  20 ms and returns success (`telio.c:101-127`). Nothing is sent and nothing
  on the screen says so.
- **Mode 3 moves the telescope part of the way, and not in a straight line.**
  It goes out as G frames, the guide loop's channel (`zwogcam.c:1169-1181`,
  `guider.c` `send_correction()`):
  - a frame carries 2.4" an axis at most, one frame goes every 1.5 s, and
    what is left is dropped 3 s after the click (`gframe.c:40-44`, `:204`).
    With guiding off a click moves 4.8" an axis at most;
  - each axis is cut at 2.4" by itself, so a move of 10" east and 2" north
    arrives as 4.8" east and 2" north;
  - the TCS ignores a frame while `GUIDING` is false or while it applies the
    last one, and anything of 0.1" or less.

Both become an offset that the Swope app applies: mode 2 to the guide probe,
mode 3 to the telescope through the TCS's `OFFSET` (William, 2026-11-22).

## What exists

### The probes

| | Where | What |
|---|---|---|
| The probes | `swope/src/Resources/SetupFiles/guider{1,2}.xml`, `galil{A,B,E,F}.xml` | each guider camera rides an X/Y stage (plus focus) on the Galil at `swope-imb`; 1.5875 mm per turn, 1600 steps and 1600 encoder counts per turn, 29"/mm: 0.029" per count, 100"/s |
| Who drives them | `swope/src/Swope/GuiderController.m` | the Swope app, and nothing else. It holds the encoder geometry, the soft limits and the collision check between the two probes (`isLegalPosition:` `:482`, `tooClose:` `:458`) |
| Offsets from code | `GuiderController -action_offset:` (`:610`), `TeleWindowController.m:1076-1086` | a T+G offset moves both probes by minus the telescope offset; the Offset button does the same for one probe. The move is `run_goto:` (`:1321`), which polls once a second until both axes stop, and skips a move under 2 steps (0.06") |
| The probe frame | `GuiderController -action_push:` (`:674`) | arcsec on the sky, +x east, +y north |

### The telescope

| | Where | What |
|---|---|---|
| Offsets in the app | `TeleWindowController -offset:mode:` (`:1068`), `TSFlowOffset` (`TelescopeFlows.m:87`), `TelescopeService -offsetRA:dec:pauseGuiding:after:reason:` (`TelescopeService.m:483`) | the checks (`TSOffsetRefusal`, `TelescopeRules.m:352`: tracking, no slew, slow gear, the sizes), `OFFSET`, the wait for the jog, then a check that the telescope moved: the whole offset from 10", its Dec part from 1". The outcome goes to the Messages window and the log |
| Its three modes | `TelescopeFlows.h:29-33` | telescope only; T+G (`GUIDING` off for 1.5 s first, the probes follow, `GUIDING` on); keypad, which is keyboard guiding: the position follows, the sums of the offsets stay, no `GUIDING` pause |
| One move at a time | `TeleWindowController -busy` (`:958`), `-moving:` (`:1236`) | a slew, an offset, a preset or keyboard guiding holds the Move guard; the offset buttons are greyed from the last `STATUS` by the same rule as the refusal |
| `OFFSET` in the TCS | `TCS/CommandHandler.cpp:555-590`, `TCS/Swope2.cpp:927-981` | `HA` and `Dec` in arcsec on the sky, +`HA` = east. RA is a jog at 15"/s for a computed time (45"/s above 60"), and an RA part under 1 arcsec of hour angle is not made (`:933`). Dec is a closed-loop move. An `OFFSET` that arrives while a guide correction is being applied is dropped (`:927`, `:976-981`). It zeroes the differential tracking rates (`CommandHandler.cpp:557-558`) |
| G frames in the TCS | `TCS/GuiderClient.cpp:190`, `TCS/Swope2.cpp:835-929` | acted on while `GUIDING` is true (the default, `TelescopeState.h:36`); RA a rate pulse with gain `[Guider_RA] kP`, Dec a closed-loop move |

### The two programs

| | Where | What |
|---|---|---|
| A way into the app | `swope/src/Swope/AppDelegate.m:366`, `:857` | a TCP command server on 51911, text lines in, one line out for each (`ok`, or `-E...`): `exptime`, `start`, `move_filter`, `focus_auto`... It was listening on swope-obs on 2026-11-22. The handler is given the sender's address. Release builds accept two connections a second and hold each answer 150 ms. The connection stays open until the client closes it |
| gcamzwo at Swope | `/usr/local/etc/gcam/swope-g2.ini` on swope-obs | `gnum 2`, `gmode 1`; `set_mm()` (`zwogcam.c:2589-2606`) allows modes 1, 2 and 3 there, and refuses -2. `gframe_port 10001`, and the TCS is connected to it |
| An interim fix for mode 3 | branch `fix/gframe-mouse-move`, worktree `zwo-gmove`, not committed | `gframe_move()`: a mouse move kept apart from the guide loop's corrections, sent to the end, both axes cut alike, 60" at most, F1 cancels. Bench 50/50. A trial build of it has run on swope-obs since 07:56 UT on 2026-11-22 (`/Users/Shared/gcamzwo-gmove/gcamzwo`); `/usr/local/bin/gcamzwo` is v1.2 |

gcamzwo and the Swope app run on the same Mac, swope-obs.

### Measured on 2026-11-22

| | |
|---|---|
| Camera geometry | `px` 49.6 ± 0.2 mas, `angle` -30.3, `parity` -1, from two offset runs. These are in the `.ini` on swope-obs, set by hand. The field is about 2 arcmin across |
| G-frame signs | +1, +1, in five closed-loop step tests. So `(da,de)` from `rotate()` is east and north |
| G frames, per 1" sent | Dec moves 0.65-0.95", whatever `kP` is. RA moves 1.0" with `[Guider_RA] kP=3.5`, which is in `/mnt/data/TCS/Swope.ini` since the TCS restart at 07:49:46 UT (0.7" at 3.0, 1.7" at 5.0, a third at the default 1.0) |
| `OFFSET`, RA | +`HA` moves the telescope east, checked on the sky. It lands -0.2 ± 1.3" from the request, 2.5" at worst |
| `OFFSET`, Dec | lost or cut short in 4 of 10 offsets: -10.1" of -20"; +0.9" of +10.2", -0.2" of -3.4" and +0.2" of -3.2", the last three with an RA part |
| `STATUS` | RA jitters ±0.8" between reads and jumps ±45" in about 1 read in 130; Dec is clean |

## Options

Who moves the probe (mode 2):

| | What | gcamzwo | Swope app | Against it |
|---|---|---|---|---|
| **A** | **gcamzwo asks the app, on its TCP command server** | **one small module, a hook in mode 2** | **one command, ~60 lines** | `swope/docs/plans/telescope_control.md` keeps new commands out of that server |
| B | the same through the app's WebSocket service | the same, plus a WebSocket client in C | the service does not exist yet | waits for the WebSocket migration; a WebSocket client in gcamzwo is more code than the feature |
| C | gcamzwo drives the Galil | the app's geometry, limits, collision check and stepper loop, written again | none | two masters on one stage; the app's probe position goes stale |
| D | the app serves a TCSIS subset, gcamzwo runs with `-t` | none | `gpaer`, and an answer to everything else gcamzwo asks a TCSIS | gcamzwo then polls it for twenty status values and takes its FITS telescope block from it |

How mode 3 moves the telescope:

| | What | For it | Against it |
|---|---|---|---|
| **T1** | **`OFFSET`, through the app** | **one move of any size, in a straight line; the app's checks, its Move guard and its record of the position** | RA lands ±1.3" and cannot make less than 1"; the TCS sometimes drops the Dec part; the differential rates are zeroed |
| T2 | G frames sent to the end (`gframe_move()`, the interim fix) | built and on the sky; keeps the differential rates; needs nothing in the app | a guide channel carrying a slew: 2.4" every 1.5 s, so 19 s for 30"; nothing checks that it arrived; the app does not know the telescope moved |
| T3 | `OFFSET` from gcamzwo to the TCS | no app change | a second client moving the telescope past the app's Move guard and its position |

**A and T1.** The app stays the only thing that moves a probe or offsets the
telescope, and each command is one of its own moves behind a text line. When
the WebSocket service arrives the commands move there with the others and
gcamzwo's module changes its transport.

## Design

### The commands, in the Swope app

```
guider_offset <n> <dx> <dy>      n = 1|2; arcsec on the sky, +east, +north
    ok                           the probe has moved
    -Eguider 2 not initialized
    -Eguider 2 is moving
    -Eillegal position
    -Eoffset too large

tele_offset <dx> <dy>            arcsec on the sky, +east, +north
    ok                           the telescope has moved
    -Ea move is running
    -Ethe telescope is not tracking         (the reason the buttons are grey)
    -Eoffset too large
    -Ethe telescope moved only 0.2" of 3.2" in Dec      (the flow's outcome)

guider_pos <n>
    ok <x> <y> <moving>          arcsec, 0|1
```

An answer is one line, and an error is `-E` and its text with no space
between, as in `-Efailed`. Also `-Eguider must be 1 or 2`, `-Eusage: ...`,
and `-Eguider N not running` / `-Etelescope window not running` when that
window's controller does not exist.

`AppDelegate -tcpip_handler:` parses the line (its `sscanf` already takes a
command and three parameters) and calls one new method in each controller,
which returns the answer. No check opens a dialog. The command and its answer
go to the app's log, as for every TCP command.

**The answer comes when the move has ended**, and it is the move's outcome.
The server runs each connection on a thread of its own (`Tcpip.m`,
`run_server`), so the wait holds up no other client.

**An exception to `telescope_control.md`** (§1, §6.7, §6.9 keep telescope
functions out of this server): `tele_offset` is the one telescope command
there, until the WebSocket service exists. The app's pull request says so in
those sections. Any host that reaches the port may send it, as for `start`.

**`guider_offset`**, in a new `GuiderController -tcpOffsetX:Y:`:
- The checks, in this order: `check:NO Other:YES` (this guider initialised,
  and the other one, for the collision check); no move running on this guider
  (`hunting` on either axis, or a keypad push); |dx| and |dy| at most 300";
  `isLegalPosition:` on the target. The last one is what the Goto button
  checks and the called path of `action_offset:` does not.
- The move is `run_goto:`, on the handler's thread, as a T+G offset runs it.
  The answer goes back when both axes have stopped.
- A command during a move is refused, because the target is the last known
  position plus the offset.

**`tele_offset`**, in a new `TeleWindowController -tcpOffsetDx:Dy:`:
- The checks: |dx| and |dy| at most 300"; on the main thread, the Move
  guard is free (`-busy`) and taken at once; then the reasons that grey the
  offset buttons (tracking, no slew, slow gear) from a fresh `STATUS`
  (`TelescopeService -checkOffsetRA:dec:`). The poller's last `STATUS` can
  still show the previous offset running (up to 1 s old, 10 s with the
  window hidden), which refused a click right after an `ok` in a test.
- Then it takes the Move guard and runs the offset flow on the handler's
  thread, in keypad mode: the position in the window follows, the sums stay,
  `GUIDING` is not touched. A mode-3 click is a pointing correction, as
  keyboard guiding is.
- The flow checks again from a fresh `STATUS`, sends `OFFSET`, waits for the
  jog and checks that the telescope moved. Its outcome goes to the Messages
  window and the log as for the buttons, and is the answer: `ok`, or the
  reason after `-E`.
- A second command while the first runs is refused, never queued. A click is
  measured on the image, so an offset queued behind another would be applied
  twice.

`guider_pos` is read-only. The tests use it, and gcamzwo does not.

### gcamzwo: `src/gcam/goffset/`

A directory of its own with standard headers only, like `gframe/`:

| File | What |
|---|---|
| `goffset.h`, `goffset.c` | `goffset_ini()`, `goffset_enabled()`, `goffset_probe(gnum,a,e,msg,len)`, `goffset_tele(a,e,msg,len)`: connect, send the line, read one line, close. They return when the move has ended: 2 s to connect, 120 s for the answer (a `tele_offset` the TCS does not finish takes the jog plus 60 s). `msg` is the app's text after `-E` |
| `goffset_test.c` | the bench: the lines it writes, the signs, `ok`, `-E`, no listener, a listener that never answers |
| `goffset_app.py` | a stand-in for the app's command port: the checks above on a pretend probe and a pretend telescope, each move lasting a given time, printing every one |
| `README.md` | the commands, the keys, the hooks, how to remove it |

Keys in the `.ini`, read at start-up:

| Key | Default | Meaning |
|---|---|---|
| `offset_port` | 0 | the app's command port; 0 = off, modes 2 and 3 are v1.2's |
| `offset_host` | 127.0.0.1 | where the app runs |
| `offset_wait` | 120 | s to wait for the app's answer |
| `probe_xsign`, `probe_ysign` | 1 | -1 flips that axis in `guider_offset` |

Hooks outside the directory:

| File | Hook |
|---|---|
| `zwogcam.c`, `handle_msmode()` case 2 | with `goffset_enabled()` and mode +2: `qltool_cursor_off()`, `rotate()`, then `goffset_probe(g->gnum,da,de,...)` in place of `telio_open()`/`telio_gpaer()` |
| the same place | with neither `offset_port` nor a TCSIS: a warning, "mm 2: no probe link", in place of the silent return |
| `zwogcam.c`, `handle_msmode()` case 3 | with `goffset_enabled()`: the table below, in place of `send_correction()` |
| `zwogcam.c`, both cases | the hold of [One move at a time](#one-move-at-a-time); the call to the app runs on a thread of its own, which shows the answer with `message()` as gframe's thread does (`gframe_log()`, `:1854`) |
| `gframe/gframe.c`, `.h` | `gframe_fits(x,y)`: the TCS is connected and both parts are within one frame (`gframe_max`, 2.4") |
| `zwogcam.c`, `read_inifile()` | `else if (goffset_ini(key,val)) {}` next to gframe's |
| `makefile`, `.gitignore` | `goffset.o`, `goffset_test` |

A mode-3 click with `offset_port` set, first match:

| When | Where it goes |
|---|---|
| a move from an earlier click has not ended | nowhere: "mm 3: the last move has not ended" |
| gcamzwo is guiding (F3) | nowhere: "mm 3: guiding is on". The TCS drops an `OFFSET` that arrives during a correction, and the loop would take the star back |
| both parts fit one G frame, and the TCS is connected to gframe | one G frame, as in v1.2: it arrives whole, and `OFFSET` cannot make an RA part under 1" |
| an RA part between 0.1" and 1" with no G frame possible (the TCS not on gframe) | nowhere: "mm 3: ... in RA needs G frames: the TCS is not connected". Step 4 showed `OFFSET` skipping such an RA part while the app booked it in its position |
| anything else | `tele_offset da de` to the app |

A refusal or a missing answer is shown in the message window and nothing
else is sent: no second route for the same click. The guider number in
`guider_offset` is gcamzwo's `gnum`.

### One move at a time

A second click before the first move has ended, a double click for one, is
measured on an image from before the move. Sent, it would move the telescope
or the probe twice. Two things stop it:

1. **gcamzwo holds modes 2 and 3 from an accepted click until its move is
   seen.** The click starts a thread that sends the command and waits for
   the app's answer, so the image and the keys keep running. Until that
   answer, and then until a frame exposed after it is on the screen (with a
   rolling average `av`, `1+2*av` of them: the old image is under a fifth
   then), a click in mode 2 or 3 is refused with the message above. Clicks
   are not queued. Mode 1 is not held.
   - The G-frame route has no answer: its hold runs for one frame period and
     the TCS's pulse (1.5 s and 1 s), then the fresh frame.
   - No answer in `offset_wait` (120 s) ends the hold with "no answer from
     the Swope app". A click that could not connect holds nothing.
   - Without frames the hold ends 10 s after the frames it waited for.
2. **The app refuses** a `tele_offset` while its Move guard is held and a
   `guider_offset` while that probe moves. That also covers its own buttons
   and any other client.

The TCS is no protection: it refuses an `OFFSET` only until the one before
has started (`TCS/CommandHandler.cpp:560-562`, `TCS/Swope2.cpp:973-974`), and
one sent during a jog starts the jog's timer again.

### Frames and signs

gcamzwo computes one vector for modes 2 and 3: `(da,de)`, the star's offset
from the box, east and north. The guide loop sends the same vector, and five
step tests closed with the G-frame signs at +1.

- **Mode 3:** `tele_offset +da +de`. +`HA` in `OFFSET` moves the telescope
  east, so the telescope moves to the star. No sign key: both halves are
  measured.
- **Mode 2:** `guider_offset <gnum> +da +de`. Moving a probe by a vector on
  the sky shifts the field in its camera as moving the telescope by the same
  vector does, and the app's probe frame is +east, +north (a T+G offset moves
  the probes by minus the telescope's). This half is not measured, hence
  `probe_xsign` and `probe_ysign`; a -1 there means the stage's calibration
  in the app is wrong, and belongs in `guider2.xml` in the end.

### What does not change

- Mode -2 (probe and telescope together) and the `gm ... p` guide modes that
  send `gpaer3`: not selectable in `gmode 1`. At Swope that move is the app's
  T+G offset.
- The guide loop, its G frames and the TCS.
- Magellan, and Swope without `offset_port`: the code path is v1.2's.

## Steps

1. **App commands.** Branch from `swope` upstream main (the working tree is
   on `focus-model` with uncommitted changes: not this work). In the
   simulator build, with `simulator/tcs_mock.py` as the TCS. Send by hand
   with `nc -w 2 localhost 51911`: the server keeps the connection, so a
   plain `echo ... | nc` does not return.
   - `guider_offset`: a legal offset moves Guider2 and answers `ok`; a second
     one during the move, a target past a soft limit, a target onto the other
     probe, an uninitialised guider and 301" each answer their `-E`;
     `guider_pos` follows the window.
   - `tele_offset`: the mock logs `OFFSET` with the same signs and no
     `GUIDING`; `ok` comes when the jog has ended; the window's position
     follows and the sums do not; a second one during the jog, tracking off
     and 301" answer their `-E`; with the mock's `dec_stuck` fault the answer
     and the Messages window say how far Dec moved.
   - `telescope_control.md` §1 and §6.9, and the manual: the three commands,
     and why two of them are an exception.
2. **goffset and its bench.** `make goffset_test`, on macOS and in the Linux
   CI image.
3. **Hooks.** gcamzwo against the camera emulator (the `run-gcamzwo-sim`
   rig) with `offset_port` at `goffset_app.py`: `mm 2` and a click, `mm 3`
   and a click of 20", each with its line and answer; a click of 1" with a
   gframe client connected: a G frame and no line; F3 on and `mm 3`: the
   refusal; the stand-in stopped: the error in the message window; no
   `offset_port`: v1.2's behaviour, and the mode-2 warning. A double click
   in each mode, with the stand-in's moves lasting 3 s: one line sent, the
   second click refused, the image running meanwhile, and a click accepted
   again after the answer and a fresh frame.
4. **Both together on one Mac.** The simulator app and the mock TCS in place
   of the stand-in: a click moves Guider2 in the app's window, and the
   telescope in the mock's log.
5. **swope-obs by day**, the real Galil: `guider_offset 2 5 0` and back, read
   against the encoders in the Guider2 window. `tele_offset` needs the
   telescope tracking, so it waits for the night unless the day crew agrees.
   Then the releases and [Deployment](#deployment).
6. Pull requests to `carnegie-observatories/swope` and
   `carnegie-observatories/zwo`.

## To the sky

Guiding off, clicks with the right button held (the whole distance).

1. `mm 3`, a star about 20" east or west of the box: it lands in or beside
   the box in one move, and the Telescope window's position follows with the
   sums unchanged. Away from the box: stop; the sign in `tele_offset` is
   wrong.
2. The same north or south, then a diagonal of about 30": a straight move.
   Note how far from the box each one lands, and how often the app reports a
   short Dec move.
3. A star 1-2" from the box: the click goes out as a G frame ("Guider X..
   Y.." in the TCS log) and centres it.
4. `mm 2`, a star east or west of the box: it moves into the box, and the
   Guider2 window shows the same offset. Away from the box on an axis:
   `probe_xsign` or `probe_ysign` -1.
5. The same north or south, then 30" in each axis: the star lands in the box
   to a pixel or two. An error here is the stage's calibration in the app
   (29"/mm, `theta 0`); the camera's is measured.
6. F3 on, a 2" probe step with `mm 2`: the telescope follows the star back
   (G frames in the TCS log).
7. F3 on, `mm 3`: the refusal.
8. A double click in `mm 3`, then in `mm 2`: one move each, and the message
   for the second click.
9. A T+G offset from the app while gcamzwo guides: as before.

## Deployment

- **App:** a release through its CI; swope-obs and hen-drp take it from the
  auto-updater. "TCP server" stays ticked.
- **gcamzwo:** the next release; in `ansible/`, the version, URL and checksum
  for swope-obs, and `offset_port: 51911` under `swope2` in
  `roles/gcam/defaults/main.yml`.
- **Before the role runs again:** `swope2` still has `px 134`, `angle 0`,
  `parity 1`, and the file on swope-obs has `49.6`, `-30.3`, `-1` by hand.
  The role would put the old values back. The calibration's values go into
  `ansible/` first (`guider_calibration.md` §7).
- **Until then** mode 3 at Swope is the trial build's G-frame move, or
  v1.2's when `/usr/local/bin/gcamzwo` is started again.

## Decisions

| # | Question | |
|---|---|---|
| P1 | who applies the offsets of modes 2 and 3 | the Swope app, both. **Decided** (William, 2026-11-22) |
| P2 | the channel | the app's TCP command server, 51911, open as the other commands are, until the WebSocket service exists: an exception to `telescope_control.md` §1, §6.7, §6.9. **Decided** (William, 2026-11-22: no loopback restriction, keep it simple) |
| P3 | when the app answers | when the move has ended, with its outcome; gcamzwo waits on a thread of its own. **Proposed** |
| P4 | largest offset per command | 300", probe and telescope. **Proposed** (the camera shows about 2 arcmin; T+G allows 600") |
| P5 | where the code lives in gcamzwo | `src/gcam/goffset/`, `offset_port`, and the listed hooks. **Proposed** |
| P6 | base | zwo v1.2; swope upstream main |
| P7 | the telescope offset's mode in the app | keypad: the position follows, the sums stay. **Decided** (William, 2026-11-22) |
| P8 | small mode-3 moves | one G frame when the move fits one (2.4" an axis). **Decided** (William, 2026-11-22). `OFFSET` makes no RA part under 1" and lands ±1.3" |
| P9 | mode 3 while gcamzwo guides | refused, with a message. **Decided** (William, 2026-11-22) |
| P10 | the G-frame mouse move (`fix/gframe-mouse-move`) | not merged. **Decided** (William, 2026-11-22) |
| P11 | a click before the last move has ended (a double click) | refused in gcamzwo, modes 2 and 3, until the move has ended and a frame from after it is shown; refused by the app as well. **Asked for** (William, 2026-11-22); the way to do it **proposed** |

Guider 1, when it gets a ZWO camera: `guider_offset` takes the guider number,
and each gcamzwo sends its own `gnum`.

## Risks

1. **The TCS drops the Dec part of some offsets** (4 of 10 on 2026-11-22;
   F7 in `telescope_control.md`). Mode-3 clicks are mostly diagonal. The app
   reports it from 1", and gcamzwo shows that answer; the star stops short
   of the box, and a second click, measured from the new image, makes the
   rest. The fix belongs in the TCS.
2. **`OFFSET` lands ±1.3" in RA.** The star ends in or beside the box, not on
   its centre. A small click (P8) or F3 does the rest.
3. **`OFFSET` zeroes the differential tracking rates.** A mode-3 click on a
   non-sidereal target loses them, as an offset from the app's buttons does.
   G frames do not.
4. **A command that moves the telescope on a port without authentication**,
   as `start` and `move_filter` are today. The app's own checks, 300" at
   most, one move at a time. The network firewall keeps the port to the
   Swope subnet (from 10.8.22.x it did not answer on 2026-11-22).
5. **A wrong probe sign or stage angle walks the star out of the field.**
   Guiding off for the first clicks, 300" at most; the soft limits and the
   collision check hold in any case.
6. **A probe move while guiding** leaves the star outside the box until the
   telescope catches up, 2.4" a frame. A large one loses the star. Mode 2 is
   for guiding off, or small steps.
7. **Three sources of moves for each device** (the app's window, T+G or the
   keypad, gcamzwo): one at a time, the others refused as moving
   ([One move at a time](#one-move-at-a-time)).
8. **App closed, or its server off:** the click shows "no answer from the
   Swope app" and nothing moves.
9. **Keyboard guiding is not behind the Move guard.** `action_keypad` does
   not check `-busy`, so a key press during a `tele_offset` sends a second
   `OFFSET`, which the TCS takes (its `go_offset` clears as the jog starts):
   the first offset can be cut short. The same gap is there today between
   the keypad and the offset buttons. The other way round is refused. Not
   fixed here.
10. **A T+G offset during a `guider_offset`** runs a second goto on the same
   probe: the T+G path of `action_offset:` checks nothing. The other way
   round is refused. Not fixed here.
11. **The app's position after an offset that fails or falls short.** It
   keeps the old position after any failed offset, although an RA part may
   have moved (a lost Dec part), and books an RA part the TCS skipped (under
   1 HA-arcsec; keyboard guiding's 0.2" steps too). The window's buttons do
   the same today. Not fixed here.
12. **The app's check reads `STATUS` once,** and RA there jumps 45" in about 1
   read in 130. A rare offset is reported short when it was not, or the
   reverse. gcamzwo shows that outcome and does not act on it.

## Not in this plan

- **The TCS:** the dropped Dec parts, the RA jog's ±1.3" and its 1" floor,
  and the G frames' Dec gain (0.65-0.95).
- **An offset the app checks and completes** (the median of several `STATUS`
  reads, then the rest sent again), as `swope/scripts/guider_calib.py` does.
  It would serve the buttons and `tele_offset` alike.
- The app's SkyMap drawing the ZWO camera's field for guider 2.
