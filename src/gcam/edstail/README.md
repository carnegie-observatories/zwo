# edstail: a tcsif client for the Magellan EDS stream

`edstail.c` and `edstail.h` tail the TCS engineering data stream (EDS) that
`tcsif` fans out on TCP port 5601 of each Magellan TCS host. They connect,
subscribe with a regexp, read one record per line, reconnect with a back-off
when the stream ends, latch the rotator of interest and pass the TCS's
command echoes to the host program through hooks, gated on the host's own
rotator.

Two programs compile this pair of files, unchanged:

| Program | Where the files sit | What it does with the echoes |
|---|---|---|
| gcamzwo, the ZWO guide-camera GUI ([zwo](https://github.com/carnegie-observatories/zwo)) | `src/gcam/edstail/`, compiled in place | `src/gcam/gcamtail.c`: runs `g<p><cmd>` as a camera command, `fone` on `slew`, `ca` from `cnca`, exits when the rotator leaves its port |
| mechgui, the IMACS GUI ([imacs](https://github.com/carnegie-observatories/imacs), private) | `src/edstail/`, a git submodule on this repository's `edstail` branch | `src/gpc.c`: queues the `gp` guider-probe verbs on its worker |

The files use standard headers only (POSIX sockets, `poll`, pthreads) and
nothing from either program, so they compile in any consumer. Nothing else
belongs in this directory: the benches, the replay tool and the fixtures live
with the consumers.

## The stream

A client sends its regexp followed by a NUL byte, then reads records, one per
line (`magsoft/eds/tcscl/tcscl.c:80-88`). `tcsif` checks every peer against
`$MAGHOSTS5601` (`magsoft/eds/mag1hosts/hosts5601`, `mag2hosts/hosts5601`) by
resolving each listed name and comparing addresses. An unlisted peer is
accepted and closed at once, so `nc -zv host 5601` does not test the
permission. `edstail` reports such a close, or the reset it causes, as "stream
from H:5601 closed by the server (or reset by the server) before the first
record (is this host in tcsif's hosts5601?)".

Columns are 0-based. Real Baade records, two from 2026-10-10 UT and three from
2026-10-12 UT:

```
~Z2076600054255802 -02 43 58 066.1799 44.3888 1.4274 179.997101-002031   6F
~Z2016600061312814* gafone                                               7D
~Z2096600195779814* slew                                                 7F
~Z2106605363415814* gaxys 5 1000.0 1175.6                                39
~Z2026605370141814* cnca 1  -30.925                                      78
```

| cols | meaning |
|---|---|
| 0-2 | `~` + source letter + `2`. `Z` is the TCS; `J`, `N`, `L`, `I`, `H`, `M`, `G`, `K`, `P`, `W`, `Y` are other subsystems and are ignored |
| 7-14 | UT of the record, `HHMMSScc` |
| 15-17 | record type: `802` the rotator of interest; `81w` a command echo, `w` the sender: `0` console keyboard, `1`-`3` the GCAM1-3 links, `4` the network channel (`tcsis`) |
| 51 | `802` only: the rotator of interest, `0` NASW, `1` NASE, `2` CASS, `3` AUX1, `4` AUX2, `5` AUX3 (`4` in the first record above) |
| 20-72 | `81w` only: the echoed command. `g<p>` at cols 20-21 is a camera command for probe `p` (`a`, `b`, `c`), `gp` a guider-probe command; `slew` and `cnca <cam> <deg>` are bare TCS commands |

`802` arrives at 5 Hz, so 30 s without a record means the stream is lost. The
TCS cuts every echo at 22 characters (`tcs/mgcmd.f:316-326`).

Subscriptions in use:

```
^~Z2............8(02|1...ga|1...slew|1...cnca).*     gcamzwo, camera 1 (probe a), with ini cnca 1
^~Z2............8(02|1...gb|1...slew).*              gcamzwo, camera 2 (probe b), with ini cnca 0
^~Z2............8(02|1...gp).*                       IMACS mechgui
```

## API

```c
typedef struct {
  const char* host;      /* tcsif host; NULL or "" = tail off */
  int         port;      /* 0 = EDSTAIL_PORT (5601) */
  const char* regexp;    /* subscription, sent with a NUL */
  int         rotator;   /* own rotator of interest: the gate */
  void (*on_roi) (int old,int now);              /* latch changed; now = -1: stream lost */
  void (*on_cmd) (int sender,const char* text);  /* Z81x inside the gate */
  void (*on_drop)(int sender,const char* text);  /* Z81x outside the gate */
  void (*on_log) (int level,const char* text);   /* 0 news, 1 trouble */
} EdsTail;

int  edstail_start (const EdsTail*);   /* 0 = reader running, or tail off */
void edstail_stop  (void);             /* end the reader, wake it up */
int  edstail_telroi(void);             /* latched rotator, -1 = unknown */
int  edstail_active(void);             /* no tail, or telroi == rotator */
void edstail_line  (const char*);      /* one record; the benches' entry */
```

`edstail_start` copies the struct and its two strings. With no host it starts
no thread, but `edstail_line` still parses, latches and calls the hooks, which
is how the benches feed fixtures. One reader runs per program: a second
`edstail_start` while one runs returns `EBUSY`. Every hook may be `NULL`. The
text passed to a hook is valid only during the call.

## Behaviour

- **Records.** Only `~Z2` records with at least 18 characters count. `802` with
  a digit at col 51 latches that rotator. `81w` takes the sender from col 17
  (`-1` if not a digit) and the text from cols 20-72, trimmed at both ends; an
  empty text is ignored. Any other record is ignored. The prefix match
  (`g<p>`, `gp`, `slew`) is the host's: the regexp already selects the records,
  and fixtures carry foreign records on purpose.
- **Latch and gate.** `on_roi(old, now)` runs when the latched value changes.
  An echo goes to `on_cmd` while the latch equals `rotator`, to `on_drop`
  otherwise, also while the rotator is unknown (`-1`) after a start or a loss.
  The first `802` after a (re)connect only latches, which the host sees as
  `old == -1`.
- **Reconnect.** Connect (10 s timeout) and subscribe; read with a 30 s
  timeout. A failed connect, a failed subscription, or a connection that is
  closed or reset before its first record waits 5 s, doubling to 120 s, and is
  reported once per outage. When a stream that delivered records ends,
  `on_log` reports the loss and `on_roi(old, -1)` runs. If the stream had
  lasted 30 s, the next connect follows 1 s later and the back-off starts over;
  a shorter one waits the back-off like a failure, so a peer that accepts,
  sends and closes is not retried in a loop. A connection that stays silent for
  30 s before its first record is retried at once and reported once per
  outage: tcsif closes a refused peer at once, so silence means it accepted
  this host but the TCS feeds nothing, and the 30 s were the wait.
- **Log lines** (`on_log`; level 0 is good news, 1 trouble):
  `tailing H:5601, own rotator R` (at the first record of each connection),
  `cannot reach H:5601 (reason), retrying`,
  `subscription to H:5601 failed (reason), retrying`,
  `stream from H lost (closed by the server | reset by the server | silent for 30 s | reason), reconnecting`,
  `stream from H:5601 closed by the server before the first record (is this host in tcsif's hosts5601?), retrying`
  (the hint only for a close or a reset).
- **Threads.** The hooks run in the reader thread, so a hook that takes time
  delays the reading; the socket buffers the stream meanwhile (about 270 bytes
  per second). `edstail_stop` may be called from a hook. After it, the reader
  calls no further hook (one already running finishes) and exits within a
  second; the latch reads `-1`.

## Sharing: the `edstail` branch

This directory lives in zwo's `main`. The `edstail` branch of the zwo
repository is generated from it and holds only these files:

```
git subtree split --prefix=src/gcam/edstail --branch edstail main
git push origin edstail
```

The split is deterministic, one commit per zwo commit that touched the
directory, so anyone can regenerate the branch and get the same commit ids.
It is generated from `main` only, after the pull request that changed the
directory is merged, and it is never force-pushed, so every commit a consumer
has pinned stays reachable.

A consumer takes the branch as a submodule (IMACS's `.gitmodules`):

```
[submodule "src/edstail"]
    path = src/edstail
    url = git@github.com:carnegie-observatories/zwo.git
    branch = edstail
    shallow = true
```

and bumps its pointer at its own release:

```
git -C src/edstail fetch --depth 1 origin edstail
git -C src/edstail checkout FETCH_HEAD
git commit -m "chore: edstail <sha>: what changed" src/edstail
```

A fresh clone of a consumer fetches zwo `main`'s tip tree once before the
pinned commit, about 56 MB in 2026. A developer avoids that with
`git clone --depth 1 --branch edstail <zwo url> src/edstail` before
`git submodule add -b edstail <zwo url> src/edstail`, which registers the
existing clone. `git subtree pull --prefix=src/edstail --squash <zwo url> edstail`
fetches only the branch and is the alternative to a submodule.

## Changing the core

1. Change the files here, in a zwo pull request; run zwo's bench
   (`make test` in `src/gcam`) and, if the change can affect IMACS, IMACS's
   `tools/gpcbench.sh` against the branch.
2. After the merge, regenerate the `edstail` branch from `main` and push it.
3. Each consumer bumps its pointer when it wants the change.
