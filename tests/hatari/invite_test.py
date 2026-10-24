#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""An .ics invitation handed to ECal by VA_START, as EMail does with one it
received (or the desktop with a file opened): the window opens with the
New Event dialog filled from it, and Return (OK) saves it as a new event
with the invitation's UID, title and times.

THING.ACC from common/tests/av (make -C common/tests/av) stands in for
the sender: it waits for C:\\GO.TXT, then sends ECal VA_START with
C:\\INVITE.ICS. ECal is ECAL.PRG, or with --acc the accessory, its window
closed until the invitation comes.

    tests/hatari/invite_test.py WORK [--machine Falcon|TT|ST:mono|...] [--acc]
"""
import argparse
import datetime as dt
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from people_test import cache_text  # noqa: E402
from rig import Rig, K, add_machine, is_st  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
AV = os.path.join(ROOT, "common", "tests", "av")
UID = "invite-88@example.org"
TITLE = "Board meeting"
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--acc", action="store_true", help="ECal as the accessory")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    subprocess.run(["make", "-s", "-C", AV, "build/THING.ACC"], check=True)
    shutil.copy(os.path.join(AV, "build", "THING.ACC"), os.path.join(hd, "THING.ACC"))
    if a.acc:
        acc = "ECAL.ACC"
        shutil.copy(os.path.join(ROOT, "binary", acc), os.path.join(hd, "ECAL.ACC"))
        os.makedirs(os.path.join(hd, "ECAL"))
        ui_tour.seed(os.path.join(hd, "ECAL"), english=True)
    else:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
        ui_tour.seed(hd, english=True)
    day = (dt.date.today() + dt.timedelta(days=3)).strftime("%Y%m%d")
    with open(os.path.join(hd, "INVITE.ICS"), "w", newline="") as f:
        f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\n"
                "UID:%s\r\nDTSTAMP:20261020T100000Z\r\nDTSTART:%sT140000Z\r\nDTEND:%sT150000Z\r\n"
                "SUMMARY:%s\r\nLOCATION:Room 4\r\nORGANIZER;CN=Alice:mailto:alice@example.org\r\n"
                "ATTENDEE;CN=Bob;PARTSTAT=NEEDS-ACTION:mailto:bob@example.org\r\n"
                "END:VEVENT\r\nEND:VCALENDAR\r\n" % (UID, day, day, TITLE))
    with open(os.path.join(hd, "AVSEND.TXT"), "w", newline="") as f:
        f.write("await GO.TXT\r\nsend ECAL|C:\\INVITE.ICS\r\n")
    r = Rig(work, a.machine, auto=None if a.acc else "C:\\ECAL.PRG")
    try:
        r.wait(25)
        r.shot("1-before")
        open(os.path.join(hd, "GO.TXT"), "w").close()
        r.wait(12)
        r.shot("2-dialog")
        r.key(K["RETURN"])			# OK
        r.wait(8)
        r.shot("3-saved")
    finally:
        r.stop()
    shots = os.path.join(work, "shots")
    print("screenshots in", shots)
    log = open(os.path.join(hd, "AVLOG.TXT"), errors="replace").read() if os.path.exists(
        os.path.join(hd, "AVLOG.TXT")) else ""
    check("VA_START sent, AV_STARTED answered", "AV_STARTED" in log)
    t = cache_text(hd)
    ev = ""
    m = re.search(r"BEGIN:VEVENT\r?\n(?:(?!END:VEVENT).)*UID:%s(?:(?!END:VEVENT).)*END:VEVENT" % re.escape(UID), t,
                  re.S)
    if m:
        ev = m.group(0)
    check("saved with the invitation's UID", bool(ev))
    check("with its title", "SUMMARY:%s" % TITLE in ev)
    check("at its time", "%sT140000Z" % day in ev or "DTSTART" in ev and day in ev)
    check("with its place", "LOCATION:Room 4" in ev)
    check("the organiser kept", "ORGANIZER;CN=Alice:mailto:alice@example.org" in ev)
    check("no METHOD in the calendar's copy", "METHOD:" not in t)
    crash = [f for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("invite test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
