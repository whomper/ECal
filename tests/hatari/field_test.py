#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A text field holds more than it shows (the guidelines, 5.4): in a new
event, a title of 100 characters typed into the 47-wide Title field,
Shift+left and Shift+right to its two ends (a letter typed at each),
^C on it and ^V into Location, and an address of 77 characters typed
into the 30-wide field Add... asks for when EContact isn't loaded.
The event saved has the whole of each.

    tests/hatari/field_test.py WORK [--machine Falcon|TT|ST:mono|...] [--acc]
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from notes_test import open_cache  # noqa: E402
from people_test import ADD_X, ADD_Y  # noqa: E402
from peoplefile_test import MONO_ADD  # noqa: E402
from rig import Rig, K, SCAN, add_machine, is_st  # noqa: E402

TITLE = " ".join("word%02d" % i for i in range(1, 14)) + " end of it"   # 100 characters
ADDRESS = "firstname.middlename.lastname@department-of-long-names.example-university.org"
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
    assert len(TITLE) == 100 and len(ADDRESS) > 70
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    if a.acc:
        acc = "ECAL.ACC"
        shutil.copy(os.path.join(ui_tour.ROOT, "binary", acc), os.path.join(hd, "ECAL.ACC"))
        top = os.path.join(hd, "ECAL")
        os.makedirs(top)
    else:
        shutil.copy(os.path.join(ui_tour.ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
        top = hd
    ui_tour.seed(top, english=True)
    r = Rig(work, a.machine, auto=None if a.acc else "C:\\ECAL.PRG")
    try:
        r.wait(16)
        if a.acc:
            r.click(18, 8)
            r.wait(1)
            r.click(60, r.ecal_row())
            r.wait(6)
        r.key(SCAN["n"])                # a new event, the cursor in its Title
        r.wait(2)
        r.type(TITLE)
        r.shot("1-typed")               # its end in sight
        r.key(K["LEFT"], shift=True)    # to the start
        r.type("A")
        r.shot("2-start")
        r.key(K["RIGHT"], shift=True)   # to the end
        r.type("Z")
        r.ctrl("c")                     # the whole title
        r.key(K["TAB"])                 # Location
        r.ctrl("v")
        r.wait(1)
        r.shot("3-pasted")
        add = MONO_ADD if r.machine.screen == "mono" else (ADD_X, ADD_Y)
        r.click(*add)                   # Add...: no EContact, so the address is asked for
        r.wait(3)
        r.type(ADDRESS)
        r.shot("4-address")
        r.key(K["RETURN"])
        r.wait(3)
        r.shot("5-back")
        r.key(K["RETURN"])              # OK: saved
        r.wait(5)
    finally:
        r.stop()
    t = open_cache(top)
    whole = "A" + TITLE + "Z"
    check("the title whole, a letter at each end (%d characters)" % len(whole), "SUMMARY:" + whole in t)
    check("Location: the whole title, copied and pasted", "LOCATION:" + whole in t)
    check("the address whole (%d characters)" % len(ADDRESS), "mailto:" + ADDRESS in t.replace("\r\n ", ""))
    crash = [f for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("screenshots in", os.path.join(work, "shots"))
    print("field test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
