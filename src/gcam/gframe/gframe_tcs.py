#!/usr/bin/env python3
"""
Stand-in for the Swope TCS's guider input, to test gframe without the TCS.

Connects to gcamzwo's gframe port the way the Swope TCS connects to its
guider (Swope-TCS GuiderClient.cpp, gitlab.lco.cl/mfiaschi/Swope-TCS),
parses the bytes with the same rules and prints what the TCS would do with
each frame:

  - bytes <= 12 are dropped (so LF goes, CR stays);
  - a 'G' and four digits make a frame; CR, then LF, may follow;
  - a two-digit count above 50 is negative (count - 100);
  - a frame with -49 or +50 on either axis is ignored;
  - one count is 0.05 arcsec; X is the RA correction, Y the Dec one;
  - a gap of 3 s or more between reads clears the parser's buffer;
  - the TCS's main loop acts on an axis only above Guider_deadband (0.1").

The real TCS does not reconnect once a connection it had drops (it polls
nothing; only GUIDE false/true or a power cycle reconnect it).  This script
reconnects every second unless --as-today is given.

--relink HOST does that GUIDE false/true on the real TCS's JSON port (50000),
so the TCS reconnects to gcamzwo after gcamzwo was restarted.  GUIDE also
zeroes the differential tracking rates (TRACKRATE): set them again after.

    ./gframe_tcs.py                       # localhost:10001
    ./gframe_tcs.py --host swope-obs --port 10001
    ./gframe_tcs.py --as-today            # stop at the first drop, like the TCS
    ./gframe_tcs.py --relink 10.8.80.53   # the TCS reconnects to its guider
"""

import argparse
import json
import socket
import sys
import time

SCALE = 0.05            # arcsec per count, GuiderClient.cpp
AUTOFLUSH_S = 3.0       # GuiderClient.cpp: clear the buffer after a 3 s gap


class Stats:
    def __init__(self):
        self.frames = self.ignored = self.corrupt = 0
        self.ra = self.dec = 0.0           # applied by the main loop
        self.ra_small = self.dec_small = 0  # below the dead band


def stamp():
    t = time.time()
    return time.strftime("%H:%M:%S", time.gmtime(t)) + ".%03d" % (int(t * 1000) % 1000)


def parse(buf, st, deadband, quiet):
    """GuiderClient::OnDataReceived, after the bytes were appended."""
    while True:
        found = buf.find("G")
        if found < 0:
            return buf
        if len(buf) - found < 5:
            return buf[found:] if found > 0 else buf
        n = 5
        if len(buf) - found >= 6 and buf[found + 5] == "\r":
            n = 6
            if len(buf) - found >= 7 and buf[found + 6] == "\n":
                n = 7
        digits = buf[found + 1:found + 5]
        if not digits.isdigit():
            st.corrupt += 1
            print("%s corrupted guider command %r" % (stamp(), buf[found:found + n]))
            buf = buf[found + n:]
            continue
        x, y = int(digits[0:2]), int(digits[2:4])
        if x > 50:
            x -= 100
        if y > 50:
            y -= 100
        if x in (-49, 50) or y in (-49, 50):
            st.ignored += 1
            print("%s ignored guide command with -49 or +50: X%d Y%d" % (stamp(), x, y))
            buf = buf[found + n:]
            continue
        st.frames += 1
        ra, dec = x * SCALE, y * SCALE
        acts = []
        if abs(ra) > deadband:
            st.ra += ra
            acts.append("RA %+.2f\"" % ra)
        elif x:
            st.ra_small += 1
        if abs(dec) > deadband:
            st.dec += dec
            acts.append("Dec %+.2f\"" % dec)
        elif y:
            st.dec_small += 1
        if not quiet:
            print("%s Guider X%d Y%d %s  -> %s" % (stamp(), x, y, digits,
                  ", ".join(acts) if acts else "below the dead band"))
        buf = buf[found + n:]


def run(a, st):
    while True:
        try:
            s = socket.create_connection((a.host, a.port), timeout=1.0)
        except OSError as e:
            print("%s cannot connect to %s:%d (%s)" % (stamp(), a.host, a.port, e))
            if a.as_today:
                return
            time.sleep(1.0)
            continue
        print("%s connection to guider opened (%s:%d)" % (stamp(), a.host, a.port))
        s.settimeout(None)
        buf, last = "", time.monotonic()
        while True:
            try:
                data = s.recv(4096)
            except OSError as e:
                data, why = b"", str(e)
            else:
                why = "closed by gcamzwo"
            if not data:
                print("%s guider connection lost (%s)" % (stamp(), why))
                break
            now = time.monotonic()
            if now - last >= AUTOFLUSH_S:
                buf = ""
            last = now
            buf += "".join(chr(c) for c in data if c > 12)
            buf = parse(buf, st, a.deadband, a.quiet)
        s.close()
        if a.as_today:
            print("%s the TCS stays disconnected until GUIDE false/true" % stamp())
            return
        time.sleep(1.0)


def json_reply(s):
    """One JSON object from the TCS: read until the braces balance."""
    buf, depth, started = b"", 0, False
    while not (started and depth == 0):
        c = s.recv(1)
        if not c:
            raise ConnectionError("the TCS closed the connection")
        buf += c
        if c == b"{":
            depth, started = depth + 1, True
        elif c == b"}":
            depth -= 1
    return json.loads(buf[buf.index(b"{"):].decode())


def relink(host, port):
    """GUIDE false, then GUIDE true: the TCS drops and reopens its guider link."""
    s = socket.create_connection((host, port), timeout=5.0)
    ok = True
    for enable in (False, True):
        s.sendall(json.dumps({"Command": "GUIDE", "ENABLE": enable}).encode())
        r = json_reply(s)
        print("GUIDE %-5s -> %s" % (str(enable).lower(), json.dumps(r)))
        ok = ok and bool(r.get("ok"))
    s.close()
    if not ok:
        print("the TCS did not reconnect: is gcamzwo serving gframe_port, and is "
              "Swope.ini's Guider_addr this host?")
    return 0 if ok else 1


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=10001)
    p.add_argument("--deadband", type=float, default=0.1,
                   help="Swope.ini Guider_deadband [arcsec] (default 0.1)")
    p.add_argument("--as-today", action="store_true",
                   help="do not reconnect after a drop, as the TCS does today")
    p.add_argument("-q", "--quiet", action="store_true", help="only the summary")
    p.add_argument("--relink", metavar="TCS",
                   help="send GUIDE false/true to the Swope TCS at TCS and exit")
    p.add_argument("--tcs-port", type=int, default=50000,
                   help="the TCS's JSON port for --relink (default 50000)")
    a = p.parse_args()
    if a.relink:
        return relink(a.relink, a.tcs_port)
    st = Stats()
    try:
        run(a, st)
    except KeyboardInterrupt:
        pass
    print("%d frames, %d ignored, %d corrupted; applied RA %+.2f\" Dec %+.2f\"; "
          "below the dead band: RA %d, Dec %d"
          % (st.frames, st.ignored, st.corrupt, st.ra, st.dec, st.ra_small, st.dec_small))


if __name__ == "__main__":
    sys.exit(main())
