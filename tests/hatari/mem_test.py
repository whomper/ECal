#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""How much of its heap the accessory needs: CALS calendars of EVENTS
events each come new to it in one background refresh, and ECAL.LOG's
"memory:" line at the round's end says the peak (guidelines 2.1, the
accessory's memory).

Radicale and gateway/ecalgate.py as in bgsync_test.py. The Atari knows
the account and its calendars while they are still empty; then the
events arrive on the server, so the refresh brings every one of them at
once, as a calendar added on the server would. Events fall from today
to a year ahead (the window ECal copies), some of them weekly series
from the past, some with reminders, attendees and longer descriptions.
ECAL.ACC from make profile, LOG=1, window never opened.

    make profile && tests/hatari/mem_test.py WORKDIR [--machine ST:mono|Falcon|...] [--falcon-mode]
                                             [--cals 3] [--events 500] [--heapkb N]
"""
import argparse
import datetime as dt
import os
import random
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import rig  # noqa: E402
import bgsync_test as bg  # noqa: E402

NAMES = ["Work", "Home", "Family", "Club", "Studio", "School"]
WORDS = ("standup review planning dentist lunch call Atari repair concert lesson"
         " train flight meeting doctor dinner party football shopping yoga build").split()


VTZ = ("BEGIN:VTIMEZONE\r\nTZID:Asia/Jerusalem\r\nBEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0200\r\n"
       "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1FR\r\nDTSTART:20130329T020000\r\nTZNAME:IDT\r\n"
       "TZOFFSETTO:+0300\r\nEND:DAYLIGHT\r\nBEGIN:STANDARD\r\nTZOFFSETFROM:+0300\r\n"
       "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nDTSTART:20131027T020000\r\nTZNAME:IST\r\n"
       "TZOFFSETTO:+0200\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n")
ZOOM = ("Alice is inviting you to a scheduled Zoom meeting. Join Zoom Meeting https://us02web.zoom.us/j/"
        "81234567890?pwd=QWERTYuiopASDFghjklZXCVbnm123456 Meeting ID: 812 3456 7890 Passcode: 123456 "
        "One tap mobile +16465588656,,81234567890#,,,,*123456# US (New York) +13017158592,,81234567890#"
        ",,,,*123456# US (Washington DC) Dial by your location +1 646 558 8656 US (New York) +1 301 715 "
        "8592 US (Washington DC) +1 312 626 6799 US (Chicago) +1 669 900 9128 US (San Jose) +1 253 215 "
        "8782 US (Tacoma) +1 346 248 7799 US (Houston) Find your local number: https://us02web.zoom.us/"
        "u/kbCdEfGhIj Join by SIP 81234567890@zoomcrc.com Join by H.323 162.255.37.11 (US West) "
        "162.255.36.11 (US East) 115.114.131.7 (India Mumbai) 115.114.115.7 (India Hyderabad) "
        "213.19.144.110 (Amsterdam Netherlands) 213.244.140.110 (Germany) 103.122.166.55 (Australia "
        "Sydney) 149.137.40.110 (Singapore) 64.211.144.160 (Brazil) 149.137.68.253 (Mexico) 69.174.57.160 "
        "(Canada Toronto) 65.39.152.160 (Canada Vancouver) 207.226.132.110 (Japan Tokyo) Meeting ID: "
        "812 3456 7890 Passcode: 123456")


def fold(line):
    """an iCalendar line folded at 75 octets"""
    out = [line[:75]]
    line = line[75:]
    while line:
        out.append(" " + line[:74])
        line = line[74:]
    return "\r\n".join(out)


def events(cal, n, rnd, icloud=False):
    today = dt.date.today()
    out = []
    for i in range(n):
        uid = "%s-%04d@memtest" % (cal.lower(), i)
        series = i % 10 == 0
        day = today + dt.timedelta(days=rnd.randrange(-200, 0) if series else rnd.randrange(0, 360))
        start = dt.datetime.combine(day, dt.time(rnd.randrange(7, 20), rnd.choice((0, 15, 30, 45))))
        end = start + dt.timedelta(minutes=rnd.choice((30, 45, 60, 90, 120)))
        title = " ".join(rnd.choice(WORDS) for _ in range(rnd.randrange(1, 5))).capitalize()
        ev = ["BEGIN:VEVENT", "UID:" + uid, "DTSTAMP:20261001T000000Z",
              "DTSTART;TZID=Asia/Jerusalem:" + start.strftime("%Y%m%dT%H%M%S"),
              "DTEND;TZID=Asia/Jerusalem:" + end.strftime("%Y%m%dT%H%M%S"),
              "SUMMARY:%s %d" % (title, i)]
        if series:
            ev.append("RRULE:FREQ=WEEKLY;COUNT=%d" % rnd.randrange(40, 80))
        if i % 4 == 0:
            ev.append("LOCATION:Room %d, Tel Aviv" % rnd.randrange(1, 40))
        if i % 3 == 0:
            ev.append("DESCRIPTION:" + " ".join(rnd.choice(WORDS) for _ in range(rnd.randrange(5, 40))))
        if i % 7 == 0:
            ev.append("ORGANIZER;CN=Alice:mailto:alice@example.org")
            ev += ["ATTENDEE;CN=Person %d;PARTSTAT=ACCEPTED:mailto:p%d@example.org" % (k, k)
                   for k in range(rnd.randrange(1, 5))]
        if i % 5 == 0:
            ev += ["BEGIN:VALARM", "ACTION:DISPLAY", "TRIGGER:-PT15M", "END:VALARM"]
        if icloud:
            # as iCloud keeps them: the zone in each, Apple's own lines,
            # and now and then a video call's invitation in the notes
            ev[1:1] = ["CREATED:20260101T090000Z", "LAST-MODIFIED:20261001T090000Z", "SEQUENCE:1",
                       "X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC", "TRANSP:OPAQUE"]
            if i % 8 == 1:
                ev = [l for l in ev if not l.startswith("DESCRIPTION:")]
                ev.insert(-1, fold("DESCRIPTION:" + ZOOM))
            if i % 5 == 0:
                ev.insert(-1, "X-WR-ALARMUID:%s-alarm" % uid)
        ev.append("END:VEVENT")
        out.append((uid, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:memtest\r\n" + (VTZ if icloud else "")
                    + "\r\n".join(ev) + "\r\nEND:VCALENDAR\r\n"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    rig.add_machine(ap, "Falcon")
    ap.add_argument("--falcon-mode", action="store_true",
                    help="TLS on the Falcon itself (RSA on the DSP), no gateway")
    ap.add_argument("--cals", type=int, default=3)
    ap.add_argument("--events", type=int, default=500, help="events in each calendar")
    ap.add_argument("--icloud", action="store_true",
                    help="events as iCloud keeps them: the zone in each, Apple's lines, some long notes")
    ap.add_argument("--heapkb", type=int, default=0, help="HEAPKB= in ECAL.INF (0: ECal's own choice)")
    ap.add_argument("--secs", type=int, default=0, help="how long to watch after boot")
    ap.add_argument("--boots", type=int, default=1,
                    help="start the Atari again this many times in all: the next start takes MEMKB=")
    a = ap.parse_args()
    os.environ["TZ"] = "Asia/Jerusalem"
    time.tzset()
    if a.falcon_mode and rig.is_st(a.machine):
        a.machine = "Falcon"
    secs = a.secs or (1500 if a.falcon_mode else 1200)
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
    cert = None
    if a.falcon_mode:
        cert, key = os.path.join(work, "srv", "cert.pem"), os.path.join(work, "srv", "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                        "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=DNS:127.0.0.1",
                        "-keyout", key, "-out", cert], check=True, capture_output=True)
        ssl = ["--server-ssl", "--server-certificate", cert, "--server-key", key]
        bg.SCHEME = "https"
    procs.append(subprocess.Popen([sys.executable, "-m", "radicale", "--server-hosts", "127.0.0.1:%d" % bg.RAD] + ssl + [
                                   "--storage-filesystem-folder", os.path.join(work, "srv", "store"),
                                   "--auth-type", "htpasswd", "--auth-htpasswd-filename",
                                   os.path.join(work, "srv", "users"), "--auth-htpasswd-encryption", "plain"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    procs.append(subprocess.Popen([sys.executable, os.path.join(bg.ROOT, "gateway", "ecalgate.py"), "--port", str(bg.GW),
                                   "--bind", "127.0.0.1", "--allow-local", "-v"],
                                  stderr=open(os.path.join(work, "ecalgate.log"), "w")))
    r = None
    try:
        for _ in range(50):
            try:
                bg.http("GET", "/")
                break
            except Exception:
                time.sleep(0.2)
        names = [NAMES[i % len(NAMES)] + ("" if i < len(NAMES) else str(i)) for i in range(a.cals)]
        for name in names:
            bg.http("MKCALENDAR", "/alice/%s/" % name.lower(), (
                '<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav">'
                '<D:set><D:prop><D:displayname>%s</D:displayname></D:prop></D:set></C:mkcalendar>' % name).encode(),
                "application/xml")

        # the Atari knows the account and its calendars, still empty
        seed = os.path.join(work, "seed")
        env = dict(os.environ, CACERT=cert) if a.falcon_mode else None
        subprocess.run([bg.CLI, seed, "zone", "Asia/Jerusalem"], check=True, stdout=subprocess.DEVNULL)
        if a.falcon_mode:
            subprocess.run([bg.CLI, seed, "falcon", "1"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([bg.CLI, seed, "caldav", "Server", "%s://127.0.0.1:%d/" % (bg.SCHEME, bg.RAD), "alice",
                        "wonderland"], check=True, stdout=subprocess.DEVNULL, env=env)
        shutil.copy(os.path.join(seed, "ECAL.KEY"), os.path.join(folder, "ECAL.KEY"))
        os.makedirs(os.path.join(folder, "CACHE"))
        for f in os.listdir(os.path.join(seed, "CACHE")):
            shutil.copy(os.path.join(seed, "CACHE", f), os.path.join(folder, "CACHE", f.upper()))
        with open(os.path.join(seed, "ECAL.INF"), newline="") as f:
            lines = [l for l in f.read().split("\r\n")
                     if not l.upper().startswith(("START=", "SYNC_OPEN=", "SYNC_CLOSED=", "LOG=", "HEAPKB=",
                                                  "GATE=", "GATEPORT=", "FALCON=", "DSP="))]
        i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
        lines[i] = "FOLDER=C:\\ECAL"
        conn = ["FALCON=1", "DSP=1"] if a.falcon_mode else ["GATE=127.0.0.1", "GATEPORT=%d" % bg.GW]
        heap = ["HEAPKB=%d" % a.heapkb] if a.heapkb else []
        lines[i + 1:i + 1] = ["START=20", "SYNC_OPEN=0", "SYNC_CLOSED=1", "LOG=1"] + heap + conn
        with open(os.path.join(hd, "ECAL.INF"), "w", newline="") as f:
            f.write("\r\n".join(lines))
        if a.falcon_mode:
            with open(os.path.join(folder, "CACERT.PEM"), "wb") as f:
                f.write(open(os.path.join(bg.ROOT, "binary", "CACERT.PEM"), "rb").read() + open(cert, "rb").read())
            subprocess.run([os.path.join(bg.ROOT, "build", "mkroots"), os.path.join(folder, "CACERT.PEM"),
                            os.path.join(folder, "ROOTS.DAT")], check=True, capture_output=True)

        # the events arrive on the server
        rnd = random.Random(1960)
        total = 0
        for name in names:
            for uid, text in events(name, a.events, rnd, a.icloud):
                bg.http("PUT", "/alice/%s/%s.ics" % (name.lower(), uid.split("@")[0]), text.encode(), "text/calendar")
                total += 1
        print("the server has %d calendars of %d events (%d), the Atari none of them yet" %
              (a.cals, a.events, total))

        acc = "ECALST.ACC" if rig.is_st(a.machine) else "ECAL.ACC"
        shutil.copy(os.path.join(bg.PROF, acc), os.path.join(hd, "ECAL.ACC"))
        logf = os.path.join(folder, "ECAL.LOG")
        for boot in range(a.boots):
            # until the round's end is logged, or the time is up
            before = open(logf, "rb").read().count(b"memory:") if os.path.exists(logf) else 0
            r = rig.Rig(work, a.machine, network=True, auto=None)
            t0 = time.time()
            while time.time() - t0 < secs:
                time.sleep(15)
                if os.path.exists(logf) and open(logf, "rb").read().count(b"memory:") > before:
                    time.sleep(10)
                    break
            r.shot("desktop-%d" % (boot + 1))
            if boot + 1 < a.boots:
                r.stop()
                r = None
    finally:
        if r:
            r.stop()
        for p in procs:
            p.terminate()

    log = open(logf, "rb").read().decode("latin-1") if os.path.exists(logf) else ""
    for l in log.splitlines():
        if re.search(r"at boot|memory|Memory|out of memory|events in", l):
            print("  log: " + l.strip())
    cache = ""
    for root, _, files in os.walk(os.path.join(folder, "CACHE")):
        for fn in files:
            if fn.endswith(".DAT"):
                cache += open(os.path.join(root, fn), "rb").read().decode("utf-8", "replace")
    kept = len(re.findall(r"^R ", cache, re.M))
    print("events in the Atari's cache: %d (%d KB)" % (kept, len(cache.encode("utf-8", "replace")) // 1024))
    m = re.findall(r"memory: (\d+) KB in use, (\d+) KB at the most, of (\d+) KB.*next start (\d+) KB", log)
    check("the round's end logged its memory", bool(m))
    for k, x in enumerate(m):
        use, peak, taken, nxt = (int(y) for y in x)
        print("%s%s, round %d: %d KB in use, peak %d KB, of %d KB taken; MEMKB=%d for the next start" %
              (a.machine, ", Falcon mode" if a.falcon_mode else ", through the gateway", k + 1, use, peak, taken, nxt))
    for l in log.splitlines():
        if "!!" in l and "HTTP" not in l:
            print("  problem: " + l.strip())
    inf = open(os.path.join(hd, "ECAL.INF"), "rb").read().decode("latin-1")
    mk = re.search(r"^MEMKB=(\d+)", inf, re.M)
    check("MEMKB= written to ECAL.INF for the next start", bool(mk and int(mk.group(1)) > 0))
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("mem test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
