# gcam: web guider display — Plan

**Status:** Parts 1 and 2 implemented in [`src/web/`](../../src/web/)
and verified on the emulator rig (2026-08-29); mountain trial pending.
The image server this builds on (PR #22, see
[gcam-image-server.md](gcam-image-server.md)) is **merged** and ships
in v1.1, so the wire contract coded against here is the released one.

**Update (2026-09-17):** PR #35 is trimmed to the proxy alone — `gcamweb` serves `ws`, `status`, the JSON guider list, and the runtime `every`/`roi` setters (source-level, so shared by a guider's viewers; per-client versions belong in chz1's per-connection `config`, which is an astro-ph change). The viewer is being rebuilt in carnegie-observatories/lco-instrument-web — see its `docs/plans/guider-viewer-plan.md`; the Part 2 page that used to live here was its prototype.

## What this is

A WebSocket bridge and a browser app that show the guider live, built
in **two parts**:

- **Part 1 — MVP:** the image, and nothing else. A bridge that pulls
  frames from gcam and a minimal page that paints them. No FITS
  metadata in the browser, no status, no panels.
- **Part 2 — the guider layer:** the full viewer (imexam, colormaps,
  histogram), guider status synchronous with each frame, the guide
  box and centroid drawn on the image, strip charts.

```
zwoserver (rPi :52311)
  └─ guider host ──────────────────────────────────────────────┐
     gcam — guides, shifts >>2, averages                       │
       └─ image server localhost:52300+gnum   "fits [timeout]" │
            └─ bridge  src/web/server.py — FITS → CHZ1, aiohttp│
  ─────────────────────────────────────────────────────────────┘
                 └─ ws://guiderhost:8765  ← the only port exposed
                      └─ browser  src/web/
```

**The bridge runs on the same machine as zwogcam.** gcam's image port
never needs to be reachable from outside the guider host (it can stay
bound or firewalled to localhost); the only thing opened to the LAN is
the bridge's single HTTP/WS port. This also resolves the inbound-
gating question the image-server plan flagged.

Two reference codebases are reused rather than reinvented, both from
the [astro-ph monorepo](https://github.com/astro-ph-labs/astro-ph)
checkout at `/Users/william/workspace/astro-ph-labs/astro-ph`
(a path dependency for now, consumed from the registry later — see
*Dependencies*):

- **chz1** — the streaming wire format for 16-bit astronomical images:
  a Python encoder + reference aiohttp server, and a JS client with a
  WASM decoder. Lossless and lossy tiers, both measured.
- **core + viewer** — the browser FITS/stream viewer as a library,
  split since the monorepo move: `@astro-ph-labs/core` (view
  transform, imexam, overlays — the maths) and `@astro-ph-labs/viewer`
  (WebGPU renderer, plain-DOM panels). Used in Part 2 only; both ship
  built JS under `dist/` (`npm run build` in the checkout).

## Why a bridge process, and why outside gcam

gcam already solved its half: the image server (port `52300+gnum`, one
verb, ≤4 clients) serves "the frame the guider used" with the entire
guider + telescope state in the FITS header, filled by the same
`guider_state()` that fills the `status` reply, so the two cannot
drift. The browser side needs WebSockets, zstd, JSON and COOP/COEP
headers — none of which belong in a C/X11 application.

So the bridge is a **pure client** of the pull port, exactly what that
port was designed for:

- gcam sees **one** image client regardless of how many browsers are
  connected — the 4-client cap and the camera LAN are insulated from
  the viewers.
- **Pull end to end.** The bridge self-paces against gcam
  (`fits <timeout>` returns when a newer frame exists); browsers
  self-pace against the bridge (CHZ1's credit flow: a frame is sent
  only against an `ack`). A slow browser never stalls gcam.
- No gcam changes. If the PR's protocol or keyword spellings move in
  review, only the bridge follows.

**The bridge never touches the command port** (`52200+gnum`). That
port serves one connection at a time and is used by other software —
a bridge holding or even periodically grabbing it could lock
operations tooling out. Everything the bridge knows, it learns from
the image port.

## Compression: what to send a guider viewer

The frames are 16-bit, noise-dominated, and only *displayed* — lossy
is acceptable. Options considered:

**1. Re-stretch to 8-bit + browser-native codec (JPEG/WebP/AVIF).**
Simple to decode (an `<img>` tag), but the server must choose the
stretch, so the client can never re-stretch or read pixel values; and
on noise-dominated astronomical data DCT codecs perform poorly.
chimera's fits-bench codec comparison (the benchmark chz1's defaults
came from) found chz1's quantized tier **beats WebP-q75 and JPEG-q75
at comparable quality and encodes ~27× faster than AVIF** — noise
gives a DCT nothing to model. Rejected.

