# gcamweb — the gcam web guider

Browser display for gcam's live frames and guider state. A Python bridge
pulls FITS from gcamzwo's image server (port `52300+gnum`, the
`fits [timeout]` verb), streams the frames as [CHZ1](https://github.com/astro-ph-labs/astro-ph)
over a WebSocket, and serves the viewer built on
[@astro-ph-labs/viewer](https://github.com/astro-ph-labs/astro-ph): WebGPU
renderer, colormaps/stretch, panner + magnifier, imexam (`x y r c d`),
histogram, plus the **guider panel** (`k`) and the guide box on the frame.
`?` lists every key. Design: [docs/plans/gcam-web-viewer.md](../../docs/plans/gcam-web-viewer.md).

## Run

Needs [uv](https://docs.astral.sh/uv/) and the
[astro-ph monorepo](https://github.com/astro-ph-labs/astro-ph) checkout
at `../astro-ph-labs/astro-ph` (relative to this repository's parent) —
`chz1` is a path dependency in `pyproject.toml`, and the JS packages
(`chz1`, `core`, `viewer`) are mounted from the checkout (`--astro-ph`
if it lives elsewhere). Build the JS once first:
`npm install && npm run build` in the checkout.

```sh
uv sync                                              # once
uv run gcamweb --guider gcam41 --guider gcam42       # gcam on localhost:52301, :52302
open http://127.0.0.1:8765/guider/
```

Frames are seq-keyed and pipelines are per client (`chz1.stream`, the
transport this bridge and imageweb grew and upstreamed): every browser
sees every published frame, a late joiner is replayed the newest one,
and the tier is per client, not per process.

One process serves any number of guiders, each `--guider gcamPG[@HOST[:PORT]]`,
named the way the deployed `.ini` files and camera hosts are
(`etc/ini/clay_gcam/gcam41.ini`, `clay-gcam41.lco.cl`): `gcam` + rotator
port digit + guider number. The guider number is gcamzwo's `-g`, which
fixes its ports, so the image port defaults to `52300+G`.

| path | |
|---|---|
| `/guider/` | the list of guiders (and `/guider/guiders.json`) |
| `/guider/gcam41/` | the viewer for that guider |
| `/guider/gcam41/ws`, `…/status` | its frame stream and status channel |
| `/guider/gcam41/every`, `…/roi` | stride and crop, `GET` / `POST ?n=N` |
| `/guider/pkg/{chz1,core,viewer}/` | the JS packages, from the astro-ph checkout |

The prefix (`--prefix`, default `/guider`) is the server's business alone:
the page uses only URLs relative to its own directory. Tier, stride and
crop are **per guider** — each has its own encoder pipeline and pump.

### Behind a Cloudflare Tunnel

The bridge runs beside gcamzwo and speaks plain HTTP on `127.0.0.1:8765`.
cloudflared forwards paths unchanged, so the ingress rule is the prefix:

```yaml
ingress:
  - hostname: sbs.chimera.observer
    path: ^/guider(/.*)?$
    service: http://127.0.0.1:8765
  - hostname: sbs.chimera.observer      # the instrument SPA (lco-instrument-web)
    service: http://127.0.0.1:8080
  - service: http_status:404
```

Behind https the page opens its sockets as `wss:` (it follows
`location.protocol`). The COOP/COEP headers the decode pool needs are set
by the bridge and pass through.

## What the guider panel shows — and the rule it follows

Every value is a FITS card gcam put in the frame header, shown under the
card's own name and gcam's own comment. The bridge forwards the cards
verbatim (`guider.cards`, `guider.comments`, plus the preamble's
`seq`/`ts_ns`) in every CHZ1 frame header; nothing is renamed, converted
or derived — not in the bridge, not in the page. The guide box is drawn
from `GDBOXX/GDBOXY/GDBOXSZ` (offset by the crop, divided by the bin
factor: display geometry). The measured centroid is **not** drawn: gcam
serves only its offset (`GDDX/GDDY`), and adding it would be client-side
arithmetic — that is an issue for zwogcam. Groups whose served flag says
they are not updating (`GDGUIDE = 0`) are greyed. The four strip charts
(`GDFLUX GDFWHM GDAZ GDEL`) are the X11 window's plots.

`status` carries what the bridge knows without a frame: its connection
state to gcam, the last frame's seq and timestamp, its age — from the
image port only. The bridge never touches gcam's command port (one
client, used by other software).

## Data on the wire

The page defaults to `bin 2 · q 0.5`: 16-bit linear ADU, binned 2×2,
dithered quantization at half the measured noise; `lossless` is
bit-exact, and a client that never sends a `config` gets lossless. Two
more knobs cut in the bridge before encoding: the *frames* stride
(`every`) and the *image* centre crop (`roi`: ½ is a quarter of the data,
⅛ a sixty-fourth). Measured on 1512² frames at bin 2 + q 0.5: full
0.16 MB, ½ 0.04 MB, ¼ ~9 KB, ⅛ ~7 KB per frame.

## Emulator rig (no hardware)

```sh
python3 ../py/zwo_emulator.py &                       # fake camera, port 52311
(cd ../gcam && ./gcamzwo -f $PWD/../../etc/ini/test.ini -h 127.0.0.1) &
uv run gcamweb --guider gcam03                        # http://127.0.0.1:8765/guider/gcam03/
```

Give gcam the ini as an absolute path (a relative one is looked up under
`$GCAMZWOINI`, default `/opt/gcamzwo`), and `-h 127.0.0.1` after `-f`
to pin the emulator. gcam starts acquiring on its own.

## Layout

```
pyproject.toml         the package (uv); chz1 as a path dependency
gcamweb/server.py      CLI, the per-guider sub-apps, landing page
gcamweb/gcam.py        gcam's image-port client and the frame source
gcamweb/static/        index.html, app.js, guider-panel.js, viewer.css

The CHZ1 transport (pipeline + credit-flow WebSocket handler) is
`chz1.stream`, imported from the astro-ph checkout (chz1's
docs/stream.md is the contract).
```
