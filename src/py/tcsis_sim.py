#!/usr/bin/env python3
"""
Minimal TCSIS simulator for gcamzwo.

gcam's telio.c speaks a trivial protocol: it sends "<keyword>\\n" and reads
one value terminated by LF/CR. Launching gcamzwo with any '-t' outside
{0,1,2} points telio at localhost:5801, which is what this serves --
see the "TCSIS Simulator" case in zwogcam.c.

Values are plausible but static; the point is to model a *responsive* TCS
so the cost of gcam's per-request round trips can be measured. --delay adds
a per-request service time to model a slower TCS.

    ./tcsis_sim.py                 # responsive
    ./tcsis_sim.py --delay 0.005   # 5 ms per request

The TCS's command echoes, for the camera's poll (gcamtail.c, gcmd.c): the
simulator keeps tcsis's ring as magsoft's edsshm builds it from the EDS
records it is given (every ~Z2...81w echo with a command, columns 20-41,
tagged with the rotator of interest of the last Z802, column 51; the last
64; a Z802 too short to reach column 51 is taken as unknown, where edsshm,
which copies each record over the last in a 100-byte slot, keeps the
previous rotator for one of 49 characters or fewer) and answers 'gcmd <port> <after>' as tcsis does: '<roi> <seq> <sender>
<text>' for the oldest echo for <port> after <after>, '<roi> <latest> -'
when there is none or <after> is -1, '-1' when the request is malformed;
a bare 'gcmd' with '<roi> <latest> -'.  --old plays a tcsis older than
gcmd: an empty line for the bare 'gcmd' (a status it does not know), and
'gcmd <port> <after>' passed to the TCS as a command, which it logs.

    ./tcsis_sim.py --fixture night.txt --pace 0.01    # released 10 ms apart
    ./tcsis_sim.py --fixture a.txt@0 --fixture b.txt@8   # b from 8 s on
    ./tcsis_sim.py --down 3,5      # not listening from 3 s to 5 s
    ./tcsis_sim.py --noring 10     # '-1' for 10 s, as with an old edsshm
    ./tcsis_sim.py --old 10        # no gcmd for 10 s, as an old tcsis
    ./tcsis_sim.py --stdin         # records from stdin, as edsshm reads them,
                                   # and the lines !down !up !noring !ring
                                   # !old !new !newring (an empty ring
                                   # numbered from 1 again: a new segment)
"""

import argparse, collections, socket, socketserver, sys, threading, time

VALUES = {
    "ra": "06:45:08.9", "dec": "-16:42:58", "epoch": "2000.0",
    "st": "07:12:33", "ha": "00:27:24", "ut": "23:17:01",
    "zd": "31.4", "airmass": "1.171", "telfocus": "1234.5",
    "rotangle": "45.6", "rotatore": "-12.3", "rotatorn": "3",
    "telaz": "123.4", "telel": "58.6", "telpa": "45.6",
    "guiderx1": "10.250", "guidery1": "-5.125",
    "guiderx2": "0.000",  "guidery2": "0.000",
    "guiderx3": "0.000",  "guidery3": "0.000",
    "telguide": "1 0", "roi": "3", "gdrmountmv": "000", "mountmv": "0000",
    "tambient": "8.4", "tcell": "8.1", "wxtemp": "8.2",
    # multi-value "dump" requests the protocol doc recommends
    "datetime": "2026-08-29 23:17:01 07:12:33",
    "telpos": "06:45:08.9 -16:42:58 2000.0 00:27:24 1.171 45.6",
    "teldata": "3 1 000 0000 123.4 58.6 31.4 45.6 0.0 0",
    "gdr1data": "10.250 -5.125 0.000 0 0",
    "gdr2data": "0.000 0.000 0.000 0 0",
}

COUNT = [0]
LOCK = threading.Lock()
RING_SIZE = 64


def say(text):
    print("tcsis_sim: " + text, file=sys.stderr, flush=True)


