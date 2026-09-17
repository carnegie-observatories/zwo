# gcamweb — the gcam WebSocket proxy

The WebSocket side of gcamzwo's image server. It pulls FITS frames from
gcam (port `52300+gnum`, the `fits [timeout]` verb — see
`docs/plans/gcam-image-server.md`), encodes them as
[CHZ1](https://github.com/astro-ph-labs/astro-ph) and streams them to any
number of WebSocket clients, with the guider's FITS cards forwarded
verbatim in every frame header. It serves no pages: a viewer (the
instrument SPA, or any `chz1` client) is a separate deployment.
Design: [docs/plans/gcam-web-viewer.md](../../docs/plans/gcam-web-viewer.md).

## Run

Needs [uv](https://docs.astral.sh/uv/) and the
[astro-ph monorepo](https://github.com/astro-ph-labs/astro-ph) checkout at
`../astro-ph-labs/astro-ph` (relative to this repository's parent):
`chz1` — the encoder and the `chz1.stream` transport — is a path
dependency in `pyproject.toml`.

```sh
uv sync                                              # once
uv run gcamweb --guider gcam41 --guider gcam42       # gcam on localhost:52301, :52302
```

One process serves any number of guiders, each `--guider gcamPG[@HOST[:PORT]]`,
named the way the deployed `.ini` files and camera hosts are
(`etc/ini/clay_gcam/gcam41.ini`, `clay-gcam41.lco.cl`): `gcam` + rotator
port digit + guider number. The guider number is gcamzwo's `-g`, which
fixes its ports, so the image port defaults to `52300+G`.

| path | |
|---|---|
| `/guider/` | which guiders this proxy serves (JSON; also `/guider/guiders.json`) |
| `/guider/gcam41/ws` | that guider's CHZ1 frame stream |
| `/guider/gcam41/status` | its status channel (JSON, once a second) |
| `/guider/gcam41/every`, `…/roi` | stride and crop, `GET` / `POST ?n=N` — **shared** by that guider's viewers |

The prefix (`--prefix`, default `/guider`) exists so the proxy can sit
behind a router that forwards paths unchanged — e.g. a Cloudflare Tunnel
beside the instrument SPA on the same hostname:

```yaml
ingress:
  - hostname: sbs.chimera.observer
    path: ^/guider(/.*)?$
    service: http://127.0.0.1:8765
  - hostname: sbs.chimera.observer      # the instrument SPA (lco-instrument-web)
    service: http://127.0.0.1:8080
  - service: http_status:404
```

## The stream

`/ws` speaks chz1's wire format (`chz1/docs/stream.md` is the contract):
binary CHZ1 frames out, `config`/`ack` JSON in. Frames are seq-keyed and
pipelines are per client (`chz1.stream`): every client sees every
published frame, a late joiner is replayed the newest one, and the tier
(`bin`/`q`) is per client. A client that never sends a `config` gets
lossless. Every non-structural FITS card travels verbatim in the frame
header (`guider.cards` + gcam's card comments in `guider.comments`;
integers beyond 2⁵³, like `FRAMETS`, as strings), plus the bridge's
`crop` geometry — nothing is renamed or derived, in either direction.

Two knobs cut frames before encoding, per guider: `--every N` (every
Nth frame) and `--roi N` (the central 1/N of the side — ½ is a quarter
of the data, ⅛ a sixty-fourth), settable at runtime via `POST
/guider/<name>/every?n=N` and `…/roi?n=N`. Both act at the *source* —
`every` gates the pull, `roi` crops before publish — so they are
**shared by every viewer of that guider** (`status()` reports the
current values); per-client versions belong in chz1's per-connection
`config`, like `bin`/`q`. Measured on 1512² frames at bin 2 + q 0.5:
full 0.16 MB, ½ 0.04 MB, ¼ ~9 KB, ⅛ ~7 KB per frame.

`status` carries what the proxy knows without a frame: its connection
state to gcam, the last frame's seq and timestamp, its age — from the
image port only. gcam sees **one** image client however many WebSocket
clients are connected, and the proxy disconnects entirely while nobody
is viewing. It never touches gcam's command port (one client, used by
other software).

## Emulator rig (no hardware)

```sh
python3 ../py/zwo_emulator.py &                       # fake camera, port 52311
(cd ../gcam && ./gcamzwo -f $PWD/../../etc/ini/test.ini -h 127.0.0.1) &
uv run gcamweb --guider gcam03                        # ws://127.0.0.1:8765/guider/gcam03/ws
```

Give gcam the ini as an absolute path (a relative one is looked up under
`$GCAMZWOINI`, default `/opt/gcamzwo`), and `-h 127.0.0.1` after `-f`
to pin the emulator. gcam starts acquiring on its own.

## Layout

```
pyproject.toml         the package (uv); chz1 as a path dependency
gcamweb/server.py      CLI and the per-guider sub-apps (ws, status, discovery)
gcamweb/gcam.py        gcam's image-port client and the frame source
```
