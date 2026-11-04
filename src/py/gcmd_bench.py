#!/usr/bin/env python3
"""Bench for gcamzwo's poll of tcsis (src/gcam/gcmd.c, src/gcam/gcamtail.c).

    gcmd_bench.py [--bin PATH] [--fixtures DIR] [--ledger] [--no-tcp]

1. The exchanges of imacs docs/plans/guider-architecture.md, Appendix A
   (fixtures/gcmd_exchanges.txt), through gcmd_line() (gcmd_hello() for a
   bare 'gcmd'): the request, the cursor, the rotator of interest, the
   result and the echo after each answer.  IMACS's copy of the client runs
   the same file in its bench.
2. Each fixture, for several camera configurations, as the answers tcsis
   gives a camera that keeps up: the commands the camera runs (ACTION), its
   exit (EXIT), its rotator changes, against the camera's policy written a
   second time here.
3. The real poll against tcsis_sim.py: a night twice with tcsis away
   between the passes; a night that comes back on another rotator; the exit
   in the middle of a night; tcsis answering -1 (no ring) for a while; a
   tcsis older than gcmd, which must get nothing but the bare 'gcmd'; a
   new ring (edsshm on a new segment) numbered below the camera's cursor,
   which the camera must take from its first echo (asking 'gcmd 0 0')
   before the new ring's rotator, another, makes it exit.
4. The bench parts of the ledger tests (imacs docs/plans/
   guider-e-implementation.md, section 5): L1 tcsis killed; L2 tcsis away,
   back within the back-off cap with nothing restarted; L5 each command once
   and in order through a tcsis restart (some typed while it was away, some
   before the camera was back) and a camera restart, nothing for the other
   probe or another rotator, no log line while idle; L7 bursts while the
   camera is stopped.  --ledger runs L2 for three minutes and L5's idle for
   ten (make ledger); otherwise 70 s and 30 s (make test).

Exit status 0 when every check passes.
"""
import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "tcsis_sim.py")
BACKOFF_MAX = 30  # s: gcmd.c's GCMD_BACKOFF_MAX

N10 = "eds_20261010_llamas_aux2.txt"
N12 = "eds_20261012_mage_imacs.txt"
N20 = "eds_20261020_fire_noimacs.txt"
GAP0 = "eds_synth_gap_port0.txt"
GAP4 = "eds_synth_gap_port4.txt"

# (fixture, gnum, port, cnca, portexit), as the camera-tail bench had them
LINE_CASES = [
    (N10, 1, 4, 0, 1),   # LLAMAS probe a: fone/fthr/slews, EXIT at 14:21 UT
    (N10, 2, 4, 0, 1),   # LLAMAS probe b: likewise
    (N10, 1, 4, 0, 0),   # portexit 0: stays, and nothing more for port 4
    (N10, 1, 0, 1, 1),   # IMACS probe a: first rotator 4, no exit; then 0
    (N12, 1, 0, 1, 1),   # IMACS probe a, cnca 1: ca for every 'cnca 1'
    (N12, 2, 0, 1, 1),   # IMACS probe b, cnca 1: ca for every 'cnca 2'
    (N12, 1, 0, 0, 1),   # cnca 0: the cnca echoes are passed over
    (N12, 1, 4, 0, 1),   # 5 -> 0 is a change between two other ports
    (N12, 3, 5, 0, 1),   # probe c at MagE's port: only the slews act
    (N20, 1, 0, 0, 1),   # FIRE at AUX1: nothing
    (N20, 1, 3, 0, 1),   # a camera at port 3: the console slews act
    (GAP0, 1, 0, 1, 1),
    (GAP4, 1, 4, 0, 1),
]


def records(path):
    with open(path) as f:
        return [l.rstrip("\r\n") for l in f if l.startswith("~")]


def isnumber(s):
    try:
        float(s)
        return True
    except ValueError:
        return False


def echo(text, sender="4", ut="12000000"):
    """an EDS record of the TCS's echo of 'text'"""
    return "~Z201" + "66" + ut + "81" + sender + "* " + text.ljust(53) + "00"