class Ring:
    """edsshm's ring and tcsis's gcmd, written a second time"""

    def __init__(self):
        self.lock = threading.Lock()
        self.echoes = collections.deque(maxlen=RING_SIZE)  # (seq, roi, sender, text)
        self.latest = 0
        self.roi = "-"                 # the last Z802's, as tcsis's GETTELROI
        self.noring = False
        self.old = False               # a tcsis without gcmd

    def record(self, line):
        if len(line) < 18 or line[1:3] != "Z2":
            return
        with self.lock:
            if line[15:18] == "802":
                self.roi = line[51] if len(line) > 51 and line[51] in "012345" else "-"
            elif line[15:17] == "81":
                text = line[20:42].strip()
                if text:
                    self.latest += 1
                    self.echoes.append((self.latest, self.roi, line[17], text))

    def gcmd(self, request):
        w = request.split()
        if self.old:                   # a status it does not know, or a command
            if len(w) == 1:
                return ""
            say("old tcsis: '%s' passed to the TCS" % request)
            return "0"
        if w == ["gcmd"]:
            with self.lock:
                return "-1" if self.noring else "%s %d -" % (self.roi, self.latest)
        try:
            if len(w) != 3 or w[0] != "gcmd":
                raise ValueError
            port, after = int(w[1]), int(w[2])
            if not 0 <= port <= 5 or after < -1:
                raise ValueError
        except ValueError:
            return "-1"
        with self.lock:
            if self.noring:
                return "-1"
            if 0 <= after < self.latest:
                first = max(1, self.latest - RING_SIZE + 1)
                if after + 1 < first:
                    say("gcmd %d %d: the client fell behind the ring, echoes lost"
                        % (port, after))
                for seq, roi, sender, text in self.echoes:
                    if seq > after and roi == str(port):
                        return "%s %d %s %s" % (self.roi, seq, sender, text)
            return "%s %d -" % (self.roi, self.latest)


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        with self.server.clients_lock:
            self.server.clients.add(self.connection)
        try:
            self.serve()
        except OSError:
            pass
        finally:
            with self.server.clients_lock:
                self.server.clients.discard(self.connection)

    def serve(self):
        while True:
            line = self.rfile.readline()
            if not line:
                break
            if self.server.delay:
                time.sleep(self.server.delay)
            text = line.decode("ascii", "replace").strip()
            key = text.split()[0].lower() if text else ""
            with LOCK:
                COUNT[0] += 1
            if self.server.verbose:
                print("  req %s" % text, flush=True)
            if key == "gcmd":
                val = self.server.ring.gcmd(text)
            else:
                val = VALUES.get(key)
            # commands (aeg, gpaer, ofra, ...) just acknowledge
            self.wfile.write(((val if val is not None else "0") + "\n").encode())
            self.wfile.flush()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class Service:
    """the listening socket, which --down and !down take away"""

    def __init__(self, port, ring, delay, verbose):
        self.port, self.ring, self.delay, self.verbose = port, ring, delay, verbose
        self.srv = None
        self.lock = threading.Lock()

    def up(self):
        with self.lock:
            if self.srv:
                return
            srv = Server(("127.0.0.1", self.port), Handler)
            srv.ring, srv.delay, srv.verbose = self.ring, self.delay, self.verbose
            srv.clients, srv.clients_lock = set(), threading.Lock()
            threading.Thread(target=srv.serve_forever, daemon=True).start()
            self.srv = srv

    def down(self):
        with self.lock:
            srv, self.srv = self.srv, None
        if not srv:
            return
        srv.shutdown()
        srv.server_close()
        with srv.clients_lock:
            for c in list(srv.clients):
                try:
                    c.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass


def release(ring, path, start, pace, t0):
    with open(path) as f:
        lines = [l.rstrip("\r\n") for l in f if l.startswith("~")]
    time.sleep(max(0.0, t0 + start - time.time()))
    for l in lines:
        ring.record(l)
        if pace:
            time.sleep(pace)
    say("%s: %d records released, latest %d" % (path, len(lines), ring.latest))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=5801)
    p.add_argument("--delay", type=float, default=0.0,
                   help="per-request service time [s]")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--report", type=float, default=0.0,
                   help="print request count every N seconds")
    p.add_argument("--fixture", action="append", default=[], metavar="FILE[@T]",
                   help="EDS records for the ring, released from T s on")
    p.add_argument("--pace", type=float, default=0.0,
                   help="seconds between the records of a fixture")
    p.add_argument("--down", action="append", default=[], metavar="T0,T1",
                   help="not listening from T0 s to T1 s")
    p.add_argument("--noring", type=float, default=0.0, metavar="T",
                   help="answer gcmd with -1 for the first T s")
    p.add_argument("--old", type=float, default=0.0, metavar="T",
                   help="a tcsis without gcmd for the first T s")
    p.add_argument("--stdin", action="store_true",
                   help="records and !down !up !noring !ring !old !new !newring "
                        "from stdin")
    a = p.parse_args()
    t0 = time.time()
    ring = Ring()
    ring.noring = a.noring > 0
    ring.old = a.old > 0
    svc = Service(a.port, ring, a.delay, a.verbose)
    svc.up()
    print("TCSIS simulator on 127.0.0.1:%d  delay=%.1f ms"
          % (a.port, a.delay * 1e3), flush=True)

    def at(t, fn):
        threading.Timer(max(0.0, t0 + t - time.time()), fn).start()

    if a.noring:
        def ringon():
            ring.noring = False
            say("ring served")
        at(a.noring, ringon)
    if a.old:
        def new():
            ring.old = False
            say("gcmd served")
        at(a.old, new)
    for d in a.down:
        d0, d1 = (float(x) for x in d.split(","))
        at(d0, lambda: (svc.down(), say("down")))
        at(d1, lambda: (svc.up(), say("up")))
    for spec in a.fixture:
        path, _, start = spec.partition("@")
        threading.Thread(target=release, daemon=True,
                         args=(ring, path, float(start or 0), a.pace, t0)).start()
    if a.report:
        def rep():
            last = 0
            while True:
                time.sleep(a.report)
                with LOCK:
                    now = COUNT[0]
                print("  TCS requests: %d total (%.1f/s)"
                      % (now, (now - last) / a.report), flush=True)
                last = now
        threading.Thread(target=rep, daemon=True).start()
    if a.stdin:
        for line in sys.stdin:
            line = line.rstrip("\r\n")
            if line == "!down":
                svc.down()
                say("down")
            elif line == "!up":
                svc.up()
                say("up")
            elif line == "!noring":
                ring.noring = True
                say("no ring")
            elif line == "!ring":
                ring.noring = False
                say("ring served")
            elif line == "!old":
                ring.old = True
                say("no gcmd")
            elif line == "!new":
                ring.old = False
                say("gcmd served")
            elif line == "!newring":
                with ring.lock:
                    ring.echoes.clear()
                    ring.latest, ring.roi = 0, "-"
                say("a new ring")
            elif line.startswith("~"):
                ring.record(line)
        return
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
