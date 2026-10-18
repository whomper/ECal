#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ECal on an emulated Atari, through the Pi gateway, against a real
CalDAV server: the way an ST uses it.

Radicale (CalDAV) and gateway/ecalgate.py run on this computer. Hatari has
no network card, so FAKESTNG.PRG in C:\\AUTO carries the Atari's TCP
connections over the emulated serial port to serial_bridge.py
(common/tools/fakesting, make -C common/tools/fakesting). The accessory syncs, shows the server's Hebrew event, and an
event added on the Atari must arrive on the server.

    tests/hatari/net_test.py WORKDIR etos512us.img [--acc ECAL.ACC --machine falcon]

With --prg it is ECAL.PRG instead, started from C:\\ECAL: the program is
where the crash catcher is, so this is the run that shows STinG's way
into supervisor mode (FAKESTNG does it as STinG does) isn't taken for a
crash.
"""
import argparse
import base64
import os
import shutil
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
import rig  # noqa: E402
from rig import K, SCAN  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
RAD, GW = 15232, 18180
AUTH = "Basic " + base64.b64encode(b"alice:wonderland").decode()


SCHEME = "http"


def http(method, path, body=None, ctype=None):
    import ssl as _ssl
    rq = urllib.request.Request("%s://127.0.0.1:%d%s" % (SCHEME, RAD, path), data=body, method=method)
    rq.add_header("Authorization", AUTH)
    if ctype:
        rq.add_header("Content-Type", ctype)
    ctx = _ssl._create_unverified_context() if SCHEME == "https" else None
    with urllib.request.urlopen(rq, timeout=10, context=ctx) as r:
        return r.read().decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--acc", default="ECALST.ACC")
    ap.add_argument("--machine", default="st")
    ap.add_argument("--prg", action="store_true", help="ECAL.PRG instead of the accessory")
    ap.add_argument("--falcon-mode", action="store_true",
                    help="TLS on the Falcon itself (RSA on the DSP), no gateway")
    a = ap.parse_args()
    # the emulated clock is the host's local time; ECal reads it in its
    # home zone (Asia/Jerusalem, below), so the host's must be the same, or
    # the clock is hours out and a certificate made now not yet valid
    os.environ["TZ"] = "Asia/Jerusalem"
    time.tzset()
    if a.falcon_mode:
        a.machine, a.acc = "falcon", "ECAL.ACC"
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(os.path.join(hd, "AUTO"))
    os.makedirs(os.path.join(hd, "ECAL"))
    os.makedirs(os.path.join(work, "srv"))
    with open(os.path.join(work, "srv", "users"), "w") as f:
        f.write("alice:wonderland\n")
    procs = []
    fails = []

    def check(what, ok):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)

    ssl = []
    if a.falcon_mode:
        # the test certificate, for the address the Atari uses (the serial
        # bridge has no name lookup); the only root it can match
        cert, key = os.path.join(work, "srv", "cert.pem"), os.path.join(work, "srv", "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=DNS:127.0.0.1",
                        "-keyout", key, "-out", cert], check=True, capture_output=True)
        ssl = ["--server-ssl", "--server-certificate", cert, "--server-key", key]
        global SCHEME
        SCHEME = "https"
    procs.append(subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % RAD] + ssl + [
                                   "--storage-filesystem-folder", os.path.join(work, "srv", "store"),
                                   "--auth-type", "htpasswd", "--auth-htpasswd-filename",
                                   os.path.join(work, "srv", "users"), "--auth-htpasswd-encryption", "plain"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "ecalgate.py"), "--port", str(GW),
                                   "--bind", "127.0.0.1", "--allow-local", "-v"],
                                  stderr=open(os.path.join(work, "ecalgate.log"), "w")))
    for _ in range(50):
        try:
            http("GET", "/")
            break
        except Exception:
            time.sleep(0.2)
    http("MKCALENDAR", "/alice/home/", b'<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" '
         b'xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>\xd7\x91\xd7\x99\xd7\xaa'
         b'</D:displayname></D:prop></D:set></C:mkcalendar>', "application/xml")
    # weekly since 2020: inside the sync window whatever the emulated clock says
    # (an ST without a clock card starts at EmuTOS's build date)
    http("PUT", "/alice/home/server.ics", ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\nBEGIN:VEVENT\r\n"
         "UID:server-1\r\nDTSTAMP:20261001T000000Z\r\nDTSTART;TZID=Asia/Jerusalem:20200105T110000\r\n"
         "DTEND;TZID=Asia/Jerusalem:20200105T120000\r\nRRULE:FREQ=WEEKLY\r\nSUMMARY:\u05de\u05e8\u05d5\u05d1\u05d4 \u05d4\u05e9\u05e8\u05ea\r\n"
         "END:VEVENT\r\nEND:VCALENDAR\r\n").encode(), "text/calendar")

    if a.prg:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL", "ECAL.PRG"))
    else:
        shutil.copy(os.path.join(ROOT, "binary", a.acc), os.path.join(hd, "ECAL.ACC"))
    shutil.copy(os.path.join(ROOT, "common", "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
    if a.falcon_mode:
        # the Mozilla roots ECal ships, plus the test certificate, decoded here
        with open(os.path.join(hd, "ECAL", "CACERT.PEM"), "wb") as f:
            f.write(open(os.path.join(ROOT, "binary", "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
        subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(hd, "ECAL", "CACERT.PEM"),
                        os.path.join(hd, "ECAL", "ROOTS.DAT")], check=True, capture_output=True)
        conn = "falcon=1\r\ndsp=1\r\n"
    else:
        conn = "gate=127.0.0.1\r\ngateport=%d\r\n" % GW
    with open(os.path.join(hd, "ECAL", "ECAL.INF"), "w", newline="") as f:
        f.write("home_tz=Asia/Jerusalem\r\nview_tz=Asia/Jerusalem\r\n%s"
                "sync_open=1\r\nlog=1\r\n[account]\r\nname=Server\r\nkind=0\r\n"
                "url=%s://127.0.0.1:%d/\r\nuser=alice\r\npass=wonderland\r\n" % (conn, SCHEME, RAD))

    fifo_out, fifo_in = os.path.join(work, "st_out"), os.path.join(work, "st_in")
    for p in (fifo_out, fifo_in):
        os.mkfifo(p)
    procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "common", "tools", "fakesting", "serial_bridge.py"), fifo_out, fifo_in],
                                  stderr=open(os.path.join(work, "bridge.log"), "w")))
    extra = ["--memsize", "4" if a.machine == "st" else "14"]
    if a.machine == "st":
        extra += ["--monitor", "mono"]
    if a.falcon_mode:
        extra += ["--dsp", "emu"]
    r = rig.Rig(work, a.tos, machine=a.machine, serial=(fifo_out, fifo_in), extra=extra,
                auto="C:\\ECAL\\ECAL.PRG" if a.prg else None)
    try:
        time.sleep(18)
        if not a.prg:
            r.click(18, 8)
            time.sleep(1)
            r.click(60, 8 + 3 * 16 + 8)
        time.sleep(150 if a.falcon_mode else 60)   # the first sync, over a serial line (and TLS)
        r.shot("n1-synced")
        cache = ""
        for root, _, files in os.walk(os.path.join(hd, "ECAL")):
            for fn in files:
                if fn.endswith(".DAT"):
                    cache += open(os.path.join(root, fn), "rb").read().decode("utf-8", "replace")
        check("the server's Hebrew event arrived" + ("" if a.falcon_mode else " through the gateway"),
              "\u05de\u05e8\u05d5\u05d1\u05d4 \u05d4\u05e9\u05e8\u05ea" in cache)
        # an event made on the Atari goes to the server
        r.key(SCAN["n"])
        time.sleep(2)
        r.type("From the Atari")
        r.key(K["RETURN"])
        time.sleep(90 if a.falcon_mode else 40)
        r.shot("n2-added")
        srv = http("GET", "/alice/home/")
        check("the Atari's event is on the server", "SUMMARY:From the Atari" in srv)
        check("with its zone", "TZID=Asia/Jerusalem" in srv)
        if a.falcon_mode:
            log = open(os.path.join(hd, "ECAL", "ECAL.LOG"), "rb").read().decode("latin-1")
            check("Falcon mode: TLS on the Atari", "secure: TLS 1.2" in log)
            check("the signature checked on the DSP", "signature on the DSP" in log)
            check("no gateway used", "PUT" not in open(os.path.join(work, "ecalgate.log")).read())
        else:
            log = open(os.path.join(work, "ecalgate.log")).read()
            # ecalgate logs the server, never the path (a feed's path can be its password)
            check("it went through ecalgate", "PUT http://127.0.0.1:%d -> 201" % RAD in log)
            check("no path in its log", "/alice/home/" not in log)
        crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
        check("no crash (CRASH.LOG)", not crash)
        for c in crash:
            print(open(c, "rb").read().decode("latin-1"))
    finally:
        r.stop()
        for p in procs:
            p.terminate()
    print("screenshots in", os.path.join(work, "shots"))
    print("net test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
