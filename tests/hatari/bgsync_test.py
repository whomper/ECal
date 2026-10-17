#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The accessory's background refresh (Settings > Refresh > Also with the
window closed), timed: how long it holds the machine between its event
calls while it brings a big calendar up to date with its window shut.

Radicale (CalDAV) and gateway/ecalgate.py run on this computer, the
Atari's connections going over the emulated serial port as in
net_test.py. The server has the Work calendar of tests/host/bigcal.py
(250 events), and the Atari a copy of it from an earlier refresh (made
here with cal-cli). Then the server changes behind the Atari's back:
three events new (one with a reminder the day after tomorrow), two
changed, one deleted. ECAL.ACC from make profile boots with START=20
and its window never opens; the rig checks the changes reached the
cache and the reminder ALARMS.IDX, and fails on any stretch between
event calls over 50 ms on a Falcon (100 ms on a Mega ST), except a new
connection's (with --falcon-mode, the TLS handshake on the Atari),
which is reported apart.

    make profile && tests/hatari/bgsync_test.py WORKDIR etos512us.img [--machine falcon|st] [--falcon-mode]
"""
import argparse
import base64
import datetime as dt
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
import rig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CLI = os.path.join(ROOT, "build", "cal-cli")
PROF = os.path.join(ROOT, "build", "prof", "bin")
RAD, GW = 15232, 18180
AUTH = "Basic " + base64.b64encode(b"alice:wonderland").decode()
SCHEME = "http"
NEW_EVENT = "Came in the background"


def http(method, path, body=None, ctype=None):
    import ssl as _ssl
    rq = urllib.request.Request("%s://127.0.0.1:%d%s" % (SCHEME, RAD, path), data=body, method=method)
    rq.add_header("Authorization", AUTH)
    if ctype:
        rq.add_header("Content-Type", ctype)
    ctx = _ssl._create_unverified_context() if SCHEME == "https" else None
    with urllib.request.urlopen(rq, timeout=10, context=ctx) as r:
        return r.read().decode("utf-8", "replace")


def split_ics(text):
    """a calendar file as CalDAV resources: the events by UID, each with
    the time zone"""
    head, rest = text.split("BEGIN:VEVENT", 1)
    vtz = head[head.index("BEGIN:VTIMEZONE"):] if "BEGIN:VTIMEZONE" in head else ""
    by_uid = {}
    for ev in ("BEGIN:VEVENT" + rest).split("BEGIN:VEVENT")[1:]:
        ev = "BEGIN:VEVENT" + ev.split("END:VCALENDAR")[0]
        uid = re.search(r"UID:([^\r\n]+)", ev).group(1)
        by_uid.setdefault(uid, []).append(ev)
    return {uid: "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\n" + vtz + "".join(evs) + "END:VCALENDAR\r\n"
            for uid, evs in by_uid.items()}


def event(uid, start, summary, alarm=False):
    s = start.strftime("%Y%m%dT%H%M%S")
    e = (start + dt.timedelta(minutes=30)).strftime("%Y%m%dT%H%M%S")
    return ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\nBEGIN:VEVENT\r\nUID:%s\r\n"
            "DTSTAMP:20261001T000000Z\r\nDTSTART;TZID=Asia/Jerusalem:%s\r\nDTEND;TZID=Asia/Jerusalem:%s\r\n"
            "SUMMARY:%s\r\n%sEND:VEVENT\r\nEND:VCALENDAR\r\n" %
            (uid, s, e, summary, "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n" if alarm else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--machine", default="falcon")
    ap.add_argument("--falcon-mode", action="store_true",
                    help="TLS on the Falcon itself (RSA on the DSP), no gateway")
    ap.add_argument("--secs", type=int, default=0, help="how long to watch after boot")
    ap.add_argument("--log", action="store_true",
                    help="ECAL.LOG on (off for the timings: writing it is time of its own)")
    a = ap.parse_args()
    # the emulator's clock and cal-cli read this computer's local time:
    # Israel time, as ECAL.INF says the Atari's clock is (a UTC clock
    # read as Israel time is three hours behind: certificates not yet valid)
    os.environ["TZ"] = "Asia/Jerusalem"
    time.tzset()
    if a.falcon_mode:
        a.machine = "falcon"
    limit = 50 if a.machine == "falcon" else 100
    secs = a.secs or (600 if a.falcon_mode else 420)
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    folder = os.path.join(hd, "ECAL")
    os.makedirs(os.path.join(hd, "AUTO"))
    os.makedirs(folder)
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
        global SCHEME
        cert, key = os.path.join(work, "srv", "cert.pem"), os.path.join(work, "srv", "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=DNS:127.0.0.1",
                        "-keyout", key, "-out", cert], check=True, capture_output=True)
        ssl = ["--server-ssl", "--server-certificate", cert, "--server-key", key]
        SCHEME = "https"
    procs.append(subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % RAD] + ssl + [
                                   "--storage-filesystem-folder", os.path.join(work, "srv", "store"),
                                   "--auth-type", "htpasswd", "--auth-htpasswd-filename",
                                   os.path.join(work, "srv", "users"), "--auth-htpasswd-encryption", "plain"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    procs.append(subprocess.Popen([sys.executable, os.path.join(ROOT, "gateway", "ecalgate.py"), "--port", str(GW),
                                   "--bind", "127.0.0.1", "--allow-local", "-v"],
                                  stderr=open(os.path.join(work, "ecalgate.log"), "w")))
    r = None
    try:
        for _ in range(50):
            try:
                http("GET", "/")
                break
            except Exception:
                time.sleep(0.2)
        http("MKCALENDAR", "/alice/work/", b'<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" '
             b'xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>Work'
             b'</D:displayname></D:prop></D:set></C:mkcalendar>', "application/xml")
        ics = os.path.join(work, "ics")
        subprocess.run([sys.executable, os.path.join(ROOT, "tests", "host", "bigcal.py"), ics],
                       check=True, stdout=subprocess.DEVNULL)
        res = split_ics(open(os.path.join(ics, "WORK.ICS"), newline="").read())
        uids = sorted(res)
        for i, uid in enumerate(uids):
            http("PUT", "/alice/work/w%03d.ics" % i, res[uid].encode(), "text/calendar")
        n_server = len(uids)

        # the Atari's copy, as an earlier refresh left it
        seed = os.path.join(work, "seed")
        env = dict(os.environ, CACERT=cert) if a.falcon_mode else None
        subprocess.run([CLI, seed, "zone", "Asia/Jerusalem"], check=True, stdout=subprocess.DEVNULL)
        if a.falcon_mode:
            subprocess.run([CLI, seed, "falcon", "1"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([CLI, seed, "caldav", "Server", "%s://127.0.0.1:%d/" % (SCHEME, RAD), "alice", "wonderland"],
                       check=True, stdout=subprocess.DEVNULL, env=env)
        shutil.copy(os.path.join(seed, "ECAL.KEY"), os.path.join(folder, "ECAL.KEY"))
        os.makedirs(os.path.join(folder, "CACHE"))
        for f in os.listdir(os.path.join(seed, "CACHE")):
            shutil.copy(os.path.join(seed, "CACHE", f), os.path.join(folder, "CACHE", f.upper()))
        with open(os.path.join(seed, "ECAL.INF"), newline="") as f:
            lines = [l for l in f.read().split("\r\n")
                     if not l.upper().startswith(("START=", "SYNC_OPEN=", "SYNC_CLOSED=", "LOG=",
                                                  "GATE=", "GATEPORT=", "FALCON=", "DSP="))]
        i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
        lines[i] = "FOLDER=C:\\ECAL"
        conn = ["FALCON=1", "DSP=1"] if a.falcon_mode else ["GATE=127.0.0.1", "GATEPORT=%d" % GW]
        lines[i + 1:i + 1] = ["START=20", "SYNC_OPEN=0", "SYNC_CLOSED=1", "LOG=%d" % a.log] + conn
        with open(os.path.join(hd, "ECAL.INF"), "w", newline="") as f:
            f.write("\r\n".join(lines))
        if a.falcon_mode:
            with open(os.path.join(folder, "CACERT.PEM"), "wb") as f:
                f.write(open(os.path.join(ROOT, "binary", "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
            subprocess.run([os.path.join(ROOT, "build", "mkroots"), os.path.join(folder, "CACERT.PEM"),
                            os.path.join(folder, "ROOTS.DAT")], check=True, capture_output=True)

        # the server changes meanwhile
        soon = dt.datetime.combine(dt.date.today() + dt.timedelta(days=2), dt.time(10, 0))
        http("PUT", "/alice/work/bg-1.ics", event("bg-1@test", soon, NEW_EVENT, alarm=True).encode(), "text/calendar")
        http("PUT", "/alice/work/bg-2.ics", event("bg-2@test", soon + dt.timedelta(hours=2), "Second new").encode(),
             "text/calendar")
        http("PUT", "/alice/work/bg-3.ics", event("bg-3@test", soon + dt.timedelta(days=1), "Third new").encode(),
             "text/calendar")
        # changed and deleted: among those the Atari keeps (past one-off
        # events aren't copied)
        kept = []
        for f in os.listdir(os.path.join(seed, "CACHE")):
            if f.upper().endswith(".DAT"):
                kept += re.findall(rb"^R [^\t]*\t/alice/work/w(\d+)\.ics\t", open(os.path.join(seed, "CACHE", f), "rb").read(), re.M)
        kept = sorted(int(k) for k in kept)
        print("the server has %d events, the Atari a copy of %d" % (n_server, len(kept)))
        for i in kept[:2]:
            text = res[uids[i]].replace("SUMMARY:", "SUMMARY:Changed ", 1)
            http("PUT", "/alice/work/w%03d.ics" % i, text.encode(), "text/calendar")
        gone = "/alice/work/w%03d.ics" % kept[2]
        http("DELETE", gone)

        acc = "ECAL.ACC" if a.machine == "falcon" else "ECALST.ACC"
        shutil.copy(os.path.join(PROF, acc), os.path.join(hd, "ECAL.ACC"))
        shutil.copy(os.path.join(ROOT, "tools", "fakesting", "FAKESTNG.PRG"), os.path.join(hd, "AUTO"))
        fifo_out, fifo_in = os.path.join(work, "st_out"), os.path.join(work, "st_in")
        for p in (fifo_out, fifo_in):
            os.mkfifo(p)
        procs.append(subprocess.Popen([sys.executable, os.path.join(HERE, "serial_bridge.py"), fifo_out, fifo_in],
                                      stderr=open(os.path.join(work, "bridge.log"), "w")))
        extra = ["--memsize", "14" if a.machine == "falcon" else "4"]
        if a.machine == "st":
            extra += ["--monitor", "mono"]
        if a.falcon_mode:
            extra += ["--dsp", "emu"]
        r = rig.Rig(work, a.tos, machine="megast" if a.machine == "st" else a.machine,
                    serial=(fifo_out, fifo_in), extra=extra, auto=None)
        time.sleep(secs)
        r.shot("desktop")
    finally:
        if r:
            r.stop()
        for p in procs:
            p.terminate()

    cache = ""
    for root, _, files in os.walk(os.path.join(folder, "CACHE")):
        for fn in files:
            if fn.endswith(".DAT"):
                cache += open(os.path.join(root, fn), "rb").read().decode("utf-8", "replace")
    check("the new events reached the cache", all(t in cache for t in (NEW_EVENT, "Second new", "Third new")))
    check("the changed ones too", cache.count("SUMMARY:Changed ") == 2)
    check("the deleted one marked gone", "X %s" % gone in cache)
    idx = os.path.join(folder, "ALARMS.IDX")
    check("the new event's reminder in ALARMS.IDX",
          os.path.exists(idx) and NEW_EVENT in open(idx, "rb").read().decode("latin-1"))
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)

    log = open(os.path.join(work, "hatari.log"), errors="replace").read()
    if "AESPROF ECal" not in log:
        check("the make profile build (an AESPROF line on the console)", False)
    gaps = [(int(m.group(1)), m.group(2).strip()) for m in re.finditer(r"AESGAP (\d+) ms ([^\n]*)", log)]
    worst, count = {}, {}
    for ms, what in gaps:
        worst[what] = max(ms, worst.get(what, 0))
        count[what] = count.get(what, 0) + 1
    print("%s%s: %d stretches of 50 ms or more between event calls" %
          (a.machine, ", Falcon mode" if a.falcon_mode else ", through the gateway", len(gaps)))
    apart = "work: refresh (new connection)"
    for what, ms in sorted(worst.items(), key=lambda x: -x[1]):
        note = " (%d)" % count[what]
        if what == apart:
            note += "   apart: a new connection" + (" (the TLS handshake)" if a.falcon_mode else "")
        elif ms > limit:
            note += "   OVER %d ms" % limit
        print("  %5d ms  %s%s" % (ms, what, note))
    over = [g for g in gaps if g[0] > limit and g[1] != apart]
    check("everything else at most %d ms" % limit, not over)
    print("screenshots in", os.path.join(work, "shots"))
    print("bgsync test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
