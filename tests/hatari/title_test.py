#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The toolbar's title fits: on a 640-wide screen with the calendar list
shown, a long title takes a shorter form ("18-24 Oct", "Sep 2026")
instead of being cut ("18-24 Oct 202"). For each view and date below,
Go to date (^G) and the view's key, then the width of the title's ink
is measured against the form expected (8 pixels a character).

    tests/hatari/title_test.py WORK [--machine Falcon|TT|ST:mono|...]
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from edit_test import empty  # noqa: E402
from people_test import text_width  # noqa: E402
from rig import Rig, K, SCAN, add_machine  # noqa: E402

# the title's place in the toolbar, between the → button and Day
TITLE_BOX = (161, 66, 284, 80)
# (date, view key, the title expected); the week starts on Sunday
CASES = [
    ("22.10.2026", "w", "18-24 Oct"),           # "18-24 Oct 2026" is a character too long
    ("30.9.2026", "w", "27 Sep-3 Oct"),
    ("30.9.2026", "d", "Wed 30 Sep"),
    ("30.9.2026", "m", "Sep 2026"),             # "September 2026"
    ("22.10.2026", "m", "October 2026"),        # fits as it is
]
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(ui_tour.ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    ui_tour.seed(hd, english=True)
    r = Rig(work, a.machine, auto="C:\\ECAL.PRG")
    shots = []
    try:
        r.wait(16)
        for i, (date, view, want) in enumerate(CASES):
            r.ctrl("g")
            r.wait(1.5)
            empty(r)                    # the date shown, typed over
            r.type(date)
            r.key(K["RETURN"])
            r.wait(2)
            r.key(SCAN[view])
            r.wait(2)
            shots.append(r.shot("%d-%s-%s" % (i + 1, view, date)))
    finally:
        r.stop()
    for (date, view, want), shot in zip(CASES, shots):
        w = text_width(shot, TITLE_BOX)
        check("%s, %s: \"%s\" whole (%d px, %d expected)" % (date, view.upper(), want, w, 8 * len(want)),
              8 * len(want) - 4 <= w <= 8 * len(want) + 1)
    print("screenshots in", os.path.join(work, "shots"))
    print("title test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