Z802 = "~Z2016600120000802 -02 49 22 067.2843 43.3024 1.4554 179.996701-002041   6B"


def z802(roi):
    """a Z802 record: the rotator of interest is column 51"""
    return Z802[:51] + str(roi) + Z802[52:]


class Model:
    """The camera's policy written a second time: what gcamtail_test must
    print for these records, a camera that keeps up, tcsis gating."""

    def __init__(self, gnum, port, cnca, portexit):
        self.gnum, self.port, self.cnca, self.portexit = gnum, port, cnca, portexit
        self.probe = "abc"[gnum - 1]
        self.roi = -1
        self.last_known = -1
        self.actions = []        # ACTION <cmd> and EXIT, in order
        self.gates = []          # (old, now) for every rotator change
        self.exited = False

    def latch(self, now):
        if now == self.roi or self.exited:
            return
        self.gates.append((self.roi, now))
        self.roi = now
        if self.last_known == self.port and now not in (self.port, -1):
            if self.portexit:
                self.actions.append("EXIT")
                self.exited = True
                return
        if now != -1:
            self.last_known = now

    def gap(self):               # tcsis away: unknown until it answers again
        self.latch(-1)

    def line(self, rec):
        if self.exited or len(rec) < 18 or not rec.startswith("~Z2"):
            return
        if rec[15:18] == "802":
            self.latch(int(rec[51]) if len(rec) > 51 and rec[51] in "012345" else -1)
            return
        if rec[15:17] != "81":
            return
        text = rec[20:42].strip()
        if not text or self.roi != self.port:
            return
        if text[0] == "g" and text[1:2] == self.probe:
            cmd = text[2:].strip()
            if cmd:
                self.actions.append("ACTION " + cmd)
        elif text.startswith("slew"):
            self.actions.append("ACTION fone")
        elif self.cnca and text.startswith("cnca") and text[4:5].isspace():
            w = text.split()
            if len(w) >= 3 and w[1].lstrip("+-").isdigit() \
                    and int(w[1]) == self.gnum and isnumber(w[2]):
                self.actions.append("ACTION ca " + w[2])


def said(text, probe="a", cnca=1):
    """how gcamtail.c logs an echo it acts on: "'<cmd>' (why)"""
    if text[0] == "g" and text[1:2] == probe:
        return "'%s'" % text[2:].strip()
    if text.startswith("slew"):
        return "'fone' (slew)"
    if cnca and text.startswith("cnca"):
        return "'ca %s' (cnca)" % text.split()[2]
    return None


def answers(recs, port):
    """what tcsis answers a client of 'port' that keeps up with 'recs'"""
    out = ["- 0 -"]
    roi, latest = "-", 0
    for rec in recs:
        if len(rec) < 18 or rec[1:3] != "Z2":
            continue
        if rec[15:18] == "802":
            new = rec[51] if len(rec) > 51 and rec[51].isdigit() else "-"
            if new != roi:
                roi = new
                out.append("%s %d -" % (roi, latest))
        elif rec[15:17] == "81":
            text = rec[20:42].strip()
            if text:
                latest += 1
                if roi == str(port):
                    out.append("%s %d %s %s" % (roi, latest, rec[17], text))
    return out


def options(gnum, port, cnca, portexit):
    return ["--gnum", str(gnum), "--port", str(port),
            "--cnca", str(cnca), "--portexit", str(portexit)]


def parse(output):
    acts = [l.rstrip() for l in output.splitlines()
            if l.startswith("ACTION ") or l == "EXIT"]
    gates = [(int(m.group(1)), int(m.group(2))) for m in
             re.finditer(r"^LOG eds: roi (-?\d+) -> (-?\d+),", output, re.M)]
    roi = re.findall(r"^ROI (-?\d+)$", output, re.M)
    return acts, gates, int(roi[-1]) if roi else None


