# EDS fixtures for gcamzwo's command poll

Raw records from the Baade EDS archive (`~mag1op/public_html/log/log<UTC stamp>` on
`alpaca`, written by `magsoft/eds/edslog`), one per line, exactly as `tcsif` delivers them,
with no comments inside the files. The record layout is in
[src/gcam/edstail/README.md](../../gcam/edstail/README.md): columns 15-17 the type (`802`
the rotator of interest, column 51; `81w` a command echo from sender `w`, the command in
columns 20-41). `gcmd_bench.py` (one directory up) turns them into the answers tcsis gives
a camera that keeps up and feeds those to `gcamtail_test`, and feeds the records to
`tcsis_sim.py`, which builds tcsis's ring from them as magsoft's `edsshm` does, for the
real poll; it derives what the camera must do straight from the records (`make test` in
`src/gcam`).

`gcmd_exchanges.txt` is not a night: it is the table of gcmd exchanges of imacs
`docs/plans/guider-architecture.md`, Appendix A, which both copies of the client (this
repository's `src/gcam/gcmd.c` and imacs `src/gcmd.c`) run in their benches.

The night files keep, in the original order, every `Z81x` echo whose command starts with
`g` (`ga`, `gb`, `gc`, `gp`), `slew` or `cnca`, and only those `Z802` records where the
rotator of interest changes. They were cut from four days of logs (2026-10-10 to 10-12
and 10-20, 5262 files) with

```
cat log<YYYYMMDD>* | LC_ALL=C awk '
  substr($0,1,3)=="~Z2" && substr($0,16,3)=="802" { r=substr($0,52,1); if (r!=last) {print; last=r}; next }
  substr($0,1,3)=="~Z2" && substr($0,16,2)=="81" &&
    (substr($0,21,1)=="g" || substr($0,21,4)=="slew" || substr($0,21,4)=="cnca")'
```

The camera echoes all come from the network channel (sender `4`, `tcsis` clients); the
`slew`s come from sender `4` and from the console (`0`).

## eds_20261010_llamas_aux2.txt (2026-10-10 UT)

LLAMAS at AUX2 (rotator 4) all night, then the daytime changeover to IMACS (rotator 0)
at 14:21:13 UT. 46 `ga` echoes (`gafone` x24, `gafthr` x22), 44 `gb` (`gbfone` x24,
`gbfthr` x20), 44 `gp` (LLAMAS's dithers and IMACS's probe verbs), and 10 `slew`s: five
at rotator 4 (three from the network, two from the console) and five console slews at
rotator 0 in the afternoon.

| camera | port | expected |
|---|---|---|
| 1 | 4 | `fone` x29 (24 echoes, 5 slews) and `fthr` x22, then EXIT at the changeover |
| 2 | 4 | `fone` x29, `fthr` x20, then EXIT at the changeover |
| 1 | 4, `portexit 0` | the same commands, no exit; nothing after the changeover (tcsis has no echo for port 4) |
| 1 | 0 | the first rotator heard (4) does not exit; rotator 0 from 14:21; `fone` x5 from the afternoon slews |

## eds_20261012_mage_imacs.txt (2026-10-12 UT)

MagE at AUX3 (rotator 5) until 05:25:30 UT, then IMACS at rotator 0. While the rotator is
5: seven `gafone`/`gbfone`/`slew` triples and 21 `gp` echoes from the GCAM2 link (sender
`2`). At rotator 0: 19 `cnca` (3 for camera 1, 16 for camera 2), `gaxys 5` x5,
`gbxys 5` x3, `gafone`/`gbfone` x4, `gafthr`/`gbfthr` x3, and IMACS's `gp` verbs.

| camera | port | expected |
|---|---|---|
| 1 | 0, `cnca 1` | `xys 5 …` x5, `ca <deg>` x3 (`-30.925`, `-32.949`, `-43.470`), `fone` x4, `fthr` x3; `cnca 2` ignored |
| 2 | 0, `cnca 1` | `ca <deg>` x16, `xys 5 …` x3, `fone` x4, `fthr` x3 |
| 1 | 0, `cnca 0` | the same without the `ca`s |
| 1 | 4 | nothing: 5 to 0 is a change between two other ports, so no exit either |
| 3 | 5 | `fone` x7 from the slews, then EXIT at the changeover. In production MagE's slit viewer does not poll, and no `gc` echo reaches Baade's probe c |

## eds_20261020_fire_noimacs.txt (2026-10-20 UT)

FIRE at AUX1 (rotator 3). A negative fixture with foreign records on purpose: one record
of each other TCS type of the day (`801`, `804`-`806`, `809`, `820`-`826`), one `802`
record from each of six other subsystems (`J`, `N`, `O`, `L`, `I`, `H`), the one `Z802`,
and all sixteen command echoes of the day (`autf`, `az`, `dome`, `dut`, `h`, four console
`slew`s); no camera echo.

| camera | port | expected |
|---|---|---|
| 1 | 0 | nothing |
| 1 | 3 | `fone` x4 from the slews |

## eds_synth_gap_port0.txt, eds_synth_gap_port4.txt

Real records, regrouped for the `port -> gap -> other rotator` rule. `port0`: the
2026-10-12 changeover to rotator 0 and the next four camera-1 echoes there (`gaxys 5` x2,
`cnca 1 -30.925`, `gafone`). `port4`: the first 2026-10-10 record (rotator 4) and
`gafone`, `gbfone`, `gafthr`. Fed to `tcsis_sim.py` one after the other, with tcsis
away between them, the rotator leaves 0 and comes back as 4.

| camera | port | expected |
|---|---|---|
| 1 | 0, `cnca 1`, polled | the four commands, then EXIT when tcsis answers rotator 4 |
| 1 | 0, `cnca 1`, `portexit 0`, polled | the four commands; nothing of pass 2, no exit |