**2. chz1 lossless** — pedestal filter + byte-shuffled planes + banded
zstd. 2.98× on dark full-well frames, 7.08× measured on typical sky.
Bit-exact 16-bit ADU in the browser: client-side stretch, real pixel
readout. This is the tier a second, "inspection-grade" consumer gets
by default (a client that doesn't opt in to lossy always receives
lossless — the protocol's `accept` handshake guarantees it).

**3. chz1 lossy tier: bin, then quantize (the recommendation for
display).** Box-average `bin×bin`, then dithered quantization with a
step chosen as a fraction `q` of the *binned* frame's measured noise
σ. Stays 16-bit linear ADU — the client still re-stretches and still
measures — and the added noise is known and carried in the frame
header. chz1's measured reference (9600×6422 frame): 123.3 MB raw →
1.09 MB at `bin 4, q 0.5` (+14.2 % noise), 1.42 MB at `q 1` (+3.7 %).

For gcam's frames, display is the sizing anchor: the X11 GUI shows
MIKE/MagE's 1512² frames at 504² — **bin 2 (756²) exceeds what the
current GUI displays**, and bin 1 stays available for a full-res look.
Per-frame wire sizes, **measured through the bridge on 1512² emulator
frames** (Poisson noise + one Gaussian star — real sky with structure
will compress somewhat less at the lossless tier; re-measure on the
mountain):

| tier | 1512² measured | vs raw 4.57 MB | at 5 Hz | 1000² (PFS, scaled) |
|---|--:|--:|--:|--:|
| lossless | 1.23 MB | 3.7× | 49 Mbit/s | ~0.54 MB |
| bin 2 + q 0.5 (step 4 ADU) | 0.16 MB | 29× | 6.4 Mbit/s | ~0.07 MB |
| bin 4 + q 0.5 (step 2 ADU) | 0.04 MB | 115× | 1.6 Mbit/s | ~0.02 MB |

5 Hz is gcam's own display throttle; typical guide exposures (0.1–1 s)
are slower still. Even lossless fits the instrument LAN — the lossy
tier is what makes remote/VPN viewing comfortable.

The mechanism is negotiated at runtime (`config` message:
`accept: ["bin","quant"], bin, q, dither`), so this is a **default in
the bridge CLI plus a toggle in the page**, not an architectural
choice. Two more knobs sit beside the tier, both cut in the bridge before
encoding so the bytes never leave the machine: a **frame stride**
(`--every N`) and a **centre crop** (`--roi N`, the central 1/N of the
side — ½ is a quarter of the data, ⅛ a sixty-fourth; the offset
travels in the frame header so the guide box stays put). Measured on
the 1512² emulator frame at bin 2 + q 0.5: full 0.16 MB, ½ 0.04 MB,
¼ ~9 KB, ⅛ ~7 KB per frame. One correction found in Part 1: chz1's
reference server keeps the operating point **per process** (it mutates its `args`), so a
second browser changing the tier changes it for everyone, and frames
are handed to one client each (N browsers share the frame rate).
*(Fixed since — 2026-08-30: the shared transport generalization made
for lco-instrument-web's imageweb landed here too, and has since been
**upstreamed into chz1 itself** as `chz1.stream` — so the bridge
imports it rather than carrying a copy. It builds a pipeline +
settings clone per connection and `GcamSource.get` is seq-keyed, so
tiers are per client and every browser sees every published frame.
Both repos now consume the one upstream module — see
lco-instrument-web/docs/plans/image-viewer-plan.md § Shared
transport.)*

---

## Part 1 — MVP: an image in a browser

Only two files of ours, plus the checkouts.

### The bridge (`src/web/server.py`)

chz1's reference server (`chz1/python/server.py`) already does almost
all of the job: banded zstd encoding, the lossy tier, per-client
credit flow, COOP/COEP isolation headers, and a composable
`build_app()` with exactly one route (`GET /ws`). Its only
file-specific part is `FrameSource`, which reads FITS from a glob.
The bridge replaces that class and adds static routes; the rest is
imported from chz1's checkout (see *Dependencies*).

`GcamSource` — lift the ~40-line protocol client from
[gcamclient.py](../../src/py/gcamclient.py) (`fits <timeout>` →
`"<seq> <ts_ns> <nbytes>\n"` + FITS). One reader task:

- Connect to `localhost:52300+gnum`. Loop `fits 2.0`; `-Enodata` →
  poll again. The timeout form returns only frames newer than the
  last served on this connection, so the loop runs at the guider's
  frame rate with no busy-wait.
- `-EZWO not acquiring` / connection loss → retry with backoff.
  **Disconnect from gcam when no browser is connected** — it frees
  one of the 4 client slots and costs one reconnect on the first
  viewer.
- Parse with astropy: `BITPIX=16` big-endian with `BZERO=32768` comes
  back as `uint16` directly, which is what `chz1.pedestal.Encoder`
  eats. In Part 1 the FITS header is used **only** for geometry —
  nothing from it is forwarded to the browser.

Pixel semantics are inherited and documented, not hidden: served
pixels are `>>2`-shifted and, when averaging is on, rolling-averaged
([zwotcp.c:433-520](../../src/gcam/zwotcp.c#L433-L520)).

Static routes: `/` → the page; `/pkg/chz1/` → a mount of the
monorepo's `packages/chz1/` (the page reaches its `ts/src/`). CLI:
`--gnum` (gcam at `localhost:52300+gnum`; `--gcam-host`/`--gcam-port`
exist for the emulator rig), `--listen`, `--astro-ph`, and the encoder
knobs (`--bands`, `--level`, `--bin`, `--q`).

### The page (`src/web/index.html` + `app.js`)

chz1's example page is already this app: connect button,
`Chz1Stream` + `loadWasm` + pooled decode workers, 2D-canvas paint
with a percentile stretch, mandatory `ack`. Adapt it — same import-map
no-build pattern, auto-connect to `ws://<self>/ws`, plus a lossy-tier
toggle (`stream.configure({bin, q})`). Deliberately **no WebGPU and no
viewer dependency** in Part 1: it renders anywhere, and it keeps the
MVP to one moving part.

Two things import maps cannot reach get explicit URLs:
`new Worker("/pkg/chz1/decode-worker.js")` (workers ignore import
maps) and `loadWasm("/pkg/chz1/pkg/decoder.wasm")`.

### Part 1 staging

1. ~~`GcamSource` + `build_app()` against the emulator rig~~ **Done.**
   `server.py` imports chz1's `Pipeline`/`ws_handler`/`isolation_headers`
   from the checkout and injects `GcamSource` (which imports
   [gcamclient.py](../../src/py/gcamclient.py) rather than copying it).
2. ~~The adapted page~~ **Done.** `index.html` + `app.js`: import map
   over `/pkg/chz1/`, pooled WASM decode, canvas stretch, tier and
   worker selects (also `?tier=`/`?workers=` URL params), single-timer
   reconnect with backoff.
3. ~~Measure~~ **Done on emulator frames** (table above); the default
   is bin 2 + q 0.5. Repeat on real frames when a camera is available.
4. Try it on the mountain: bridge next to a live gcam, browsers on
   the ops LAN. This is the end of Part 1 — usable on its own.

Two things learned the hard way, for the next client author:

- The ack's `client` field must be a JSON **object**. chz1's server
  dict-updates it before releasing the credit, and its reader dies
  (without setting its stop flag) on a bare number — which is what
  `Chz1Stream.finished()` returns and what chz1's own example sends.
  The symptom is exactly two frames, then silence, and a handler that
  never exits. Worth an upstream issue on chz1.
- A reconnect scheduled per `status` event doubles the socket count
  every cycle (a failed socket fires both `error` and `close`; closing
  the previous socket fires `close` again). That storm, with a tab
  open across a bridge restart, is the likely cause of a 39 GB
  out-of-memory reboot during development. The page now arms one
  timer at most and detaches the old socket's handlers first.

---

## Part 2 — the guider layer

### Design rule: display verbatim, derive nothing

The viewer knows nothing about the system it is viewing. Neither the
page nor the bridge computes physical or astronomical quantities —
no alt/az from ZD, no absolute centroid from box + offset, no unit
conversions beyond formatting. Every displayed number is a served
header value, shown under its served meaning. When the display needs
a quantity that gcam doesn't serve, the answer is an **issue against
zwogcam** to add it to `guider_state()` / the FITS header — never
client-side arithmetic. (Pure display geometry — pan/zoom, and
scaling overlay coordinates by the header's `bin` factor — is the
viewer's own transform, not system knowledge.)

### Status rides in the frame header

CHZ1's wire is deliberately frames-only, but the per-frame JSON header
tolerates unknown keys (`protocol.md` §"unknown keys are ignored"), so
no format bump is needed. The bridge maps the FITS header into:

- the standard keys chz1 defines: `seq`, `name`
  (`"gcam<g> <SEQ-NUM>"`), `context` (dateobs, exptime, airmass,
  telescope — copied verbatim from header cards, nothing derived;
  keys with no served source stay absent);
- one new top-level `guider` object carrying everything: the 27
  guider cards (`GDINIT … GDSENS`, `FRAMETS`) plus temps, gain and
  the TCS block (`RA DEC AIRMASS TELFOCUS ROTANGLE GUIDERX1/Y1 …`) —
  a 1:1 rename of served cards, no synthesis.

The viewer forwards the raw header with every frame event, so the UI
gets status **exactly synchronous with the displayed image** — no
correlation problem, no second poll while frames are flowing.

### `/status` — the channel for when frames stop

When the loop stops, frames stop, and a header-only design would just
freeze. A second endpoint `GET /status` (WebSocket, JSON text, ~1 Hz
broadcast) carries what the bridge knows without a frame — all of it
derived from the image port alone:

```jsonc
{ "gcam": "connected" | "idle" | "unreachable",
  "last_seq": 12345, "last_ts_ns": …, "age_s": 4.2 }
```

While the loop is stopped the UI shows "idle since …" plus the last
frame's status, greyed — not live temperatures. If live idle-state
telemetry is ever wanted, the right mechanism is a gcam-side change
on the image port, not a command-port poll; noted under open
questions.

### The full viewer assembly

Swap the MVP's canvas paint for `@astro-ph-labs/core` +
`@astro-ph-labs/viewer` (add `/pkg/core/` and `/pkg/viewer/` mounts
and import-map entries resolving to their built `dist/`;
`@astro-ph-labs/chz1` must stay mapped even though the page stops
importing it directly — the viewer's stream source reaches for its
`protocol.js`):

- `ViewerClient` (viewer's worker facade) would be the cleanest API,
  but its worker imports bare specifiers, and module workers don't
  inherit import maps — that path forces an esbuild bundle (viewer's
  own examples say so). Instead the app composes on the main thread,
  which viewer's example already demonstrates and which needs no
  build: `createRenderer` on a main-thread canvas (WebGPU),
  `setGeometry`/`uploadPixels`/`draw`, `mountViewerChrome` +
  `mountBar`/`mountControls`/`mountInspector`/`mountHistogram`,
  imexam, `overlay.css`. Decoding stays in the pooled workers; at
  guider rates main-thread coordination is far from any limit. If it
  ever measurably drops interactions, the upgrade path is the bundled
  worker — an optimization, not a redesign.

### The guider panel (the new code)

Fed by the `guider` object arriving with every frame and by `/status`:

- **Readouts** — guiding state (`GDGUIDE`: off / F2–F5 / correction
  pending), `gm/fm/mm` modes, exptime, gain, `av`, FWHM ["],
  flux/peak/back [ADU], dx/dy [px], az/el corrections ["], CCD temp /
  setpoint / cooler %, camera and guider-loop fps, frame seq + age.
  Stale data must look stale: photometry only updates while guiding,
  and temps freeze when the cooler is off — grey out on `age_s` and
  on `loop=0`.
- **Overlay** — viewer's overlay shapes, drawn only from served
  fields: the guide box (center `GDBOXX/GDBOXY`, side `GDBOXSZ`)
  verbatim. The measured-centroid marker needs an **absolute**
  position, and gcam serves only the offset (`GDDX/GDDY` relative to
  the box, [guider.c:249-250](../../src/gcam/guider.c#L249-L250)) —
  per the design rule the client does not add them. **File a zwogcam
  issue to serve the absolute centroid** (e.g. `GDCENX`/`GDCENY` from
  `guider_state()`) when Part 2 starts; until it is served, the
  overlay shows the box only and the panel shows dx/dy as numbers.
  Overlay coordinates scale by the header's `bin` factor
  (`src_w/src_h`) — display geometry, allowed.
- **Strip charts** — flux, FWHM, az, el vs time: the four X11
  `GraphWindow` plots, as small canvas sparklines fed by frame
  headers. History lives in the page (the X11 ring buffers are not
  exported, and don't need to be).

One bridge process serves every guider of a telescope, each as an
aiohttp sub-application under `<prefix>/<name>/` (default prefix
`/guider`), with a landing page listing them at `<prefix>/`. Names
follow the deployed `.ini` files and camera hosts —
`gcam<rotator port><guider>`: `gcam41`, `gcam12` — so a URL says which
camera it is the way the rest of the system does. The pages
use only URLs relative to their own directory — import map, worker and
wasm URLs, both sockets, the settings endpoints — so the prefix is the
server's business and the whole thing sits behind a reverse proxy or a
Cloudflare Tunnel whose ingress rule is just `path: ^/guider(/.*)?$`,
beside the instrument SPA on the same hostname. Tier, stride and crop
are per guider (each sub-app has its own encoder pipeline and pump).
This replaces the plan's earlier `?host=&gnum=` idea: the bridge, not
the page, knows which guiders exist.

### Part 2 staging

5. ~~Header `guider` object~~ **Done**, and more literal than planned:
   rather than a curated rename, the bridge forwards **every non-
   structural FITS card verbatim** (`guider.cards`) together with
   gcam's card comments (`guider.comments`), so the page labels each
   number with the meaning gcam wrote — no client-side vocabulary at
   all. Integers beyond 2⁵³ (`FRAMETS`) travel as strings. Appended
   after chz1's pipeline builds the message (a `Pipeline` subclass),
   so chz1's encoder is untouched.
6. ~~Viewer assembly~~ **Done** as `index.html`/`app.js`: viewer's
   example assembly with the file path replaced by the stream (main-
   thread WebGPU renderer, pooled CPU decode, `uploadPixels`), with
   the panes, bar, controls, imexam in all five modes, histogram and
   `?`. (The Part 1 canvas page was retired once the viewer proved
   out; a browser without WebGPU gets a clear message.) Headless
   Chrome with WebGPU passes; the control-room browsers still need
   the check.
7. ~~Guider panel, overlay, sparklines, stale-greying~~ **Done**
   (`guider-panel.js`): grouped readouts, the guide box on the frame
   (amber while `GDGUIDE ≠ 0`, dashed otherwise; note gcamzwo's main
   window draws the *magnifier footprint* there, not this box), four
   strip charts,
   greying keyed on the served `GDGUIDE` flag and on frame age.
8. ~~`/status` + lifecycle~~ **Done**: JSON once a second from the
   image-port state only; the panel shows "frames stopped — gcam:
   <state> · last frame N s ago" when the stream stalls.
9. Fit and finish — **partly done**: multi-guider sub-apps under a
   prefix, relative URLs throughout, `wss:` behind https, and the
   Cloudflare Tunnel ingress documented in the README. **Remaining**:
   a `docs/ZWO/` section beside the zwogcam docs, the ansible service
   unit, and the mountain trial.

Found while building Part 2, to settle on a real frame:

- **Coordinate convention of `GDBOXX/GDBOXY`.** The box is drawn at
  the served coordinates divided by the bin factor, assuming they are
  0-based pixel positions in the same row order as the FITS data. gcam
  keeps them as qltool cursor positions; if its row origin is the top
  of the buffer while FITS rows run bottom-up, the box will appear
  mirrored vertically. The emulator's star sits at the centre, so it
  cannot tell; one real frame with the star off-centre will. If they
  differ, the fix is a served convention (a card saying so, or gcam
  writing FITS-convention values) — not a flip in the page.
- **`GDFWHM` is served in pixels** (its card comment says so), not
  arcsec as the X11 GUI displays — another reason the labels come
  from the served comments rather than from the plan's table.

## Dependencies

All consumed from the astro-ph monorepo checkout at
`/Users/william/workspace/astro-ph-labs/astro-ph` (uv path dependency
for Python, static mounts of `packages/*` for the browser):

- **Python**: `src/web/` is a uv package (`gcamweb`, `uv sync` /
  `uv run gcamweb`), with `chz1` as a path dependency
  (`[tool.uv.sources]`) for the encoder *and* the transport. The
  streaming server originally had to be vendored here — chz1 shipped
  only `pedestal.py`/`fits.py`, so a copy of its `python/server.py`
  (MIT) lived in `gcamweb/stream.py`. That copy, with the two bugs
  found here fixed, was **merged upstream**: the bridge now does
  `from chz1.stream import Settings, isolation_headers, ws_handler`
  (and `Frame`), and carries no transport code of its own.
- **Browser**: no package management in the bridge — it mounts the
  monorepo's `packages/{chz1,core,viewer}` and the import map resolves
  them (chz1's plain `ts/src`, core/viewer's built `dist` — one
  `npm install && npm run build` in the checkout). The checkout
  location is one `--astro-ph` flag.
- Deployment therefore means: clone `zwo` + the `astro-ph` monorepo on
  the guider host, and build its JS once.

The local checkout is the interim source; the packages will be
consumed **from the monorepo's registry/repo when it becomes public**
(it is private today, so deployment hosts would each need a deploy
key). The switch, verified 2026-08-30 (pre-monorepo, mechanics
unchanged):

- **Python — ready now**: `[tool.uv.sources] chz1 = { git = "…",
  rev = "<sha>" }` resolves and locks (tested over SSH against the
  private repo); it becomes a plain dependency if chz1 is published.
- **JavaScript — needs one of two things**: uv installs Python
  packages only, and the monorepo does not ship its JS in a wheel
  (chz1's wheel is deliberately `pedestal.py`/`fits.py` only). Either
  upstream adds the JS to its wheels (then gcamweb mounts
  `importlib.resources.files("chz1")/"ts"` and everything arrives
  through `uv sync`, pinned in one lock — the clean end state), or
  gcamweb grows a `fetch-js` that shallow-clones the monorepo at a
  pinned rev into `~/.cache/gcamweb/` and builds it (needs git + npm
  on the host). `--astro-ph` then becomes a development override.

Licensing note for later packaging: chz1's JS/Python and viewer are
MIT, but the shipped `decoder.wasm` is built from a **GPL-2.0-or-later**
Rust crate. Irrelevant for in-house ops tooling; relevant if this ever
ships externally.

## Placement

`src/web/` is right, and consistent with this repo's convention of
per-language/component dirs under `src/` (`gcam`, `server`, `py`, …).
Bridge and app stay **together** — the bridge serves the app, they
share a deploy unit (the guider host), and splitting them would only
add a path to configure. Flat inside, like lco-instrument-web:

```
src/web/
  pyproject.toml           # the gcamweb package; chz1 as a path dependency
  gcamweb/server.py        # CLI, per-guider sub-apps, landing page
  gcamweb/gcam.py          # image-port client + frame source
  gcamweb/static/          # index.html, app.js, guider-panel.js, viewer.css
  README.md
```

The credible alternative is a separate repo (the lco-instrument-web
precedent: browser surface for the Cocoa apps lives outside them).
That's the right move **if** this grows its own users and release
cadence — mechanical later, premature now, and the bridge's coupling
to the gcam wire contract argues for co-locating with it while that
contract is young. Vendoring the JS into `external/` (where the ZWO
SDKs live) is rejected: these are first-party, actively developed
checkouts, not frozen third-party drops.

## Open questions

- **Default lossy tier** — bin 2 or bin 4, and which `q`? Decided by
  Part 1 staging step 3's measurements on real guider frames, viewed
  side by side.
- **WebGPU availability** on the actual control-room browsers
  (Part 2, step 6). The Part 1 canvas page is the permanent fallback
  either way.
- **Live status while idle** — v1 shows "idle since …" with the last
  frame's greyed status, because the command port is off-limits (one
  client, used by other software). If live idle telemetry is ever
  needed, it should come from a gcam-side addition on the image port
  (e.g. a `status` verb there, answered from `guider_state()` without
  touching `command_msg`) — a small, separate PR.
- **gcam-side additions** (each a small, separate issue/PR against
  zwogcam, per the display-verbatim rule): absolute centroid
  (`GDCENX`/`GDCENY` — needed for the Part 2 overlay marker);
  `q_flag` (fit quality 0/1/2, today only colors a GUI box — fit
  health in the panel); anything else the panel turns out to need
  that would otherwise tempt client-side derivation.
- **Multi-guider view** — one page showing both Clay/Baade guiders
  (one bridge each) is a composition question for later; the
  `?host=&gnum=` form keeps it open.
