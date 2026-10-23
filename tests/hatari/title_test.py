#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The toolbar's title fits: on a 640-wide screen with the calendar list
shown, a long title takes a shorter form ("18-24 Oct", "Sep 2026")
instead of being cut ("18-24 Oct 202"). For each view and date below,
Go to date (^G) and the view's key, then the width of the title's ink
(8 pixels a character) must be one of the title's forms, whole: which
one depends on the room the AES leaves (13 characters under EmuTOS, 14
under TOS 1.04).

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

# the title's place in the toolbar, between the → button and Day, up
# from the toolbar's bottom line
TITLE_X = (161, 284)
# (date, view key, its forms, longest first); the week starts on Sunday
CASES = [
    ("22.10.2026", "w", ["18 - 24 October 2026", "18-24 Oct 2026", "18-24 Oct"]),
    ("30.9.2026", "w", ["27 Sep - 3 Oct 2026", "27 Sep-3 Oct 2026", "27 Sep-3 Oct"]),
    ("30.9.2026", "d", ["Wednesday, 30 September 2026", "Wed 30 Sep 2026", "Wed 30 Sep", "30 Sep"]),
    ("30.9.2026", "m", ["September 2026", "Sep 2026", "Sep"]),
    ("22.10.2026", "m", ["October 2026", "Oct 2026", "Oct"]),
]


def title_box(shot):
    """the title's box: above the toolbar's bottom line, the last black
    line from the → button past Day before the view (y 84 under EmuTOS,
    79 under TOS 1.04)"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    px = im.load()
    lines = [y for y in range(30, 100) if all(px[x, y] < 128 for x in range(150, 300))]
    y = lines[-1] if lines else 84
    return (TITLE_X[0], y - 18, TITLE_X[1], y - 4)
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
        for i, (date, view, forms) in enumerate(CASES):
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
    for (date, view, forms), shot in zip(CASES, shots):
        w = text_width(shot, title_box(shot))
        whole = [f for f in forms if 8 * len(f) - 4 <= w <= 8 * len(f) + 1 and 8 * len(f) <= TITLE_X[1] - TITLE_X[0]]
        check("%s, %s: a form drawn whole (%d px: %s)" % (date, view.upper(), w, '"%s"' % whole[0] if whole else "cut"),
              bool(whole))
    print("screenshots in", os.path.join(work, "shots"))
    print("title test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
