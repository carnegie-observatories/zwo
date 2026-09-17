"""gcam -> browser proxy: the WebSocket side of gcamzwo's image server.

One process serves several guiders, each under ``<prefix>/<name>/`` so the
proxy can sit behind a reverse proxy or a Cloudflare Tunnel that forwards
paths unchanged (ingress ``path: ^/guider(/.*)?$``)::

    /guider/                 which guiders this proxy serves (JSON; also guiders.json)
    /guider/gcam41/ws        one guider's CHZ1 frame stream
    /guider/gcam41/status    its status channel (JSON, once a second)
    /guider/gcam41/every     frame stride   (GET / POST ?n=N; shared by its viewers)
    /guider/gcam41/roi       centre crop    (GET / POST ?n=N; shared by its viewers)

Guiders are named as their .ini files and camera hosts are: ``gcam`` +
rotator port digit + guider number. The frames carry the guider's FITS
cards verbatim in the CHZ1 header; a viewer (the instrument SPA, or any
``chz1`` client) is a separate deployment. Design: docs/plans/gcam-web-viewer.md.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import re
from concurrent.futures import ThreadPoolExecutor

from aiohttp import web
from chz1.stream import Settings, ws_handler

from .gcam import GcamSource

log = logging.getLogger("gcamweb")

GUIDER_SPEC = re.compile(r"^(?P<name>gcam(?P<rport>\d)(?P<gnum>[1-3]))(?:@(?P<host>[^:]+)(?::(?P<port>\d+))?)?$")


def parse_guider(spec: str, default_host: str) -> tuple[str, int, str, int]:
    """``gcamPG[@HOST[:PORT]]`` -> (name, gnum, host, image_port); the port defaults to 52300+G."""
    m = GUIDER_SPEC.match(spec.strip())
    if not m:
        raise argparse.ArgumentTypeError(
            f"bad guider {spec!r}; expected gcamPG[@HOST[:PORT]], e.g. gcam41 or gcam12@clay-gcamgui2")
    gnum = int(m["gnum"])
    return m["name"], gnum, m["host"] or default_host, int(m["port"] or 52300 + gnum)


def parse_args(argv=None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--guider", action="append", metavar="gcamPG[@HOST[:PORT]]",
                    help="a guider to serve (gcam41 = rotator port 4, guider 1); repeat for several. "
                         "Default: gcam03, the emulator rig")
    ap.add_argument("--gcam-host", default="127.0.0.1", help="gcam host for guiders given without @HOST")
    ap.add_argument("--prefix", default="/guider", help="path prefix (default /guider)")
    ap.add_argument("--fits-timeout", type=float, default=2.0, help="seconds gcam waits per request")
    ap.add_argument("--every", type=int, default=1, help="serve every Nth frame (default 1)")
    ap.add_argument("--roi", type=int, default=1, help="serve the central 1/N of the frame side (default 1)")
    ap.add_argument("--host", default="127.0.0.1", help="listen address")
    ap.add_argument("--port", type=int, default=8765, help="listen port")
    enc = ap.add_argument_group("encoder (chz1)")
    enc.add_argument("--bands", type=int, default=8)
    enc.add_argument("--level", type=int, default=1, help="zstd level")
    enc.add_argument("--encoders", type=int, default=4, help="encode thread pool size")
    enc.add_argument("--inflight", type=int, default=2, help="unacked frames per client")
    enc.add_argument("--bin", type=int, default=2, help="preview bin factor (default 2)")
    enc.add_argument("--q", type=float, default=0.5, help="preview quantization, fraction of noise sigma")
    enc.add_argument("--no-dither", dest="dither", action="store_false")
    args = ap.parse_args(argv)
    try:
        args.guiders = [parse_guider(g, args.gcam_host) for g in (args.guider or ["gcam03"])]
    except argparse.ArgumentTypeError as e:
        ap.error(str(e))
    if len({g[0] for g in args.guiders}) != len(args.guiders):
        ap.error("a guider is given twice")
    args.prefix = "/" + args.prefix.strip("/")
    return args


def guider_app(name: str, gnum: int, host: str, port: int, args: argparse.Namespace) -> web.Application:
    """One guider: its own source, pipeline and settings, so the tier, stride and crop are per guider."""
    app = web.Application()
    settings = Settings(bands=args.bands, level=args.level, encoders=args.encoders,
                        inflight=args.inflight, bin=args.bin, q=args.q, dither=args.dither)
    source = GcamSource(name, gnum, host, port, args.fits_timeout, every=args.every, roi=args.roi)
    app["settings"], app["source"] = settings, source
    status_clients: set[web.WebSocketResponse] = set()

    def setting(attr: str, setter):
        # Source-level, so SHARED by every viewer of this guider: `every` gates
        # the pull before parse/encode, `roi` crops before publish. Per-client
        # versions belong in chz1's per-connection `config` (an astro-ph change).
        async def handler(request):
            if request.method == "POST":
                try:
                    setter(int(request.query.get("n", "1")))
                except ValueError:
                    raise web.HTTPBadRequest(text="n must be an integer")
            return web.json_response({attr: getattr(source, attr)})
        return handler

    async def status_ws(request):
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        status_clients.add(ws)
        try:
            await ws.send_json(source.status())
            async for _ in ws:  # the client sends nothing
                pass
        finally:
            status_clients.discard(ws)
        return ws

    async def status_broadcast():
        while True:
            await asyncio.sleep(1.0)
            for ws in list(status_clients):
                try:
                    await ws.send_json(source.status())
                except Exception:
                    status_clients.discard(ws)

    async def on_start(app):
        source.start()
        # Shared encode pool; pipelines are per client connection (chz1.stream).
        app["pool"] = ThreadPoolExecutor(args.encoders, thread_name_prefix=f"enc-{name}")
        app["status_task"] = asyncio.create_task(status_broadcast())

    async def on_stop(app):
        app["status_task"].cancel()
        await source.stop()
        app["pool"].shutdown(wait=False)

    app.router.add_get("/ws", ws_handler)
    app.router.add_get("/status", status_ws)
    app.router.add_route("*", "/every", setting("every", source.set_every))
    app.router.add_route("*", "/roi", setting("roi", source.set_roi))
    app.on_startup.append(on_start)
    app.on_cleanup.append(on_stop)
    return app


def redirect(location: str):
    async def handler(request):
        raise web.HTTPFound(location)
    return handler


def build_app(args: argparse.Namespace) -> web.Application:
    prefix = args.prefix
    root = web.Application()
    subs = {name: guider_app(name, gnum, host, port, args) for name, gnum, host, port in args.guiders}

    async def guiders_json(request):
        return web.json_response({"prefix": prefix, "guiders": [
            sub["source"].status() | {"host": sub["source"].host, "port": sub["source"].port}
            for sub in subs.values()]})

    root.router.add_get(prefix, redirect(f"{prefix}/"))
    root.router.add_get(f"{prefix}/", guiders_json)
    root.router.add_get(f"{prefix}/guiders.json", guiders_json)
    for name, sub in subs.items():
        root.add_subapp(f"{prefix}/{name}/", sub)
    return root


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    args = parse_args()
    for name, gnum, host, port in args.guiders:
        log.info("%s (guider %d): gcam at %s:%d -> http://%s:%d%s/%s/",
                 name, gnum, host, port, args.host, args.port, args.prefix, name)
    web.run_app(build_app(args), host=args.host, port=args.port, print=None)


if __name__ == "__main__":
    main()