def after(t, t0, fmt="%.2f s"):
    """'<t - t0> s' for a check's label, or 'never'"""
    return "never" if t is None else fmt % (t - t0)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Proc:
    """a process whose output lines are kept with their arrival times"""

    def __init__(self, argv, stdin=False):
        self.p = subprocess.Popen(argv, stdin=subprocess.PIPE if stdin else None,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True, bufsize=1)
        self.lines = []          # (time, line)
        self.lock = threading.Lock()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for l in self.p.stdout:
            with self.lock:
                self.lines.append((time.time(), l.rstrip("\n")))

    def mark(self):
        with self.lock:
            return len(self.lines)

    def wait_for(self, pattern, timeout, since=0):
        """the (time, line) of the first line from 'since' on matching"""
        rx = re.compile(pattern)
        end = time.time() + timeout
        while True:
            with self.lock:
                for t, l in self.lines[since:]:
                    if rx.search(l):
                        return t, l
            if time.time() > end:
                return None, None
            time.sleep(0.02)

    def matching(self, pattern, since=0):
        rx = re.compile(pattern)
        with self.lock:
            return [l for t, l in self.lines[since:] if rx.search(l)]

    def send(self, lines):
        for l in lines:
            self.p.stdin.write(l + "\n")
        self.p.stdin.flush()

    def feed(self, recs, pace):
        for r in recs:
            self.send([r])
            time.sleep(pace)

    def kill(self, sig=signal.SIGKILL):
        if self.p.poll() is None:
            self.p.send_signal(sig)
        if sig in (signal.SIGKILL, signal.SIGTERM):
            try:
                self.p.wait(5)
            except subprocess.TimeoutExpired:
                pass


