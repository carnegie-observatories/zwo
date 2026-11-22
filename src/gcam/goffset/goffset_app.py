#!/usr/bin/env python3
"""Stand-in for the Swope app's TCP command port, for goffset (README.md).

Answers the three commands gcamzwo and the tests use, as the app does:
one line, an error is "-E" and its text, the connection stays open
until the client closes it, and a move's answer comes when the move has
ended.  A pretend probe for each guider and a pretend telescope keep
their positions; every line and answer is printed.

    guider_offset <n> <dx> <dy>     ok | -E<why>    (after --move-s)
    tele_offset <dx> <dy>           ok | -E<why>    (after --move-s)
    guider_pos <n>                  ok <x> <y> <moving>
    version                         goffset_app

    python3 goffset_app.py --port 51911 --move-s 3
    python3 goffset_app.py --dec-short      # tele: Dec part lost
    python3 goffset_app.py --no-track       # tele: refused
    python3 goffset_app.py --silent         # never answers a move

Standard library only.
"""

import argparse
import math
import socket
import threading
import time

MAX_OFFSET = 300.0      # arcsec per axis (the app's cap)
PROBE_LIMIT = 600.0     # arcsec from the centre (pretend soft limits)


def now():
    return time.strftime("%H:%M:%S") + ".%03d" % (int(time.time() * 1000) % 1000)


class App:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.probe = {1: [0.0, 0.0], 2: [0.0, 0.0]}
        self.probe_moving = {1: False, 2: False}
        self.tele = [0.0, 0.0]          # the sum of the offsets, arcsec
        self.tele_moving = False
        self.moves = []                 # (what, dx, dy, answer) for the record

    # --- the commands ----------------------------------------------------

    def numbers(self, words, n):
        if len(words) != n:
            raise ValueError("wrong number of arguments")
        v = [float(w) for w in words]
        if not all(math.isfinite(x) for x in v):
            raise ValueError("not a number")
        return v

    def guider_offset(self, words):
        try:
            n, dx, dy = self.numbers(words, 3)
        except ValueError as e:
            return "-E" + str(e)
        n = int(n)
        if n not in self.probe:
            return "-Eno guider %d" % n
        with self.lock:
            if self.probe_moving[n]:
                return "-Eguider %d is moving" % n
            if abs(dx) > MAX_OFFSET or abs(dy) > MAX_OFFSET:
                return "-Eoffset too large"
            x, y = self.probe[n][0] + dx, self.probe[n][1] + dy
            if abs(x) > PROBE_LIMIT or abs(y) > PROBE_LIMIT:
                return "-Eillegal position"
            self.probe_moving[n] = True
        return self.move(lambda: self.probe_done(n, x, y),
                         ("guider %d" % n, dx, dy))

    def probe_done(self, n, x, y):
        with self.lock:
            self.probe[n] = [x, y]
            self.probe_moving[n] = False

    def tele_offset(self, words):
        try:
            dx, dy = self.numbers(words, 2)
        except ValueError as e:
            return "-E" + str(e)
        with self.lock:
            if self.tele_moving:
                return "-Ea move is running"
            if self.args.no_track:
                return "-Ethe telescope is not tracking"
            if abs(dx) > MAX_OFFSET or abs(dy) > MAX_OFFSET:
                return "-Eoffset too large"
            self.tele_moving = True
        short = self.args.dec_short and abs(dy) >= 1.0
        answer = self.move(lambda: self.tele_done(dx, 0.0 if short else dy),
                           ("telescope", dx, dy))
        if short and answer == "ok":
            answer = '-Ethe telescope moved only 0.0" of %.1f" in Dec' % abs(dy)
        return answer

    def tele_done(self, dx, dy):
        with self.lock:
            self.tele = [self.tele[0] + dx, self.tele[1] + dy]
            self.tele_moving = False

    def guider_pos(self, words):
        try:
            (n,) = self.numbers(words, 1)
        except ValueError as e:
            return "-E" + str(e)
        n = int(n)
        if n not in self.probe:
            return "-Eno guider %d" % n
        with self.lock:
            x, y = self.probe[n]
            return "ok %.2f %.2f %d" % (x, y, int(self.probe_moving[n]))

    def move(self, done, what):
        if self.args.refuse:
            done_refused = self.args.refuse
            done()                      # nothing moved: not moving any more
            return "-E" + done_refused
        print("%s   %s %+.2f\",%+.2f\" moving for %.1f s" %
              ((now(),) + what + (self.args.move_s,)), flush=True)
        time.sleep(self.args.move_s)
        done()
        return "ok"

    # --- the port --------------------------------------------------------

    def handle(self, line):
        words = line.split()
        if not words:
            return ""
        cmd, rest = words[0], words[1:]
        if cmd == "guider_offset":
            return self.guider_offset(rest)
        if cmd == "tele_offset":
            return self.tele_offset(rest)
        if cmd == "guider_pos":
            return self.guider_pos(rest)
        if cmd == "version":
            return "goffset_app"
        return "-Eunknown command"

    def serve(self, conn, peer):
        with conn:
            while True:
                try:
                    data = conn.recv(512)
                except OSError:
                    return
                if not data:
                    return
                line = data.decode(errors="replace").strip()
                print("%s %s -> %s" % (now(), peer[0], line), flush=True)
                if self.args.close:
                    print("%s    (closing, no answer)" % now(), flush=True)
                    return
                if self.args.silent and line.split()[:1] in (["guider_offset"],
                                                             ["tele_offset"]):
                    print("%s    (no answer)" % now(), flush=True)
                    while conn.recv(512):
                        pass
                    return
                answer = self.handle(line)
                print("%s %s <- %s" % (now(), peer[0], answer), flush=True)
                if answer:
                    conn.sendall((answer + "\n").encode())


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--port", type=int, default=51911)
    p.add_argument("--move-s", type=float, default=2.0,
                   help="how long each move takes [s]")
    p.add_argument("--dec-short", action="store_true",
                   help="tele_offset loses its Dec part and says so")
    p.add_argument("--no-track", action="store_true",
                   help="tele_offset: the telescope is not tracking")
    p.add_argument("--refuse", metavar="TEXT",
                   help="answer every move -E<TEXT>")
    p.add_argument("--silent", action="store_true",
                   help="never answer a move")
    p.add_argument("--close", action="store_true",
                   help="close every connection without an answer")
    args = p.parse_args()

    app = App(args)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", args.port))
    s.listen(8)
    print("%s goffset_app on port %d, moves of %.1f s" %
          (now(), args.port, args.move_s), flush=True)
    try:
        while True:
            conn, peer = s.accept()
            threading.Thread(target=app.serve, args=(conn, peer),
                             daemon=True).start()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
