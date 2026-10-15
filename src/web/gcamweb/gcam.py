"""gcam's image server: the wire client and the frame source.

The server (gcamzwo, port 52300+gnum) speaks one verb::

    fits [timeout]  ->  "<seq> <ts_ns> <nbytes>\\n" + nbytes of FITS

``timeout`` waits that long for a frame newer than the last served on the
connection; 0 returns the newest at once. See docs/plans/gcam-image-server.md.
The FITS header carries the whole guider state; it is forwarded verbatim.
"""

from __future__ import annotations

import asyncio
import io
import logging
import math
import socket
import time

import numpy as np
from astropy.io import fits as pyfits

from chz1.stream import Frame

log = logging.getLogger("gcamweb.gcam")

# Cards that describe the file rather than the guider; everything else is forwarded.
STRUCTURAL_CARDS = {"SIMPLE", "BITPIX", "NAXIS", "NAXIS1", "NAXIS2", "BZERO", "BSCALE",
                    "EXTEND", "COMMENT", "HISTORY", "END", ""}


def jsonable(v):
    """A FITS card value as JSON, verbatim. Integers beyond 2**53 become strings
    (FRAMETS, nanoseconds) so JavaScript cannot round them; NaN/Undefined -> null."""
    if isinstance(v, bool):
        return v
    if isinstance(v, int):
        return str(v) if abs(v) > 2**53 else v
    if isinstance(v, float):
        return None if math.isnan(v) or math.isinf(v) else v
    if isinstance(v, str) or v is None:
        return v
    try:  # numpy scalars
        return jsonable(v.item())
    except AttributeError:
        return None


class GcamImageClient:
    """The reference client (src/py/gcamclient.py), as a class."""

    def __init__(self, host="127.0.0.1", port=52303, timeout=30.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._rx = b""

    def close(self):
        self.sock.close()

    def _readline(self) -> str:
        while b"\n" not in self._rx:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self._rx += chunk
        line, _, self._rx = self._rx.partition(b"\n")
        return line.decode("ascii", "replace").strip()

    def _read_exactly(self, n: int) -> bytes:
        out = bytearray(self._rx[:n])
        self._rx = self._rx[n:]
        while len(out) < n:
            chunk = self.sock.recv(min(1 << 20, n - len(out)))
            if not chunk:
                raise ConnectionError(f"short read: got {len(out)} of {n} bytes")
            out += chunk
        return bytes(out)

    def fits(self, timeout=0.0):
        """(seq, ts_ns, fits_bytes), or None if nothing newer within ``timeout``."""
        self.sock.sendall(b"fits %.2f\n" % timeout)
        line = self._readline()
        if line.startswith("-E"):
            if "nodata" in line:
                return None
            raise RuntimeError(line[2:])
        seq, ts_ns, nbytes = (int(f) for f in line.split())
        return seq, ts_ns, self._read_exactly(nbytes)


class GcamSource:
    """One connection to gcam, publishing the newest frame; ``get(last_seq)`` is
    seq-keyed, so every viewer sees every published frame and a late joiner is
    replayed the newest one. Disconnects from gcam while nobody is viewing,
    freeing one of its 4 client slots."""

    def __init__(self, name: str, gnum: int, host: str, port: int, fits_timeout: float = 2.0):
        self.name, self.gnum, self.host, self.port = name, gnum, host, port
        self.fits_timeout = fits_timeout
        self.state = "idle (no viewers yet)"
        self.last = None    # {"seq", "ts_ns", "at"} of the newest published frame
        self._cond = asyncio.Condition()
        self._frame = None
        self._n = 0          # frames published
        self._wanted = asyncio.Event()
        self._clients = 0
        self._task = None

    # -- status -----------------------------------------------------------------

    def status(self) -> dict:
        """What the bridge knows without a frame -- all of it from the image port."""
        last = self.last
        return {
            "name": self.name,
            "gnum": self.gnum,
            "gcam": self.state,
            "clients": self._clients,
            "last_seq": last["seq"] if last else None,
            "last_ts_ns": last["ts_ns"] if last else None,
            "age_s": round(time.time() - last["at"], 1) if last else None,
        }

    def add_client(self) -> None:
        self._clients += 1
        self._wanted.set()

    def remove_client(self) -> None:
        self._clients = max(0, self._clients - 1)
        if not self._clients:
            self._wanted.clear()

    # -- the pump -------------------------------------------------------------

    def _parse(self, seq: int, ts_ns: int, blob: bytes) -> Frame:
        """FITS bytes -> Frame with the header cards as ``extra``. The frame is whole:
        each client cuts its own region and stride in its chz1 pipeline (``config``
        ``roi`` / ``every``). astropy returns uint16 for BITPIX=16/BZERO=32768."""
        t0 = time.perf_counter()
        cards, comments = {}, {}
        with pyfits.open(io.BytesIO(blob)) as hl:
            hdu = hl[0]
            data = hdu.data.astype(np.uint16, copy=False)
            for key in hdu.header:
                if key in STRUCTURAL_CARDS:
                    continue
                cards[key] = jsonable(hdu.header[key])
                if hdu.header.comments[key]:
                    comments[key] = hdu.header.comments[key]
        return Frame(
            name=f"gcam #{seq}",
            data=data,
            read_ms=(time.perf_counter() - t0) * 1e3,
            extra={
                "guider": {"seq": seq, "ts_ns": str(ts_ns), "cards": cards, "comments": comments},
            },
        )

    def _set_state(self, state: str) -> None:
        if state != self.state:
            log.info("%s: gcam %s:%d %s", self.name, self.host, self.port, state)
            self.state = state

    async def _pump(self) -> None:
        loop = asyncio.get_running_loop()
        client = None
        while True:
            if not self._wanted.is_set():
                if client is not None:
                    client.close()
                    client = None
                    self._set_state("paused (no viewers)")
                await self._wanted.wait()
            if client is None:
                try:
                    client = await loop.run_in_executor(None, GcamImageClient, self.host, self.port)
                    self._set_state("connected")
                except OSError:
                    self._set_state("unreachable")
                    await asyncio.sleep(2.0)
                    continue
            try:
                got = await loop.run_in_executor(None, client.fits, self.fits_timeout)
            except RuntimeError as e:  # "-E..." reply, e.g. ZWO not acquiring
                self._set_state(f"idle ({e})")
                await asyncio.sleep(1.0)
                continue
            except (ConnectionError, OSError):
                client.close()
                client = None
                self._set_state("unreachable")
                await asyncio.sleep(1.0)
                continue
            if got is None:  # nothing newer within the timeout
                continue
            seq, ts_ns, blob = got
            frame = await loop.run_in_executor(None, self._parse, seq, ts_ns, blob)
            self._set_state("streaming")
            async with self._cond:
                self._frame = frame
                self._n += 1
                self.last = {"seq": seq, "ts_ns": str(ts_ns), "at": time.time()}
                self._cond.notify_all()
            if self._n % 100 == 0:
                log.info("%s: %d frames from gcam", self.name, self._n)

    def start(self) -> None:
        self._task = asyncio.create_task(self._pump())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def get(self, last_seq: int) -> tuple[int, Frame]:
        """Seq-keyed delivery (the shared transport's ``FrameSource.get``): waits
        for a frame newer than ``last_seq``, so a late joiner is replayed the
        newest frame at once and every client sees every published frame."""
        async with self._cond:
            await self._cond.wait_for(lambda: self._frame is not None and self._n > last_seq)
            return self._n, self._frame
