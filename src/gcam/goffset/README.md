# goffset: mouse modes 2 and 3 through the Swope app

At Swope the guide probes and the telescope offsets belong to the Swope
instrument app. With `offset_port` in the `.ini`, gcamzwo sends a click in
mouse mode 2 (the probe) or 3 (the telescope) to the app's TCP command port as
one text line, and the app makes the move. Without it, both modes are what
they were in v1.2: mode 2 goes to a Magellan TCSIS as `gpaer`, mode 3 as `aeg`
or G frames.

Why this way, the measurements behind it and the decisions:
[docs/plans/gcam-swope-probe.md](../../../docs/plans/gcam-swope-probe.md).

Everything goffset adds lives in this directory. Outside it there are only
the hooks listed under [In gcamzwo](#in-gcamzwo), so the day the app takes
these commands another way (its WebSocket service), this directory changes
its transport or is deleted and the hooks reverted
([Removing goffset](#removing-goffset)).

| File | What |
|---|---|
| `goffset.h`, `goffset.c` | the module: standard headers only (POSIX sockets, `poll`, pthreads), nothing from gcamzwo |
| `goffset_test.c` | the bench: `make goffset_test` in `src/gcam`, then `./goffset_test` |
| `goffset_app.py` | a stand-in for the app's command port, with a pretend probe and telescope |

## The commands

```
guider_offset <n> <dx> <dy>     n = gnum; arcsec on the sky, +east, +north
tele_offset <dx> <dy>           arcsec on the sky, +east, +north

    ok                          the move has ended
    -E<why>                     refused, or the move went wrong
```

`(dx,dy)` is the vector gcamzwo computes for every mode-2 and mode-3 click,
the star's offset from the box on the sky (`qltool_cursor_off()`, `rotate()`),
the same vector the guide loop sends. Moving the telescope or the probe by it
brings the star into the box.

- One connection per click: connect (2 s at most), send the line with `\n`,
  read the answer, close.
- The app answers when the move has ended, which can take seconds, so
  goffset waits for the first byte up to `offset_wait` s. A `tele_offset`
  the TCS does not finish takes the jog and 60 s (`OFFSET_WAIT` in the
  app): about 100 s at worst, hence 120 s.
- The answer ends with `\n`, and the app keeps the connection open. Some of
  the app's older answers have no line end, so goffset also takes what has
  arrived once the port has been quiet for 200 ms, or when the app closes.
- An error is `-E` and its text with no space between, as for every command
  on that port (`-Efailed`). goffset trims a leading `:` or space.

| Return | When | The click |
|---|---|---|
| `GOFFSET_OK` | `ok` | moved |
| `GOFFSET_REFUSED` | `-E...`: the app's reason in `msg` | shown in the window and the log |
| `GOFFSET_TIMEOUT` | sent, no answer in `offset_wait` s, or closed without one | shown; the app may still be moving |
| `GOFFSET_BADREPLY` | sent, neither `ok` nor `-E` | shown |
| `GOFFSET_NOLINK` | no connection: nothing sent | "no answer from the Swope app" |
| `GOFFSET_OFF` | `offset_port` is 0 | (not called) |

## Keys

| Key | Default | Meaning |
|---|---|---|
| `offset_port` | 0 | the app's command port (51911 at Swope); 0 = off, modes 2 and 3 as in v1.2 |
| `offset_host` | 127.0.0.1 | where the app runs |
| `offset_wait` | 120 | s to wait for the answer (0.5 to 600) |
| `probe_xsign`, `probe_ysign` | 1 | -1 flips that axis in `guider_offset` only |

`tele_offset` has no sign keys: both halves are measured (the G-frame signs
in five step tests, `OFFSET`'s +`HA` = east on the sky, 2026-11-22).
`guider_offset`'s are not, hence the keys; a -1 there means the stage's
calibration in the app is wrong.

## A click, with `offset_port` set

Mode 2 (the probe): `guider_offset <gnum> da de`.

Mode 3 (the telescope), first match:

| When | Where it goes |
|---|---|
| a move from an earlier click has not been seen yet | nowhere: "mm 3: the last move has not ended", or "no image since the last move yet" |
| gcamzwo is guiding and sending (F3, F5) | nowhere: "mm 3: guiding is on (F1 first)". The TCS drops an `OFFSET` that arrives during a correction |
| the TCS is connected to gframe and one frame carries both parts (`gframe_max`, 2.4") | one G frame, as v1.2 would: it arrives whole, and `OFFSET` makes no RA part under 1" |
| an RA part between 0.1" and 1", and no G frame possible | nowhere: "mm 3: -0.5" in RA needs G frames: the TCS is not connected". `OFFSET` would skip the RA part while the app booked it as made (found in the test with the app, 2026-11-22) |
| anything else | `tele_offset da de` |

Mode 2 is allowed while guiding: the telescope follows the star back.

### One move at a time

A click measures the star on the image. A second click before the image
shows the first move, a double click for one, would move it twice. So from a
click that sends something, modes 2 and 3 refuse clicks (they are not queued)
until:

1. the move has ended: the app's answer, or for a G frame `gframe_period` and
   the TCS's 1 s pulse; and
2. new frames exposed after that are on the screen: one, or `1+2*av` with a
   rolling average (`av`), after which the old image is under a fifth of it.
   A frame counts when its display time minus the exposure time minus 0.7 s
   (readout, transfer, the display's poll) is after the end of the move.

The hold ends anyway `frames*(exptime+0.7)+10` s after the move, if no frames
come. A click that reached nothing (`GOFFSET_NOLINK`) holds nothing.

The app refuses a second command itself while it moves, which also covers its
own buttons and other clients. The TCS is no guard: it refuses an `OFFSET`
only until the one before has started.

The call waits on a thread of its own, so the image and the keys keep running
while the app moves.

## Trying it without the app

```
# the stand-in, moves of 3 s
python3 src/gcam/goffset/goffset_app.py --port 51991 --move-s 3

# in the .ini
offset_port 51991
```

Then `mm 3` (or `mm 2`) and a Control-click in the image. The stand-in prints
each line and answer. `--dec-short`, `--no-track`, `--refuse TEXT`,
`--silent` and `--close` make it answer as the app does when things go wrong.

Tested this way on 2026-11-22 (gcamzwo on macOS against `zwo_emulator.py`,
clicks sent to the image window with `XSendEvent`): a double click in mode 3
sends one `tele_offset` and refuses the second; a click 0.3 s after the
answer is refused until a new frame; mode 2 sends `guider_offset <gnum>`; a
tenth-distance click goes as one G frame with `gframe_tcs.py` connected;
guiding refuses mode 3 and not mode 2; the app's `-E` reason, its absence
and its silence (`offset_wait 2`) each show in the window and the log;
without `offset_port`, mode 2 warns and mode 3 is v1.2's. The same day with
the app itself (its test build against `tcs_mock.py` and its Galil
simulator): mode 3 reached `OFFSET` with `HA`=dx, `Dec`=dy and the window's
position followed with the sums unchanged; mode 2 moved probe 2 by the
click; double clicks moved once; tracking off and a lost Dec part came back
as the app's reasons.

## In gcamzwo

The whole of goffset outside this directory:

| File | Hook |
|---|---|
| `zwogcam.c` | `#include`; before `handle_msmode()`: the one-move-at-a-time hold (`move_hold()`, `move_held()`, `move_frame()`), `move_say()` (each message whole in the log and cut to the window's 128-byte line, which `message()` would overrun and abort on, zwo#44 "Found, not fixed"), `run_move()` (the thread that waits for the app) and `app_move()` (the routing table above); `handle_msmode()` modes +2 and 3 call `app_move()` when `goffset_enabled()`; without a TCSIS (`-t 0`), a mode-2 click with no `offset_port`, and any mode -2 click, warn "no probe link" instead of doing nothing; `run_display()` calls `move_frame()` for each frame shown; `read_inifile()` passes the keys to `goffset_ini()` |
| `gframe/gframe.c`, `.h` | `gframe_fits()` and `gframe_period()`, for the small-move row |
| `makefile` | `goffset.o` in `Ogui`, its rule, `goffset_test`, `clean` |
| `.gitignore` | `src/gcam/goffset_test` |

## Removing goffset

1. `git rm -r src/gcam/goffset`.
2. Revert the hooks above. `gframe_fits()` and `gframe_period()` can stay.
3. In the Swope `.ini`, drop the `offset_*` and `probe_*sign` lines.

Nothing else in gcamzwo knows goffset exists.
