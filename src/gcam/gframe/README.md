# gframe: guide corrections as G frames for the Swope TCS

The Swope TCS takes guide corrections as the 7-byte frames the DOS guider PC
writes on its serial line, `G xx yy CR LF`, through a serial-to-Ethernet
converter at 10.8.80.54:10001 that the TCS connects to. gcamzwo normally sends
its corrections to a Magellan TCSIS as `aeg A E`; Swope has no TCSIS. With
`gframe_port` in the `.ini`, gcamzwo serves those frames itself on that TCP
port, and the TCS is pointed at the gcamzwo host instead of the converter.
Nothing changes in the TCS but two lines of its `Swope.ini`.

Why this way and not a TCSIS in the TCS, the steps to the sky and what is
left: [docs/plans/gcam-swope-guiding.md](../../../docs/plans/gcam-swope-guiding.md).

Everything gframe adds lives in this directory. Outside it there are only the
hooks listed under [In gcamzwo](#in-gcamzwo), so the day the Swope TCS takes
corrections another way, this directory is deleted and the hooks reverted
([Removing gframe](#removing-gframe)).

| File | What |
|---|---|
| `gframe.h`, `gframe.c` | the module: standard headers only (POSIX sockets, `poll`, pthreads), nothing from gcamzwo |
| `gframe_test.c` | the bench: `make gframe_test` in `src/gcam`, then `./gframe_test` |
| `gframe_tcs.py` | a stand-in for the Swope TCS's guider input, and `--relink` for the real TCS |

## The frame

```
'G' x1 x0 y1 y0 CR LF          7 bytes, ASCII, no checksum, no reply

xx, yy   two decimal digits each, 00..99
         00..49  ->  0..+49
         51..99  -> -49..-1    (value - 100)
         X is the RA correction, Y the Dec correction
```

| Who | Where | What it does with `xx yy` |
|---|---|---|
| DOS guider PC at Swope (writer today) | `GCOPTV1.J1/GCTCS.F:99-125` | writes hundredths of an arcsec, clamped to -49..+50 |
| Swope TCS (reader) | `Swope-TCS/GuiderClient.cpp:70-134` | drops bytes <= 12 (so LF, not CR); ignores a frame with -49 or +50 on either axis; 1 count = **0.05"** |
| du Pont TCS (reader, for reference) | `c100-tcs/c100/src/guider.cpp:105-127` | the same frame from a tty; negates both axes |

So the TCS reads the DOS PC's counts five times larger than written; its gains
were tuned around that. gframe writes in the unit the TCS reads
(`gframe_unit 0.05`), so a frame means what the TCS applies.

## What gframe sends

- Every correction gcamzwo would send as `aeg` (the guide loops, and mouse
  mode 3, "star at mouse to box") is added to a pending pair instead, in
  arcsec on the axes `rotate()` gives (`angle`, `parity`).
- At most one frame per `gframe_period` goes to every connected client. It
  carries `round(pending / gframe_unit)` counts per axis, at most
  `gframe_max` (48 counts, 2.4", whichever is less: -49 and +50 are never
  written), and what it carries is subtracted from the pending pair. A 3"
  correction goes out as 2.4" now and 0.6" one period later.
- An axis whose frame would carry `gframe_min` or less (the TCS's
  `Guider_deadband`, 0.1": the TCS ignores `<=`) is written as 0 and stays
  pending until more comes. A frame with both axes 0 is not sent.
- The pending pair is dropped on F1 (`fone`), after 3 s without a correction
  (the star was lost; the TCS forgets its own after 3 s too), and whenever no
  client is connected. It is held to four frames' worth.
- Clients: up to four (the TCS, and a listener or two such as
  `gframe_tcs.py`); a fifth is closed at once. All get the same frames.
  Anything a client sends is read and ignored. A client that cannot take a
  frame, closes, or stops answering TCP keepalives (10 s idle, then 3 probes
  5 s apart) is dropped. Connects and disconnects go to the message window
  and log.
- With `gframe_port` set, the corrections go to the frames **instead of**
  `aeg`; with `-t 0` (the default, and the only sense at Swope) nothing goes
  to a TCSIS anyway.

## Keys

| Key | Default | Meaning |
|---|---|---|
| `gframe_port` | 0 | TCP port to serve; 0 = off. Read at start-up; a `dspi` reload does not move it |
| `gframe_period` | 1.5 | seconds between frames, at least (0.2..60). Above the TCS's 950 ms RA pulse plus its 100 ms loop, so a frame never lands while the TCS is still applying the last one |
| `gframe_unit` | 0.05 | arcsec per count, as the TCS reads it |
| `gframe_max` | 2.4 | arcsec per frame and axis (capped at 48 counts) |
| `gframe_min` | 0.1 | arcsec: an axis carrying this or less waits |
| `gframe_xsign`, `gframe_ysign` | 1 | -1 flips that axis on the wire only. For a TCS sign found on the sky; the camera's own orientation belongs in `angle` and `parity` |

`status` on the command port gains `gframe=<clients> gpend=<x>,<y>
gsent=<frames> gdrop=<n>` while `gframe_port` is set; `gdrop` counts the
times the pending pair was dropped for want of a client or after 3 s
without a correction (F1 empties it uncounted).

## The Swope TCS side

In the Pi's `Swope.ini` (`[Config]`):

```
Guider_addr=<the gcamzwo host>
Guider_port=10001
```

and restart the TCS. Then, from the TCS's own code:

- It connects when it initialises (power on with the key) and, while that
  fails, retries every ~1.1 s. Start gcamzwo before or after; both work.
- **It does not notice a connection it had being dropped**
  (`GuiderClient::IsConnected()` is never polled; `state.guider_connected`
  stays true). After gcamzwo is restarted the TCS stays deaf until `GUIDE`
  false then true arrives on its JSON port, or the key is cycled. That is
  STI-831. From any host the TCS accepts:

  ```
  ./gframe_tcs.py --relink 10.8.80.53
  ```

  `GUIDE` also zeroes the differential tracking rates (`TRACKRATE`): set them
  again after a relink while tracking a moving target.
- It applies a frame per axis above `Guider_deadband`: RA as a tracking-rate
  pulse of `Pulse_ms` (950 ms) through a PID, Dec as a move of the slow Dec
  motor, closed loop unless `GUIDER_OPENLOOP`. An axis that arrives while the
  previous correction on it still runs is ignored, and `GUIDING false` (the
  Swope app's T+G offsets) ignores every frame until `GUIDING true`.
- Three small TCS changes would help, none needed for gframe: poll
  `guider.IsConnected()` in the 1.1 s loop (reconnects by itself, closes
  STI-831); TCP keepalive on the guider socket; add frames that arrive during
  a correction instead of dropping them (then `gframe_period` can go down).

## Trying it without the telescope

In `src/gcam`, with `sim.ini` a copy of `etc/ini/swope_gcam/gcam01.ini` whose
`host` is `127.0.0.1`:

```
make gframe_test && ./gframe_test               # the module alone, 40 checks

python3 -u ../py/zwo_emulator.py -p 52311 &     # a camera
./gcamzwo -f $PWD/sim.ini &                     # serves G frames on 10001
./gframe/gframe_tcs.py --port 10001             # what the TCS would do
nc localhost 52201                              # then type fthr (F3), fone (F1)
```

`gframe_tcs.py` parses with the TCS's rules and prints each frame with what
the TCS would apply ("RA -0.25"", "below the dead band"); it reconnects every
second, or, with `--as-today`, stops at the first drop as the TCS does.

## In gcamzwo

The whole of gframe outside this directory:

| File | Hook |
|---|---|
| `guider.c` | `#include "gframe/gframe.h"`; `send_correction()`: `gframe_add()` when `gframe_enabled()`, else `telio_aeg()`; the three guide loops call it instead of `telio_aeg()` |
| `guider.h` | `send_correction()`'s prototype |
| `zwogcam.c` | `#include`; `handle_msmode()` mode 3 calls `send_correction()`; `gframe_log()` (messages to the window and log); `run_init()` calls `gframe_start()`; `read_inifile()` passes `gframe_*` keys to `gframe_ini()`; `fone` calls `gframe_clear()`; `status` appends `gframe_status()` |
| `makefile` | `gframe.o` in `Ogui`, its rule, `gframe_test`, `clean` |
| `.gitignore` | `src/gcam/gframe_test` |

## Removing gframe

When the Swope TCS takes corrections another way (the TCSIS of the plan's
option B, a TCS that reads finer steps):

1. `git rm -r src/gcam/gframe`.
2. Revert the hooks above: the `zwogcam.c` lines go, `send_correction()` can
   stay as the one place that decides where a correction goes, or become
   `telio_aeg()` again.
3. In the Swope `.ini`, drop the `gframe_*` lines; add whatever the new path
   needs (for a TCSIS: the TCS host and port, and `-t`/`tcs`).
4. In `Swope.ini`, `Guider_addr` back to the converter or unused.

Nothing else in gcamzwo knows gframe exists.
