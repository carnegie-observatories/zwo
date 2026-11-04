# gcam: poll tcsis for the TCS's command echoes — the `cmdpoll` transport — Plan

**Status:** drafted 2026-11-03, revised twice, built the same night on
`feature/gcam-gcmd-poll`, and changed on 2026-11-04 after a code review of the E track
(not pushed); "As built" at the end lists the commits, says where the build departs from
this text and what the tests showed. The zwo part of IMACS's
`docs/plans/guider-e-implementation.md` (branch `IMACS-gcmd-poll`), which orders the work
across magsoft, zwo and IMACS, and of the design in `guider-architecture.md` §6 beside it
(option E: the TCS's command echoes kept in a ring on the TCS host, answered by tcsis as
`gcmd <port> <after>`, polled by each guider at the DOS cameras' rate). A separate track:
the branch of this plan, `feature/gcam-gcmd-poll`, starts from `upstream/main`
(`5987156`, the merge of zwo#41) with commits of its own. It takes nothing from the
camera-tail branch `feature/gcam-eds-tail` (local, no pull request), whose reactions it
writes again on the poll, nor from zwo#40 (`feature/cursor-tenths-mount`), which both
tracks need and which stays its own pull request. It leaves `src/gcam/edstail/`, which
zwo#41 merged into `main`, unused; it stays while the other track lives. Line numbers of
the camera-tail branch below are of `cf5a465`, where the reactions this plan re-creates
are defined.

## What this is

The camera-tail branch gives gcamzwo an EDS tail: `src/gcam/edstail/` subscribes to tcsif
:5601, parses the stream, latches the rotator of interest and hands the TCS's command
echoes to `src/gcam/gcamtail.c`, which runs `g<p><cmd>` as a camera command, `fone` on a
`slew`, `ca` from `cnca`, and exits when the rotator leaves the camera's port.

This plan keeps what `gcamtail.c` decides and changes how the echoes arrive: instead of
the stream, the camera asks tcsis every 250 ms for the next echo tagged with its port, on a
socket of its own, as a DOS camera asks the TCS with `:N9`. The parser stays out of the
camera (it is `edsshm`'s, in magsoft, on the TCS host), the `hosts5601` entry per camera
host goes, and a camera that was away for a second, or lost tcsis for a while, finds what
it missed in tcsis's ring (its last 64 echoes) instead of losing it. The reasons are the
architecture document's §6.1 ("Why pull, not a subscription") and its §8 ledger.

The poll is this track's only transport. Rollback on the telescope is the relay
(`gcam2zwo.py` forwarding again, `cmdpoll 0` and `cnca 0` in the cameras; IMACS plan §8),
not the tail.

## Design

### `src/gcam/gcmd.c`, `gcmd.h`: the poll

```
typedef struct {
  const char* host;                    /* tcsis host; NULL or "" = no poll */
  int         port;                    /* tcsis port (5800, 5801, 5810-5815) */
  int         rotator;                 /* own rotator 0-5: whose echoes to ask for */
  void (*on_roi)(int old,int now);     /* rotator of interest changed, -1 = unknown */
  void (*on_cmd)(int sender,const char* text);  /* an echo for 'rotator' */
  void (*on_log)(int level,const char* text);   /* 0 news, 1 trouble */
} Gcmd;

int       gcmd_start (const Gcmd*);    /* 0 = polling, or no host; else an errno */
void      gcmd_stop  (void);           /* end the poll: after it the thread calls */
                                       /* no hook (one already running finishes) */
int       gcmd_telroi(void);           /* last answered rotator of interest, -1 = unknown */
int       gcmd_active(void);           /* no poll, or gcmd_telroi() == rotator */
int       gcmd_line  (const char* answer);  /* one answer, no socket (benches): */
                                       /* 1 an echo, 0 nothing newer, -1 none */
int       gcmd_hello (const char* answer);  /* the bare gcmd's answer (benches): */
                                       /* 0 it knows gcmd, -1 not */
long long gcmd_after (void);           /* the cursor: the next request's <after> */
```

The thread: a socket of its own to the host and port (a non-blocking connect with a 2 s
timeout). Its first request on each connection is a bare `gcmd\n`, and it reads one line
(3 s at most):

- `<roi> <latest> -`: tcsis has `gcmd` and the ring. The thread polls, and the camera logs
  `eds: polling HOST:PORT for port N`. At a start, with the cursor still `-1` from
  `gcmd_start()`, it takes `<latest>` as its cursor and `<roi>` as the rotator of
  interest, so nothing older is replayed; on a reconnect it takes neither and goes on from
  the cursor it kept, or from 0 when `<latest>` is below that cursor (below);
- an empty line: a tcsis older than `gcmd`. To it a request without a space is a status
  request it does not know, answered with nothing logged and nothing sent to the TCS;
- `-1`: a tcsis without the ring (an old `edsshm`).

Either of the last two is logged once ("no gcmd", "no echo ring"), and the bare `gcmd` is
asked again every 5 s on the same socket until tcsis answers it with `<roi> <latest> -`.

Then it sends `gcmd <rotator> <after>\n` and reads one line (3 s at most), again at once
after an echo, 250 ms later otherwise:

- `<roi> <seq> <sender> <text>`: `on_cmd(sender, text)`; `after = seq`; the `roi` of this
  answer is not taken (below). An echo with blank text, or of 64 characters or more (tcsis
  sends 22 at most), also advances the cursor but never reaches `on_cmd`; the long one is
  passed over with a warning;
- `<roi> <latest> -`: nothing newer; `after = latest` (nothing for this rotator up to
  there; a cursor past `latest` comes back to it);
  `on_roi(old, roi)` when `roi` differs from the last one taken; wait for the next tick;
- `-1`: tcsis has no ring; logged once, asked every 5 s until it answers.

`<roi>` is `0`-`5`, or `-` when the rotator of interest is unknown; the client takes a
digit 6-9 as unknown too.

The rotator of interest is taken from the `-` answers only: an answer with an echo carries
the rotator of now, and taking it made a camera with echoes still to come exit before
acting on them (the bench's live LLAMAS night: six commands short of the stream's 51).

A failed request closes the socket, warns on the camera's message line once per outage
("eds: tcsis HOST:PORT lost, retrying") and sets the answered roi to unknown (`-1`, what
`status` shows); the reconnect backs off, 1 s doubling to 30 s, back to 1 s once tcsis
answers. The cursor is kept: `after` is `-1` only at `gcmd_start()`, and a reconnect asks
with the cursor of the last answer. So the camera catches up, in order, on what the TCS
took in between: commands typed at the console or sent from the TO's Tcl GUI through
tcsif :5602, which need no tcsis, while tcsis was down; and commands sent through the
restarted tcsis before the camera's back-off ran out. A bare answer whose `<latest>` is
below the cursor means the ring started again (`edsshm` on a new segment, which restarts
tcsis too): the cursor goes to 0, and the camera takes the new ring from its first echo.
tcsis's rules bound the catch-up: a cursor older than the ring starts at `latest-63`, and
tcsis logs that the client fell behind. The rotator of
interest stays unknown until the first `-` answer, so the camera acts on the echoes it
missed before it sees a change of rotator (the port exit).

`gcmd_stop()` wakes the thread (a pipe every wait polls, as `edstail` has) and does not
join it: the port exit calls it from inside a hook, and the command port's `exit` holds the
command mutex a hook may be waiting for. After it the thread calls no hook, `on_log`
included (one already running finishes). Hooks run in the poll thread. `gcmd_line()` and
`gcmd_hello()` run the thread's two parsers without a socket, for the benches: an answer
to `gcmd <rotator> <after>`, and a bare `gcmd`'s; `gcmd_after()` reads the cursor. About
400 lines, standard headers only (POSIX sockets, `poll`, pthreads: neither `tcpip.c` here
nor IMACS's has the timeouts), so the bench links it alone. tcsis gates by port, so there
is no `on_drop`: an echo for another port never reaches the camera. There is no age limit
on what the camera acts on (IMACS plan §1.2): the ring's 64 entries, about a minute of
commands while a camera guides, bound what a camera that froze could replay. A tcsis
outage stretches that: gcamzwo's `aeg` corrections go through tcsis and stop with it, so
the ring then holds only echoes from the console, the Tcl GUI and the DOS cameras' links,
and a reconnect can replay commands as old as the outage, in order, 64 at most.

`gcmd.c` is one of two copies of the client; IMACS's `src/gcmd.c` is the other, the same
code below a header of its own (decided 2026-11-03, IMACS plan §1.3). The header of each
says so:

```
 * One of two copies of the gcmd client: zwo src/gcam/gcmd.c and imacs
 * src/gcmd.c, the protocol in imacs docs/plans/guider-architecture.md,
 * Appendix A.  Keep them the same: a change to the loop, the answer's
 * parsing or the back-off in one is made in the other, in pull
 * requests of the two repositories opened together.
```

and both benches feed the exchanges of that Appendix A, copied in as data, through
`gcmd_line()` and `gcmd_hello()`, so a copy that drifts fails its own bench.

The tcsis host and port are telio's (`zwogcam.c`: `5810+rPort` on a telescope for ports
1-5, `5801` for port 0 and for the simulator): a seven-line accessor in `telio.c`
(`telio_server(char* host, int n, int* port)`, -1 while telio is offline) hands them over,
so the ini needs no new host key and the poll's socket is a second connection from the
same host to the same port, which tcsis's allow-list (`hosts5801`, for :5801 and :581x)
already admits.

### `src/gcam/gcamtail.c`, `gcamtail.h`: the reactions

Written again on the `gcmd` hooks, from the camera-tail branch's `gcamtail.c:57-124`:

- `g<p><cmd>` for this camera's probe letter (`'a' + gnum - 1`): the rest of the line as a
  camera command through `remote_command()`;
- any command starting `slew`: `fone`;
- `cnca <gnum> <deg>` for this camera's number, when the ini says `cnca 1`: `ca <deg>`,
  the angle verbatim. It is IMACS's plain probe angle; the camera's own `mount` (zwo#40:
  -90.0 on gcam01, -90.4 on gcam02) makes it the position angle;
- every other echo (the camera's own `aeg`, the other probes' commands): passed over
  without a word;
- `on_roi`: when the answered roi leaves the camera's `port` after having been on it,
  `remote_exit()` (`portexit 1`), or a log line (`portexit 0`); the first answer after a
  start sets the last known port without exiting, and an unknown roi (`-1`) keeps it, so
  the exit also comes across a tcsis outage.

The log lines are the camera-tail branch's, word for word (`eds: '%s' from %d`,
`eds: '%s' (%s) from %d`, `eds: port %d left for %d, exiting`, `eds: roi %d -> %d, gate
%s`): the rig's `gpcrig.py` and the bench read them, and they say what the camera did, not
how it heard. Two kinds are new: `eds: polling HOST:PORT for port N`, written on each
connection once tcsis has answered the bare `gcmd`, which the rig's `rig-cams.sh` and
`ledger.py` and the bench wait for; and the client's warnings (tcsis lost or not
answering, no `gcmd`, no echo ring, an echo passed over).

### `zwogcam.c`

`remote_command()` and `remote_exit()` under one mutex with the command port (the
camera-tail branch's `zwogcam.c` lines for them, written again, about 25 lines); the ini
keys `cmdpoll`, `cnca`, `portexit` (in `guider.h`); `gcamtail_start()` at the end of the
start-up, before the main loop, with telio's host and port, or a warning and no poll when
`cmdpoll 1` meets an offline telio; `gcamtail_stop()` in `my_shutdown()`; the `status`
reply gains `eds=` (polling or not) and `roi=`. The command port keeps trusting any caller
(decided 2026-11-03): no allow list on this track, and the camera-tail branch's
`cmd_hosts` was dropped from that branch the same day.

### Configuration

| Key | Default | Meaning |
|---|---|---|
| `cmdpoll` | 0 | 1: poll tcsis for the echoes |
| `cnca` | 0 | 1: `cnca <gnum> <deg>` becomes `ca <deg>` |
| `portexit` | 1 | exit when the answered roi leaves `port` |
| `port` | as today | the camera's rotator: what the poll asks for |

The inis of the cameras that take the TCS's guider commands get `cmdpoll 0`, so the switch
is one edit on the host: on Baade `gcam41.ini` and `gcam42.ini` (LLAMAS, AUX2) and
`mage.ini` (AUX3); on Clay `gcam11.ini`, `gcam12.ini`, `gcam41.ini`, `gcam42.ini`,
`mike.ini` and `pfs.ini`, each with a note that `cmdpoll 1` needs magsoft's echo ring,
which Clay does not have yet. `mike.ini`'s `port 1` stays commented out: set, it moves
MIKE's camera from TCSIS :5801 to :5811 (the rotator-1 gate) and its ROTN to 1, a change
on Clay; Clay's cut-over sets it, since the poll asks for that rotator. Baade's IMACS
cameras, `gcam01.ini` and `gcam02.ini`, come with zwo#40 and are not on `main`; when the
two meet they need `cmdpoll`, `cnca 1` and #40's `mount` lines (-90.0, -90.4) together, or
the angle is 90° off. Once the `mount` lines are
on the cameras, the relay must send the plain angle too, with magsoft !9's
`GCAM2ZWO_CA_OFFSETS=0`, whichever path forwards it. The relay's switch,
`GCAM2ZWO_FORWARD=0`, is one for the whole telescope, so on Baade all five cameras change to
`cmdpoll 1` at once (IMACS plan §8). `docs/ZWO/zwogcam.html`: a section on the poll (the
keys, the rate, the `status` fields, the exit rule, the TCSIS it needs, the log).

### Files

| File | Change |
|---|---|
| `src/gcam/gcmd.c`, `gcmd.h` | new, about 400 and 50 lines |
| `src/gcam/gcamtail.c`, `gcamtail.h` | new, about 190 and 40 lines |
| `src/gcam/zwogcam.c`, `guider.h` | `remote_command`, `remote_exit`, the keys, start and stop, `status`; about 55 lines |
| `src/gcam/telio.c`, `telio.h` | `telio_server()`, 7 lines |
| `src/gcam/makefile` | `gcmd.o`, `gcamtail.o`; `gcamtail_test`; `test`, `ledger` |
| `src/py/tcsis_sim.py` | the ring built as `edsshm` builds it (every `Z81w` record with a command, its roi from the preceding `Z802`, `0`-`5` or `-`, its sender digit and columns 20-41) from `--fixture FILE[@T]` (released `--pace S` apart from T s on) or from `--stdin` (records as they come, and `!down`, `!up`, `!noring`, `!ring`, `!old`, `!new`, `!newring`), and `gcmd` answered from it with tcsis's rules (a bare `gcmd` with `<roi> <latest> -`, no replay at `-1`, the 64-entry depth, a client behind the ring logged); `--down T0,T1`, `--noring T`, `--old T` (a tcsis without `gcmd`: an empty line for the bare `gcmd`, the full request taken for a TCS command and logged); the existing `--delay` stays the per-request service time; about 200 lines more |
| `src/gcam/gcamtail_test.c` | new: links `gcmd.o` and `gcamtail.o` with recording stand-ins (`remote_command`, `remote_exit`, `message`); `gcamtail_test [opts] ANSWERS...` feeds answer lines through `gcmd_line()` (`HELLO <answer>` through `gcmd_hello()`), `--trace` prints the cursor, rotator and result after each, `--poll HOST:PORT` runs the real poll; prints ACTION, EXIT, LOG, WARN and ROI lines |
| `src/py/gcmd_bench.py` | new: Appendix A's exchanges through `gcamtail_test --trace`; derives the expected ACTION and EXIT lines from each fixture (every `Z81w` while the preceding `Z802` says the camera's port, the camera's own policy applied), runs the line cases through `gcamtail_test` and the live cases against `tcsis_sim.py --stdin`, with the ledger tests' bench parts below; `make test`, `make ledger` |
| `src/py/fixtures/` | the real Baade nights of the camera-tail branch, copied as data; `gcmd_exchanges.txt`, Appendix A's exchanges, the same file as IMACS's `tests/fixtures/gcmd_exchanges.txt` |
| `etc/ini/*/*.ini` | `cmdpoll 0` (Configuration above) |
| `docs/ZWO/zwogcam.html` | the poll's section |

## Steps

1. `gcmd.c`, `gcamtail.c`, the `zwogcam.c` changes; `make gcamzwo`.
2. `tcsis_sim.py --fixture`, `gcamtail_test`, `gcmd_bench.py`; `make test`.
3. The inis and the manual.
4. On the rig (IMACS plan §6): `RIG_CAMS=poll`, the camera A/B (`RIG_AB=cams`): side B on
   the poll against side A on the relay, and the ledger run; the zwo tree there is the
   local integration branch of this branch with zwo#40 and the emulator's `--bind`.

Commits on `feature/gcam-gcmd-poll`, in order: `gcam: poll tcsis for the TCS's echoes
(cmdpoll)` (1), `bench: the poll against a tcsis stand-in` (2), `docs, etc: cmdpoll
in the manual and the inis` (3). One pull request from `main`; CI's `build` and
`build-macos` as for zwo#40.

## Testing

- `make test` (81 checks, about 3 minutes), the exchanges: the 15 of Appendix A
  (`gcmd_exchanges.txt`), each answer fed through `gcmd_line()`, or `gcmd_hello()` for a
  bare `gcmd`; after each, the cursor, the rotator of interest and the result, the echo as
  the camera logs it (the command and the sender), and the request, which must be the one
  the client sends with the cursor it has.
- `make test`, the line cases: every fixture, the four camera configurations of gnum, port,
  cnca and portexit, give the ACTION and EXIT lines the derivation expects.
- `make test`, the live cases against the stand-in: the same night twice with the stand-in
  away between the passes (the camera warns, then polls again); a night that comes back on
  another rotator after a gap (the port exit, and `portexit 0`); the stand-in answering
  `-1` for about six seconds before it serves (the slow retry); a TCSIS without `gcmd`
  (`!old`): the camera says so once and sends only the bare `gcmd`, every 5 s, nothing for
  the TCS, then polls and acts once the stand-in has `gcmd`; a new ring (`!newring` while
  the stand-in is down) numbered below the camera's cursor and ending at another rotator:
  the camera asks `gcmd 0 0`, acts on its echoes in order, and only then exits.
- `make test`, the ledger tests' bench parts (IMACS plan §5): L1, the stand-in killed: a
  warning within one second, roi unknown, the next command acted on once it is back; L2,
  the stand-in away for 70 s: the poll back within the 30 s cap with nothing restarted,
  and a camera started then polls in its first second; L5, each echo for the camera acted
  on exactly once and in order through a stand-in restart, with two echoes fed while it is
  down and two after it is back but before the camera reconnects, and a camera restart: 34
  of 34, none for the other letter or another rotator, no log line in 30 idle seconds; L7,
  40 commands in 2 s with the client stopped for 1.5 s: all 40 in order, and 70 while
  stopped: the newest 64 in order, the stand-in logging the client behind the ring. L2's
  70 s is past the client's attempt at 61 s, so a missing cap shows: the next attempt
  comes at 91 s with the cap, at 127 s without. `make ledger` runs L2's outage for three
  minutes and L5's idle for ten, the IMACS plan's lengths.
- Mutants of the client that the bench catches: the cursor reset on a reconnect (L5: 30
  of 34), `gcmd 0 -1` sent to an old tcsis (the TCSIS without `gcmd`), the back-off
  without its cap (L2), a new ring read from its `<latest>`, and one whose rotator is
  taken on the reset (each: the new-ring exchange and the live new ring).
- The rig's camera A/B and ledger run: side A on the relay, side B on the poll.
- Not tested here: tcsis itself (magsoft's `test/gcmd_test.py`, IMACS plan §2.6), the load
  of four pollers on it (the rig, IMACS plan §6 item 7), and an answer cut mid-line, which
  the review checked by hand (the echo acted on once, with the kept cursor).

## Out of scope

The camera's reactions beyond the camera-tail branch's table (MOP-690's wish list), the
`aeg` corrections and the `:5603` records, which stay as they are; the wait form `gcmdw`.

## Decisions

Taken 2026-11-03 (IMACS plan §9): a separate track from `main`; two copies of the poll
client, kept the same by the header note and the shared exchanges; no staleness bound; no
allow list on the command port, here or on the camera-tail branch. Taken in the review
(2026-11-03/04), in both copies: the bare `gcmd` first on each connection, and the cursor
kept across a reconnect. The back-off's cap stays 30 s: 5 s would bring a client back
sooner after tcsis returns, at one connect per 5 s per client while tcsis is away. The bare
answer carries no ring generation number: a new ring shows as a `<latest>` below the kept
cursor, and the client goes back to 0 (the final check, 2026-11-04). Only a new ring whose
`<latest>` has already reached the kept cursor goes unseen, its first echoes passed over,
which needs an old ring no older than the outage.

## As built (2026-11-03; after the review and its final check, 2026-11-04)

`feature/gcam-gcmd-poll`, from `upstream/main` `5987156`, not pushed. The review's changes,
and its final check's, are folded into the same three commits; the first build is kept as
`backup/gcam-gcmd-poll-pre-review` (`806f486`, `710c705`, `aefabb2`), the review's as
`backup/gcam-gcmd-poll-pre-final` (`d77ba18`, `24efe5d`, `3417c7b`), and the final check's
first, which the last rig runs used, as `backup/gcam-gcmd-poll-pre-verify` (`9bdfdde`,
`6365d5c`, `1e42d25`).

| Commit | What |
|---|---|
| `4f0313f` gcam: poll tcsis for the TCS's echoes (cmdpoll) | `gcmd.c/.h`, `gcamtail.c/.h`, `zwogcam.c`, `guider.h`, `telio.c/.h` (`telio_server()`), the makefile's objects |
| `11ac925` bench: the poll against a tcsis stand-in | `gcamtail_test.c`, `tcsis_sim.py`, `gcmd_bench.py`, the fixtures and `gcmd_exchanges.txt`, `make test`, `make ledger` |
| docs, etc: cmdpoll in the manual and the inis | `zwogcam.html`; `cmdpoll 0` in the nine inis that take guider commands; this plan |

Where it departs from the design above, and why:

- The client's two rules (the cursor taken from every `-` answer, the rotator of interest
  from the `-` answers only) and its code (POSIX sockets, not `tcpip.c`; a stop that does
  not join): above, and IMACS plan §1.1, §1.3.
- Baade's `gcam01.ini` and `gcam02.ini` are zwo#40's, not on `main`: they need `cmdpoll`,
  `cnca 1` and #40's `mount` lines when the two meet (the rig's inis have them).
- `make test` takes about 3 minutes (L2's outage 70 s, L5's idle 30 s); `make ledger`
  runs them at the IMACS plan's length (3 and 10 minutes), about 14 minutes.
- The bare `gcmd` (the review). As first built, the client's first request on a connection
  was `gcmd <port> -1`. A tcsis older than `gcmd` (Baade's and Clay's in production) takes
  a request with a space and a first word it does not know for a TCS command, so the MGTCS
  got the request as a network command every 5 s from every polling client. At Baade's
  rotator 0 the TCS echoes `gcmd 0 -1` and queues `md 0 -1` for guider camera 3; nothing
  acts on `md`, but the echo stream, that camera's queue and the decode-error records are
  polluted. A request without a space is a status request to every tcsis since 2016, and
  one it does not know is answered with an empty line, nothing logged, nothing sent to the
  TCS; so each connection now starts with the bare `gcmd`, which magsoft's new tcsis
  answers `<roi> <latest> -`: above.
- The cursor kept across a reconnect (the review). As first built, `after` was `-1` again
  after every reconnect, so an echo the TCS took between the camera's last answer and its
  first after the reconnect was never acted on, without a word: commands typed at the
  console or the TO's Tcl GUI while tcsis was down, and commands sent through the
  restarted tcsis during the back-off, up to 30 s after it returned. Today's relay reads
  tcsif directly and delivers both. Now `after` is `-1` only at `gcmd_start()`: above.
- The back-off returns to 1 s once tcsis answers, as `gcmd.h` says and the code did from
  the first build; the design's "reset after a connection that lasted" was never built.
  The review kept the code's rule: tcsis never answers once and then drops a connection,
  and the rule brings a camera back soonest after a real restart.
- The review (2026-11-03/04, of the E track's three branches and its plans) made the two
  protocol changes above, and these others. The client takes an answer's rotator digit
  6-9 as unknown, not as no answer; an echo with blank text, or of 64 characters or more,
  advances the cursor without reaching `on_cmd`; `on_log` is not called after
  `gcmd_stop()`; `gcmd_hello()` feeds the benches a bare `gcmd`'s answer. The bench checks
  each exchange's request and echo (12 exchanges, were 8), runs a TCSIS without `gcmd`,
  keeps L2's outage at 70 s (was 40 s) so that a missing cap shows, and feeds L5 echoes
  during the outage. The manual now says which `mount` lines `cnca 1` needs, that the
  relay's switch is one for the whole telescope, and which TCSIS the poll needs; MagE's ini
  gets `cmdpoll 0`, and Clay's a note.
- The final check (2026-11-04), three reviewers of the code, the docs and the history as
  the review left them. A ring that started again (`edsshm` on a new segment, which
  restarts tcsis too) was read from its `<latest>`: the kept cursor, above it, was
  answered `<roi> <latest> -`, so the camera passed over the new ring's first echoes
  without a word (against magsoft's tcsis and `edsshm`, a camera skipped two). Now a bare
  answer whose `<latest>` is below the cursor sends the cursor to 0, in both copies, and
  the rotator is still not taken; the shared exchanges gain three rows (15: the new ring
  at another rotator, and a reconnect with nothing new), and the bench a live new ring
  (`!newring`) that checks the camera's `gcmd 0 0` and its exit at the new rotator after
  the echoes. A new ring that has already reached the cursor still goes unseen
  (Decisions). `mike.ini`'s `port 1` goes back to a comment (Configuration above), the
  stand-in says how it and `edsshm` differ on a `Z802` too short for column 51, and the
  first commit's subject is shorter. A second round of reviewers checked these changes
  (IMACS plan §11).

Tested after the review (2026-11-04):

| Run | Result |
|---|---|
| `make test`, macOS arm64 | 78 checks, all passed, in about 3 minutes: Appendix A's 12 exchanges with the request and the full echo; 13 nights × configurations as answers; 5 live nights, the `-1` TCSIS and a TCSIS without `gcmd` (only the bare `gcmd`, every 5 s, nothing for the TCS); L1; L2 (70 s); L5 with two echoes fed while the stand-in was down and two after it was back but before the camera reconnected: 34 of 34, once, in order; L7 |
| `make test` on mutants of the client | each caught: the cursor reset on a reconnect (L5: 30 of 34), `gcmd 0 -1` sent to an old tcsis, the back-off without its cap |
| `make ledger`, macOS | all checks passed, in 14 minutes: L1 warned in 0.19 s; L2 polling again 1.5 s after a 180 s outage (the 30 s cap is the bound); L5 34 of 34 once and in order, four of them fed before the camera was back, and no log line in 600 idle s; L7 40 of 40, the newest 64 of 70, tcsis logging the camera behind |
| `make test` after the final check, macOS arm64 | 81 checks, all passed: Appendix A's 15 exchanges; the live new ring (the camera asks `gcmd 0 0`, acts on both echoes, then exits at rotator 4); the rest as above |
| `make test` on mutants of the new-ring rule | each caught by the new-ring exchange and the live new ring: the rule left out (the cursor stays 14, not 0), and the rotator taken on the reset (4, not 0; the camera exits before acting) |
| the new ring end to end, Linux x86-64 | `gcamtail_test --poll` against magsoft's 64-bit tcsis and `edsshm`; the segment removed and made again, two echoes in it, before tcsis came back: the camera ran both, then the next; without the rule it ran neither |
| `make ledger` after the final check, macOS | 81 checks, all passed: L1 warned in 0.23 s; L2 polling again 1.5 s after a 180 s outage; L5 34 of 34, four fed before the camera was back, no log line in 600 idle s; L7 40 of 40, the newest 64 of 70, tcsis logging the camera behind; the exchanges and the new ring as in `make test` |

The rig's runs after the review go in the IMACS plan's §11.

Tested before the review, the first build (`806f486`, `710c705`, `aefabb2`), kept as
history:

| Run | Result |
|---|---|
| `make test`, macOS arm64 (clang `-Wall -Wextra`: no warning in the new files) | all checks passed: Appendix A's 8 exchanges, 13 nights × configurations as answers, 5 live nights, the `-1` TCSIS, L1, L2 (40 s), L5 (30 s idle), L7 |
| `make test`, Linux x86-64 (Ubuntu 24.04, gcc 13) | all checks passed |
| `make ledger`, macOS | all checks passed: L1 warned in 0.21 s; L2 polling again 1.5 s after a 180 s outage (the outage ended 1 s before the attempt at 181 s: the bound is the 30 s cap); L5 30 of 30 once and in order (none sent while the stand-in was down), no log line in 600 s; L7 40 of 40, the newest 64 of 70 |
| `gcamzwo`, macOS and Linux x86-64; the rig's arm64 | built; the same eight `zwogcam.c` warnings as `main` |
| `rig/e-plus-40` (this branch, zwo#40 and the emulator's `--bind`) | merges without conflict, builds, line checks pass |
| the rig's camera A/B (`cams3`; local containers, `tcs-port` out of reach) | 0 failures on all 14 steps against the relay: each camera's guiding, angle and box, and every command, the same; each exits once at the rotator step. The first run (`cams2`) found the relay's own exits doubling the cameras' and ending a `portexit 0` camera; magsoft's `GCAM2ZWO_FORWARD=0` sends no exits since |
| the rig's ledger (`ledger1`, side B) | L1: both cameras warned 0.15 s after tcsis died, `roi=-1`, polling again 0.7 s after its return; L2: back 1.3 s after a 180 s outage (the same alignment: the bound is the 30 s cap), a camera started afresh polls within 1 s of its poll starting; L4: moved to 127.0.0.4, the box and the angle reached it; L7: 40 of 40 in order 0.5 s after the burst with the camera stopped 1.5 s, the newest 64 of 70; L5: 31 of 31 once and in order through a tcsis and a camera restart (none sent while tcsis was away), none at rotator 4 (`portexit 0`), no log line in 600 s |
| the rig on `tcs-port` (2026-11-04, 06:00-07:00 UTC) | `cams2` there: 0 failures on 14 steps. `ledger1` there, side B: two failures, both in the ledger's measurement, not the code: L7's "newest 64" got 66 (5 to 70, in order, none twice) because camera 1 was continued before the last two of the 70 echoes had reached the ring; L5's first command for camera 2 showed in its state after 2.58 s, against a 2 s bound, though the camera logged it in the second it was sent: its first find after a quiet spell took the rest (its next five took 0.25 to 0.75 s). The ledger now waits for the 70 to be in the ring, and starts camera 2 guiding before it times it |