class Bench:
    def __init__(self, binary, fixtures, ledger):
        self.bin, self.fix, self.ledger = binary, fixtures, ledger
        self.failed = 0
        self.procs = []

    def check(self, label, ok, detail=None):
        print("%s %s" % ("ok  " if ok else "FAIL", label), flush=True)
        if not ok:
            self.failed += 1
            for l in (detail or []):
                print("       " + str(l))
        return ok

    def compare(self, label, want, got):
        if want == got:
            n = sum(1 for a in want if a.startswith("ACTION"))
            e = " and EXIT" if "EXIT" in want else ""
            return self.check("%s: %d commands%s in order" % (label, n, e), True)
        detail = ["expected %d, got %d lines" % (len(want), len(got))]
        for i in range(max(len(want), len(got))):
            w = want[i] if i < len(want) else "-"
            g = got[i] if i < len(got) else "-"
            if w != g:
                detail.append("first difference at %d: want %r, got %r" % (i, w, g))
                break
        return self.check("%s: commands" % label, False, detail)

    # -- 1. the shared exchanges -------------------------------------

    def exchanges(self):
        rows = []
        with open(os.path.join(self.fix, "gcmd_exchanges.txt")) as f:
            for l in f:
                if l.strip() and not l.startswith("#"):
                    rows.append(l.rstrip("\n").split("|"))
        path = self.tmpfile("exchanges.ans", [("HELLO " + r[1]) if r[0] == "gcmd" else r[1]
                                              for r in rows])
        r = subprocess.run([self.bin, "--trace"] + options(1, 0, 1, 0) + [path],
                           capture_output=True, text=True, timeout=30)
        groups, cur = [], []
        for l in r.stdout.splitlines():
            if l.startswith("AFTER "):
                groups.append((l, cur))
                cur = []
            else:
                cur.append(l)
        ok = len(groups) == len(rows)
        detail = [] if ok else ["%d answers, %d results" % (len(rows), len(groups))]
        cursor_was = None
        for (req, ans, cursor, roi, res, ech), (after, lines) in zip(rows, groups):
            want = "AFTER %s ROI %s R %s" % (cursor, roi, res)
            froms = [l for l in lines if re.match(r"LOG eds: '.*' (\(.*\) )?from -?\d+$", l)]
            if ech == "-":
                good = after == want and not froms
            else:
                sender, text = ech.split(" ", 1)
                good = after == want and froms == ["LOG eds: %s from %s" % (said(text), sender)]
            if req != "gcmd" and req != "gcmd 0 %s" % cursor_was:
                good = False               # not what the client would ask
            cursor_was = cursor
            if not good:
                ok = False
                detail.append("%s -> %s: got %r %s, want %r echo %s"
                              % (req, ans, after, froms, want, ech))
        self.check("the %d exchanges of Appendix A: request, cursor, rotator, result, "
                   "echo" % len(rows), ok, detail)

    # -- 2. the fixtures as answers ---------------------------------

    def line_case(self, name, gnum, port, cnca, portexit):
        label = "%s g%d p%d cnca%d exit%d" % (name[4:-4], gnum, port, cnca, portexit)
        recs = records(os.path.join(self.fix, name))
        m = Model(gnum, port, cnca, portexit)
        for rec in recs:
            m.line(rec)
        path = self.tmpfile(name + ".ans", answers(recs, port))
        r = subprocess.run([self.bin] + options(gnum, port, cnca, portexit) + [path],
                           capture_output=True, text=True, timeout=30)
        acts, gates, roi = parse(r.stdout)
        self.compare(label, m.actions, acts)
        self.check("%s: %d rotator changes" % (label, len(m.gates)), gates == m.gates,
                   ["want %s" % m.gates, "got  %s" % gates])
        self.check("%s: rotator %d at the end" % (label, m.roi), roi == m.roi,
                   ["got %s" % roi])

    # -- the real poll -----------------------------------------------

    def sim(self, port, *extra):
        s = Proc([sys.executable, SIM, "--port", str(port), "--stdin"] + list(extra),
                 stdin=True)
        self.procs.append(s)
        t, _ = s.wait_for(r"TCSIS simulator on", 10)
        if t is None:
            raise RuntimeError("tcsis_sim.py did not start")
        return s

    def camera(self, port, gnum, rot, cnca, portexit, seconds=900):
        c = Proc([self.bin] + options(gnum, rot, cnca, portexit) +
                 ["--poll", "127.0.0.1:%d" % port, "--seconds", str(seconds)])
        self.procs.append(c)
        return c

    def polling(self, cam, since=0, timeout=5):
        return cam.wait_for(r"^LOG eds: polling ", timeout, since)[0]

    def settle(self, cam, n, timeout=10):
        """wait until 'cam' has printed n ACTION/EXIT lines and is quiet"""
        end = time.time() + timeout
        while time.time() < end:
            k = len(cam.matching(r"^(ACTION |EXIT$)"))
            if k >= n:
                time.sleep(0.5)
                return
            time.sleep(0.05)

    def actions(self, cam, since=0):
        return cam.matching(r"^(ACTION |EXIT$)", since)

    def cleanup(self):
        for p in self.procs:
            p.kill()
        self.procs = []

    def live_twice(self, names, gnum, rot, cnca, portexit, pace=0.01):
        """the passes fed in turn, tcsis away between them"""
        label = "tcp %s g%d p%d cnca%d exit%d" % (
            "+".join(n[4:-4] for n in names), gnum, rot, cnca, portexit)
        port = free_port()
        sim = self.sim(port)
        cam = self.camera(port, gnum, rot, cnca, portexit)
        m = Model(gnum, rot, cnca, portexit)
        try:
            if not self.check("%s: polling" % label, self.polling(cam) is not None):
                return
            for i, n in enumerate(names):
                recs = records(os.path.join(self.fix, n))
                if i > 0:
                    m.gap()
                    mark = cam.mark()
                    sim.send(["!down"])
                    lost = cam.wait_for(r"^WARN eds: tcsis .* lost, retrying", 3, mark)[0]
                    self.check("%s: tcsis away after pass %d, the camera warns" % (label, i),
                               lost is not None)
                    sim.send(["!up"])
                    if not self.check("%s: polling again" % label,
                                      self.polling(cam, mark, 10) is not None):
                        return
                for rec in recs:
                    m.line(rec)
                cam_exit = "EXIT" in self.actions(cam)
                if not cam_exit:
                    sim.feed(recs, pace)
                self.settle(cam, len(m.actions), 3)
            self.compare(label, m.actions, self.actions(cam))
        finally:
            self.cleanup()

    def live_old(self):
        label = "tcp a tcsis without gcmd"
        port = free_port()
        sim = self.sim(port, "--verbose")
        sim.send(["!old"])
        cam = self.camera(port, 1, 0, 1, 1)
        try:
            t, l = cam.wait_for(r"^WARN eds: tcsis .* answers '', no gcmd", 5)
            self.check("%s: warned once" % label, t is not None, [l])
            time.sleep(6)
            reqs = sim.matching(r"^  req ")
            self.check("%s: only the bare 'gcmd', every 5 s (%d), nothing for the TCS"
                       % (label, len(reqs)), reqs and all(r == "  req gcmd" for r in reqs)
                       and not sim.matching(r"passed to the TCS"), reqs[:5])
            mark = cam.mark()
            sim.send(["!new", z802(0)])
            self.check("%s: a tcsis with gcmd: polling within the 5 s retry" % label,
                       self.polling(cam, mark, 7) is not None)
            sim.send([echo("gafone")])
            self.settle(cam, 1, 3)
            self.check("%s: and acting" % label, self.actions(cam) == ["ACTION fone"],
                       self.actions(cam))
        finally:
            self.cleanup()

    def live_noring(self):
        label = "tcp tcsis answering -1 for a while"
        port = free_port()
        sim = self.sim(port)
        sim.send(["!noring"])
        cam = self.camera(port, 1, 0, 1, 1)
        try:
            t, l = cam.wait_for(r"^WARN eds: tcsis .* answers '-1'", 5)
            self.check("%s: warned once" % label, t is not None, [l])
            time.sleep(6)
            n = len(cam.matching(r"answers '-1'"))
            self.check("%s: and only once (%d)" % (label, n), n == 1)
            sim.send(["!ring"])
            t, l = cam.wait_for(r"^LOG eds: tcsis .* answers again", 7)
            self.check("%s: served within the 5 s retry" % label, t is not None)
            recs = records(os.path.join(self.fix, GAP0))
            m = Model(1, 0, 1, 1)
            for rec in recs:
                m.line(rec)
            sim.feed(recs, 0.01)
            self.settle(cam, len(m.actions), 3)
            self.compare(label, m.actions, self.actions(cam))
        finally:
            self.cleanup()

    def live_newring(self):
        label = "tcp a new ring below the cursor"
        port = free_port()
        sim = self.sim(port, "--verbose")
        cam = self.camera(port, 1, 0, 0, 1)
        try:
            self.polling(cam)
            sim.send([z802(0), echo("gafone"), echo("gaftwo"), echo("gafthr")])
            self.settle(cam, 3, 3)
            mark, smark = cam.mark(), sim.mark()
            sim.send(["!down", "!newring", z802(0), echo("gaftwo"), echo("gafone"), z802(4)])
            cam.wait_for(r"^WARN eds: tcsis .* lost, retrying", 3, mark)
            sim.send(["!up"])
            self.check("%s: polling again" % label,
                       self.polling(cam, mark, BACKOFF_MAX + 5) is not None)
            self.settle(cam, 6, 3)
            reqs = sim.matching(r"^  req ", smark)
            self.check("%s: the camera asks from 0 again" % label,
                       "  req gcmd 0 0" in reqs, reqs[:6])
            self.check("%s: its two echoes acted on, in order, then the exit at rotator 4"
                       % label,
                       self.actions(cam, mark) == ["ACTION ftwo", "ACTION fone", "EXIT"],
                       self.actions(cam, mark))
        finally:
            self.cleanup()

    # -- 4. the ledger tests' bench parts -----------------------------

    def l1(self):
        label = "L1 tcsis killed"
        port = free_port()
        sim = self.sim(port)
        cam = self.camera(port, 1, 0, 0, 1)
        try:
            self.polling(cam)
            sim.send([z802(0), echo("gafone")])
            self.settle(cam, 1, 3)
            mark = cam.mark()
            t0 = time.time()
            sim.kill()
            t, l = cam.wait_for(r"^WARN eds: tcsis .* lost, retrying", 3, mark)
            self.check("%s: the camera warns on its screen within 1 s (%s)"
                       % (label, after(t, t0)), t is not None and t - t0 <= 1.0, [l])
            t, l = cam.wait_for(r"^LOG eds: roi 0 -> -1,", 3, mark)
            self.check("%s: and its rotator is unknown (roi=-1)" % label,
                       t is not None and t - t0 <= 1.0)
            sim = self.sim(port)
            t = self.polling(cam, mark, BACKOFF_MAX + 5)
            self.check("%s: tcsis started again, the camera polls" % label, t is not None)
            mark = cam.mark()
            sim.send([z802(0), echo("gafthr")])
            self.settle(cam, 2, 3)
            self.check("%s: and acts on the next command" % label,
                       self.actions(cam, mark) == ["ACTION fthr"], self.actions(cam, mark))
        finally:
            self.cleanup()

    def l2(self):
        away = 180 if self.ledger else 70      # past 61 s: the cap shows
        label = "L2 tcsis away for %d s" % away
        port = free_port()
        sim = self.sim(port)
        cam = self.camera(port, 1, 0, 0, 1)
        try:
            self.polling(cam)
            sim.send([z802(0)])
            mark = cam.mark()
            sim.send(["!down"])
            time.sleep(away)
            mark = cam.mark()
            t1 = time.time()
            sim.send(["!up"])
            t = self.polling(cam, mark, BACKOFF_MAX + 5)
            self.check("%s: back to polling %s after tcsis, within the %d s cap, "
                       "nothing restarted" % (label, after(t, t1, "%.1f s"), BACKOFF_MAX),
                       t is not None and t - t1 <= BACKOFF_MAX + 1 and cam.p.poll() is None)
            mark = cam.mark()
            sim.send([echo("gafone")])
            self.settle(cam, 1, 3)
            self.check("%s: and acts on the next command" % label,
                       self.actions(cam, mark) == ["ACTION fone"], self.actions(cam, mark))
            t0 = time.time()
            cam2 = self.camera(port, 2, 0, 0, 1)
            t = self.polling(cam2, 0, 3)
            self.check("%s: a camera started now polls from its first second (%s)"
                       % (label, after(t, t0)), t is not None and t - t0 <= 1.0)
        finally:
            self.cleanup()

    def l5(self):
        idle = 600 if self.ledger else 30
        label = "L5 once, in order"
        port = free_port()
        sim = self.sim(port)
        cam1 = self.camera(port, 1, 0, 0, 1)
        try:
            self.polling(cam1)
            want = []

            def phase(n, start):
                recs = []
                for i in range(n):
                    cmd = ("gaftwo", "gafone")[(start + i) % 2]
                    recs.append(echo(cmd, ut="1200%02d00" % (start + i)))
                    want.append("ACTION " + cmd[2:])
                    if i % 3 == 2:
                        recs.append(echo("gbftwo"))            # the other probe
                sim.feed(recs, 0.05)

            sim.feed([z802(4)] + [echo("gaftwo")] * 5, 0.05)  # another rotator
            sim.send([z802(0)])
            phase(10, 0)
            self.settle(cam1, 10, 5)
            mark = cam1.mark()                                 # a tcsis restart:
            sim.send(["!down"])
            cam1.wait_for(r"lost, retrying", 3, mark)
            phase(2, 10)                                       # typed while away,
            sim.send(["!up"])
            phase(2, 12)                                       # and before the
            self.polling(cam1, mark, 10)                       # camera is back
            phase(10, 14)
            self.settle(cam1, 24, 5)
            cam1.kill(signal.SIGTERM)                           # a camera restart
            cam2 = self.camera(port, 1, 0, 0, 1)
            self.polling(cam2)
            phase(10, 24)
            self.settle(cam2, 10, 5)
            got = self.actions(cam1) + self.actions(cam2)
            self.compare("%s: 34 gaftwo/gafone through a tcsis restart (4 typed before "
                         "the camera was back) and a camera restart, none of the other "
                         "probe or rotator, none twice" % label, want, got)
            mark = cam2.mark()
            time.sleep(idle)
            quiet = cam2.matching(r"^(LOG|WARN) ", mark)
            self.check("%s: %d s idle, no log line" % (label, idle), not quiet, quiet[:5])
        finally:
            self.cleanup()

    def l7(self):
        label = "L7 bursts"
        port = free_port()
        sim = self.sim(port)
        cam = self.camera(port, 1, 0, 0, 1)
        try:
            self.polling(cam)
            sim.send([z802(0)])
            time.sleep(0.5)
            mark = cam.mark()
            cam.kill(signal.SIGSTOP)
            t0 = time.time()
            stopped = True
            for i in range(40):                    # 40 in 2 s, stopped 1.5 s
                sim.send([echo("gaxyr 5 1 0")])
                time.sleep(0.05)
                if stopped and time.time() - t0 >= 1.5:
                    cam.kill(signal.SIGCONT)
                    stopped = False
            if stopped:
                cam.kill(signal.SIGCONT)
            self.settle(cam, 40, 5)
            got = self.actions(cam, mark)
            self.check("%s: 40 'gaxyr 5 1 0' in 2 s, the camera stopped for 1.5 s: "
                       "all %d acted on" % (label, len(got)),
                       got == ["ACTION xyr 5 1 0"] * 40)
            mark, smark = cam.mark(), sim.mark()
            cam.kill(signal.SIGSTOP)
            sim.feed([echo("gaxys 5 1000.0 %d.0" % (1000 + i)) for i in range(1, 71)], 0.005)
            cam.kill(signal.SIGCONT)
            want = ["ACTION xys 5 1000.0 %d.0" % (1000 + i) for i in range(7, 71)]
            self.settle(cam, 40 + 64, 5)
            self.compare("%s: 70 while the camera is stopped: the newest 64" % label,
                         want, self.actions(cam, mark))
            self.check("%s: tcsis logs the camera behind the ring" % label,
                       bool(sim.matching(r"fell behind the ring", smark)))
        finally:
            self.cleanup()

    def tmpfile(self, name, lines):
        d = os.path.join(os.environ.get("TMPDIR", "/tmp"), "gcmd_bench.%d" % os.getpid())
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, name)
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "gcam", "gcamtail_test"))
    ap.add_argument("--fixtures", default=os.path.join(HERE, "fixtures"))
    ap.add_argument("--ledger", action="store_true",
                    help="L2 away for 180 s, L5 idle for 600 s")
    ap.add_argument("--no-tcp", action="store_true", help="the line checks only")
    args = ap.parse_args()

    b = Bench(os.path.abspath(args.bin), os.path.abspath(args.fixtures), args.ledger)
    b.exchanges()
    for case in LINE_CASES:
        b.line_case(*case)
    if not args.no_tcp:
        b.live_twice([N12, N12], 1, 0, 1, 0)    # stays: the commands twice
        b.live_twice([N12, N12], 1, 0, 1, 1)    # 0 -> away -> 5: EXIT
        b.live_twice([N10], 1, 4, 0, 1)         # the exit in the middle
        b.live_twice([GAP0, GAP4], 1, 0, 1, 1)  # port -> away -> other: EXIT
        b.live_twice([GAP0, GAP4], 1, 0, 1, 0)  # portexit 0: no exit
        b.live_noring()
        b.live_old()
        b.live_newring()
        b.l1()
        b.l2()
        b.l5()
        b.l7()
    print("all checks passed" if not b.failed else "%d checks FAILED" % b.failed)
    return 1 if b.failed else 0


if __name__ == "__main__":
    sys.exit(main())
