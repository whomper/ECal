#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""An event's notes, which ECal doesn't keep in memory but reads from its
cache when they are wanted: Find looks in them, the day's details and
the event dialog show them, and an event changed in the dialog is
written with them.

    tests/hatari/notes_test.py WORKDIR [--machine Falcon|TT|ST:mono|...] [--acc]

ECAL.PRG, or with --acc the accessory.
Screenshots in WORKDIR/shots: 6-details shows the notes in the day's details.
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from edit_test import cache_records, empty  # noqa: E402
from rig import Rig, K, SCAN, add_machine, is_st, machines  # noqa: E402

NOTES = "Bring the Falcon and the spare PSU"
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
    day = time.strftime("%Y%m%d", time.localtime(time.time() + 86400))
    ics = os.path.join(work, "notes.ics")
    with open(ics, "w", newline="") as f:
        f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nBEGIN:VEVENT\r\n"
                "UID:notes-1@example.org\r\nDTSTAMP:20261020T100000Z\r\nDTSTART:%sT150000Z\r\n"
                "DTEND:%sT160000Z\r\nSUMMARY:Repair cafe\r\nDESCRIPTION:%s\\nand the scope\r\n"
                "END:VEVENT\r\nEND:VCALENDAR\r\n" % (day, day, NOTES))
    if a.acc:
        shutil.copy(machines.ci_path(os.path.join(ui_tour.ROOT, "binary"), "ecal.acc"), os.path.join(hd, "ecal.acc"))
        os.makedirs(os.path.join(hd, "ecal"))
        top = os.path.join(hd, "ecal")
    else:
        shutil.copy(machines.ci_path(os.path.join(ui_tour.ROOT, "binary"), "ecal.prg"), os.path.join(hd, "ecal.prg"))
        top = hd
    ui_tour.seed(top, english=True, more=lambda cli: cli("import", "1", ics))
    check("seeded with its notes", "DESCRIPTION:" + NOTES in open_cache(top))
    r = Rig(work, a.machine, auto=None if a.acc else "C:\\ecal.prg")
    try:
        r.wait(16)
        if a.acc:
            r.click(18, 8)
            r.wait(1)
            r.click(60, r.ecal_row())
            r.wait(6)
        r.shot("1-start")
        # Find a word that is only in the notes
        r.ctrl("f")
        r.wait(1.5)
        r.type("spare PSU")
        r.key(K["RETURN"])
        r.wait(3)
        r.shot("2-list")
        r.key(K["RETURN"])            # the one found: selected, its details shown
        r.wait(3)
        r.shot("3-found")
        r.key(K["RETURN"])            # opened
        r.wait(3)
        r.shot("4-dialog")
        empty(r)                      # the title
        r.type("Repair cafe moved")
        r.key(K["RETURN"])            # OK
        r.wait(5)
        r.shot("5-saved")
        r.key(SCAN["d"])              # the day, with the selected event's details
        r.wait(3)
        r.click(33, 73)               # the calendars hidden: room for the details
        r.wait(3)
        r.click(150, 246)             # the event at 11:00
        r.wait(3)
        r.shot("6-details")
    finally:
        r.stop()
    print("screenshots in", os.path.join(work, "shots"))
    t = open_cache(top)
    check("Find found it by its notes, and the dialog saved it", "SUMMARY:Repair cafe moved" in t)
    check("written with its notes", "DESCRIPTION:" + NOTES in t)
    check("both lines of them", "\\nand the scope" in t)
    crash = [f for d, _, fs in os.walk(hd) for f in fs if f.lower() == "crash.log"]
    check("no crash (CRASH.LOG)", not crash)
    print("notes test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


def open_cache(top):
    """the events in top's CACHE (the folder ECal reads), unfolded"""
    out = ""
    cache = machines.ci_path(top, "cache")
    for f in sorted(os.path.join(cache, n) for n in os.listdir(cache) if n.lower().endswith(".dat")):
        for raw in cache_records(open(f, "rb").read()).values():
            out += "\nR " + raw.decode("utf-8", "replace").replace("\r\n ", "")
    return out


if __name__ == "__main__":
    sys.exit(main())
