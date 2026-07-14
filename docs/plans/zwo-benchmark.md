# ZWO Server FPS Benchmark (C) — Plan

## Context

The ZWO camera server [zwoserver.c](src/server/zwoserver.c) streams camera frames to remote clients over TCP/IP (port 52311). In video mode a background thread continuously calls `ASIGetVideoData` into two rotating buffers ([zwoserver.c:976-1005](src/server/zwoserver.c#L976-L1005)) and clients pull each new frame with `next`. There is currently no way to quantify end-to-end throughput — how many frames per second actually arrive at the client when exposure time, ROI/binning, and pixel bit-depth change.

This plan adds a **C** benchmark program that measures client-side FPS while sweeping those knobs and prints a table comparing **actual FPS vs expected FPS (1/exptime)** per configuration. C is chosen over Python because Python's per-frame interpreter overhead and GC pauses introduce jitter that masks the quantity under test; the project already uses native TCP and timing helpers in [tcpip.c](src/server/tcpip.c) / [utils.c](src/server/utils.c), so reusing them gives sub-millisecond-accurate measurements with minimal code.

## Scope & Decisions (from user Q&A)

- **Language**: C only (Python variant dropped for timing reliability).
- **Mode**: video streaming only (`start` → loop `next` → `stop`).
- **"Bitrate" = pixel bit depth** (8 vs 16). A clearly-labelled TODO in the source notes that a future sweep of `ASI_BANDWIDTHOVERLOAD` would require a server-side addition to zwoserver's command set (it is not currently exposed).

## Protocol Facts Used

Authoritative reference: [zwoserver.c:472-834](src/server/zwoserver.c#L472-L834), [zwo.h](src/server/zwo.h).

- Port = **52311** (PROJECT_ID=23, [zwo.h:7,12](src/server/zwo.h#L7)).
- Text commands end in `\n`; text responses end in `\n`.
- `open` → `"W H cooler color bitDepth model"`.
- `setup x y w h bin bits` → `"x y w h bin bits"`; server floors w to multiple of 8, h to multiple of 2 ([zwoserver.c:554-555](src/server/zwoserver.c#L554-L555)).
- Pixel format chosen inside `setup` by `(color, bits)` ([zwoserver.c:556-560](src/server/zwoserver.c#L556-L560)): mono → RAW8/RAW16; color → Y8/RAW16/RGB24.
- `exptime SEC` → `"SEC"`.
- `start`/`stop` toggle video streaming.
- `next [timeout_sec]` → `"seq temp power"` + `(w*h*bits/8)` bytes raw pixel data, or `"-Enodata"` if no frame in timeout ([zwoserver.c:644-666](src/server/zwoserver.c#L644-L666)). `seq` is monotonic — gaps = drops.
- MIN_EXPTIME = 0.0001 s, MAX_EXPTIME = 30 s ([zwo.h:37-38](src/server/zwo.h#L37-L38)).

## Reusable Existing Code

The benchmark links against the same helpers the rest of the project uses, so there is no new TCP or timing code:

- [tcpip.h](src/server/tcpip.h) / tcpip.c:
  - `TCPIP_CreateClientSocket(host, port, &err)` — open socket.
  - `TCPIP_Request(sock, cmd, buf, buflen)` — send cmd+`\n`, read text response until `\n`.
  - `TCPIP_Recv(sock, buf, nbytes, timeout_sec)` — blocking recv with timeout; loop to receive the binary frame body.
- [utils.h](src/server/utils.h) / utils.c:
  - `walltime(0)` — `gettimeofday()`-based monotonic `double` seconds (sub-µs resolution).
  - `msleep(ms)` — portable millisecond sleep.
- **Reference**: [gcam/zwotcp.c:360-422](src/gcam/zwotcp.c#L360-L422) uses the exact same pattern we need:
  - `sprintf(cmd,"next %.2f", fmin(expTime+1.0, 2.0));`
  - Detect `"-Enodata"` → `msleep(350)` and retry.
  - On success: `sscanf(buf,"%u %lf %d",&seq,&tmp,&per)` then loop `TCPIP_Recv` until full frame received.
  - FPS EMA: `fps = 0.7*fps + 0.3/(t2-t1)` — we will also compute the **true** average `fps = frames/elapsed` over the whole window, since EMA is biased for a benchmark.

## Deliverable — `src/benchmark/zwo_benchmark.c` + `src/benchmark/makefile`

A single C source file plus a makefile modeled after [src/server/makefile](src/server/makefile). Lives in a new `src/benchmark/` directory to keep it separate from the server build.

### Source structure

```
src/benchmark/zwo_benchmark.c
  static struct BenchCfg  { host, port, duration_s, warmup_s, next_timeout_s,
                            int n_exp, double exptimes[32],
                            int n_bin, int bins[8],
                            int n_bit, int bits[4],
                            int gain, int offset, int have_gain, have_offset,
                            char *csv_path, int verbose; }
  static struct BenchRow  { exptime, bin, bits, w, h, frames, elapsed, fps,
                            expected_fps, efficiency_pct, bytes_per_frame, mbps,
                            drops, enodata_count, errbuf[64]; }

  parse_args(argc, argv, &cfg)              # --host --port --exptimes 0.01,0.1,... --bins 1,2 --bits 8,16 --duration --warmup --next-timeout --gain --offset --csv --verbose
  open_camera(sock, &W, &H, &color, &model) # sends 'open', parses response
  setup_roi(sock, w, h, bin, bits, &out_w, &out_h, &out_bits)
                                            # sends 'setup 0 0 w h bin bits', reads echoed row back
  set_exptime(sock, t)                      # sends 'exptime %f', checks echoed value
  start_stream(sock) / stop_stream(sock)
  recv_frame(sock, buf, nbytes, timeout_s)  # loop TCPIP_Recv until nbytes received or error
  next_frame(sock, timeout_s, &seq, &temp, &power, buf, nbytes)
                                            # returns 0 ok, 1 -Enodata, <0 error
  run_one(sock, exptime, bin, bits, &row)   # does warmup + measurement window
  print_header(W, H, color, model, &cfg)
  print_row_live(&row)
  print_table(rows, nrows)                  # final fixed-width ASCII
  write_csv(rows, nrows, path)
  install_sigint_handler()                  # sets g_stop=1; measurement loops check it
  main()                                    # one socket for whole run, reused across all configs
```

### Pseudocode for `run_one`

```c
// configure
setup_roi(sock, W/bin, H/bin, bin, bits, &w, &h, &bits_actual);
set_exptime(sock, exptime);
int nbytes = w * h * (bits_actual / 8);
if (nbytes > buf_cap) { buf = realloc(buf, nbytes); buf_cap = nbytes; }

// warmup: discard frames for max(warmup_s, 5*exptime) and at least 5 frames
start_stream(sock);
double t_warm_end = walltime(0) + fmax(cfg.warmup_s, 5.0*exptime);
int warm = 0;
while ((walltime(0) < t_warm_end || warm < 5) && !g_stop) {
    int r = next_frame(sock, cfg.next_timeout_s, &seq, &tmp, &per, buf, nbytes);
    if (r == 0) warm++;
    else if (r == 1) msleep(5);
    else { strcpy(row->err,"warmup fail"); goto cleanup; }
}

// measurement
u_int last_seq = 0; int first = 1;
int frames = 0, drops = 0, enodata = 0;
double t0 = walltime(0), t_end = t0 + cfg.duration_s;
while (walltime(0) < t_end && !g_stop) {
    int r = next_frame(sock, cfg.next_timeout_s, &seq, &tmp, &per, buf, nbytes);
    if (r == 1) { enodata++; msleep(5); continue; }
    if (r < 0) { strcpy(row->err,"recv fail"); break; }
    if (!first && seq > last_seq+1) drops += seq - last_seq - 1;
    last_seq = seq; first = 0; frames++;
}
double elapsed = walltime(0) - t0;

cleanup:
stop_stream(sock);
row->frames = frames; row->elapsed = elapsed;
row->fps = (elapsed>0) ? frames/elapsed : 0;
row->expected_fps = 1.0/exptime;
row->efficiency_pct = 100.0 * row->fps / row->expected_fps;
row->bytes_per_frame = nbytes;
row->mbps = (elapsed>0) ? (double)frames*nbytes/elapsed/1.0e6 : 0;
row->drops = drops; row->enodata_count = enodata;
row->w = w; row->h = h; row->bits = bits_actual;
row->exptime = exptime; row->bin = bin;
```

### CLI flags

```
--host HOST           default "localhost"
--port PORT           default 52311
--exptimes CSV        default "0.001,0.005,0.01,0.05,0.1,0.5,1.0"
--bins CSV            default "1,2,4"
--bits CSV            default "8,16"
--duration SEC        default 10.0
--warmup SEC          default 3.0
--next-timeout SEC    default 2.0
--gain N              optional
--offset N            optional
--csv PATH            optional
-v / --verbose        per-frame stderr logging (seq, Δt)
-h / --help
```

### Sample output

```
ZWO benchmark   host=192.168.1.42:52311   duration=10.0s   warmup=3.0s
camera: ZWO_ASI294MM  4656x3520  cooler=1 color=0 bitDepth=14

+---------+-----+------+------+------+--------+---------+--------+---------+-------+-------+-------+---------+
| exptime | bin | bits |   W  |   H  | frames | elapsed |  fps   | expFPS  | eff%  | drops|enodata|  MB/s   |
+---------+-----+------+------+------+--------+---------+--------+---------+-------+-------+-------+---------+
|  0.001  |  1  |   8  | 4656 | 3520 |    42  |  10.02  |   4.19 | 1000.00 |   0.4 |    0  |  198  |  68.7   |
|  0.010  |  1  |   8  | 4656 | 3520 |    91  |  10.01  |   9.09 |  100.00 |   9.1 |    2  |    0  | 149.0   |
|  0.010  |  2  |   8  | 2328 | 1760 |    98  |  10.00  |   9.80 |  100.00 |   9.8 |    0  |    0  |  40.1   |
|  0.100  |  4  |  16  | 1160 |  880 |    99  |  10.02  |   9.88 |   10.00 |  98.8 |    0  |    0  |  20.2   |
| ...                                                                                                       |
+---------+-----+------+------+------+--------+---------+--------+---------+-------+-------+-------+---------+

/* TODO: future work — sweep ASI_BANDWIDTHOVERLOAD once zwoserver.c exposes a
 *       command to set it (currently only ASI_EXPOSURE/GAIN/OFFSET/COOLER are wired). */
```

### makefile

Modeled on [src/server/makefile](src/server/makefile). Reuses the server's already-built object files for tcpip/utils/ptlib so we do not duplicate source:

```makefile
OPT     = -O2
CC      = gcc
CFLAGS  = -DLINUX -Wall -I../server
LIBS    = -lm -lpthread

Obench  = zwo_benchmark.o ../server/tcpip.o ../server/utils.o ../server/ptlib.o

all:    zwo_benchmark

zwo_benchmark: $(Obench)
	$(CC) -o $@ $(Obench) $(LIBS)

zwo_benchmark.o: zwo_benchmark.c ../server/tcpip.h ../server/utils.h
	$(CC) $(CFLAGS) $(OPT) -c zwo_benchmark.c

clean:
	rm -f zwo_benchmark *.o
```

(The server makefile must have been run once — or we add a dependency rule — so that `../server/tcpip.o` etc. exist. Add a small `prereq:` rule that does `$(MAKE) -C ../server tcpip.o utils.o ptlib.o` for one-step builds.)

## Critical Files

- **Create**: [src/benchmark/zwo_benchmark.c](src/benchmark/zwo_benchmark.c)
- **Create**: [src/benchmark/makefile](src/benchmark/makefile)
- **Read / link against (do not modify)**:
  - [src/server/tcpip.h](src/server/tcpip.h), [src/server/tcpip.c](src/server/tcpip.c)
  - [src/server/utils.h](src/server/utils.h), [src/server/utils.c](src/server/utils.c)
  - [src/server/ptlib.h](src/server/ptlib.h), [src/server/ptlib.c](src/server/ptlib.c)
  - [src/server/zwo.h](src/server/zwo.h) for SERVER_PORT, MIN_EXPTIME, MAX_EXPTIME
- **Reference only**: [src/gcam/zwotcp.c](src/gcam/zwotcp.c), [src/server/zwoserver.c](src/server/zwoserver.c).

## Edge Cases / Error Handling

- `TCPIP_Recv` timeout on `next` body → record `err="recv timeout"`, `stop`+continue to next config.
- `-E...` response from any command → record `err=<msg>`; `stop`; continue.
- `-Enodata` → `msleep(5)`, increment `enodata`, retry (not counted as drop — drops come from `seq` gaps only).
- `SIGINT` → `g_stop=1`; inner loops break; current config's partial row is still printed with `err="interrupted"`; outer loop exits; socket is closed cleanly via `atexit`.
- Dimension rounding: compute `w = (W/bin) & ~7`, `h = (H/bin) & ~1` client-side to match server rounding; verify against echo of `setup` response.
- Exptime bounds: clamp to `[MIN_EXPTIME, MAX_EXPTIME]` during arg parse; warn and skip out-of-range values.
- Frame buffer: single reused `u_char*` grown with `realloc` to the largest `w*h*bits/8` across the sweep, allocated once.

## Verification

1. **Build**:
   ```
   make -C src/server tcpip.o utils.o ptlib.o
   make -C src/benchmark
   ```
2. **Dry-run against Python emulator** (starts from any machine, no hardware):
   ```
   python src/py/zwo_emulator.py --port 52399 &
   src/benchmark/zwo_benchmark --host localhost --port 52399 \
       --duration 3 --warmup 1 --exptimes 0.01,0.1 --bins 1 --bits 8 -v
   ```
   Expect a two-row table; fps close to emulator's cap or `1/exptime`; `drops=0`.
3. **Ctrl-C mid-run** → confirm last row labelled `interrupted`, emulator log shows clean `stop`+`close`.
4. **CSV**: add `--csv out.csv`; confirm row count equals configs and columns match the ASCII table.
5. **Real hardware** (when available):
   ```
   src/benchmark/zwo_benchmark --host <rig> --port 52311
   ```
   Expect `eff% ≈ 100` at long exptimes, clear efficiency drop at 1 ms (readout/bandwidth-limited), non-zero `drops` only under network/disk pressure.

## Execution Order

1. **First**: create a new branch off `main` named `plan/zwo-benchmark`, mirror this plan into the repo at `docs/plans/zwo-benchmark.md` on that branch, and commit it as a standalone commit (message: `plan: add zwo server fps benchmark`). The plan is committed on a dedicated plan branch — never on `main` and never on the current working branch.
2. Then, per the per-step-branch rule, create further branches for implementation (e.g. `plan-zwo-benchmark/step-01-scaffolding`, `step-02-measurement-loop`, `step-03-table-csv`): scaffold `src/benchmark/zwo_benchmark.c` + `src/benchmark/makefile`, add the measurement loop, then add table/CSV output. Build and verify with the emulator steps above after each step.
