#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The day view shows a day's events whichever way it is reached: from the
week view, from the day next to it, or opened on its own at the start.

A day view taken from the week's list kept the week's table of each
event's day, and showed only the events whose place in the week's list
fell on the same day (fixed 25 Nov 2026). Made-up events on today and the
two days after (a weekly series, single ones, a long one, two at the same
time); each day view reached through the week is compared with the same
day opened alone in a new start, its hours and details pixel by pixel.

    tests/hatari/dayweek_test.py WORKDIR [--machine Falcon|TT|ST:mono|...]

Screenshots in WORKDIR/shots.
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, K, SCAN, add_machine, machines  # noqa: E402

fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def day(n):
    return time.strftime("%Y%m%d", time.localtime(time.time() + n * 86400))


def more(cli):
    cli("add", "0", "Swim", day(-21) + "T0900", "90", "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR,SA,SU")
    cli("add", "1", "Call the bank", day(1) + "T1100", "60")
    cli("add", "1", "Write the report", day(1) + "T1200", "75")
    cli("add", "1", "Send it", day(1) + "T1330", "90")
    cli("add", "2", "Club evening", day(0) + "T1400", "210")
    cli("add", "2", "Film", day(0) + "T2000", "90")
    cli("add", "0", "Tidy up", day(0) + "T2000", "30")
    cli("add", "2", "Workshop", day(2) + "T1000", "300")


def grid(path):
    """the hours and the details: below the toolbar, right of the calendar
    list (its 'Updated' time and the pointer left out)"""
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    return im.crop((w * 3 // 10, h // 6, w - 24, h - 4)).tobytes()


def start(work, machine, name):
    r = Rig(os.path.join(work, name), machine, auto="C:\\ecal.prg")
    r.wait(16)
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    shots = {}
    for name in ("through", "alone0", "alone1", "alone2"):
        hd = os.path.join(work, name, "hd")
        os.makedirs(hd)
        shutil.copy(machines.ci_path(os.path.join(ui_tour.ROOT, "binary"), "ecal.prg"), os.path.join(hd, "ecal.prg"))
        ui_tour.seed(hd, english=True, more=more)
    # through the week: the week, its day view (today), the next day, the
    # week again and the day after it by the arrow
    r = start(work, a.machine, "through")
    try:
        r.key(SCAN["w"])
        r.wait(4)
        r.move(10, 10)
        r.shot("week")
        r.key(SCAN["d"])
        r.wait(4)
        shots[0] = r.shot("through-0")
        r.key(K["RIGHT"])
        r.wait(4)
        shots[1] = r.shot("through-1")
        r.key(SCAN["w"])
        r.wait(4)
        r.key(SCAN["d"])
        r.wait(4)
        r.key(K["RIGHT"])
        r.wait(4)
        shots[2] = r.shot("through-2")
    finally:
        r.stop()
    # each day opened alone, in a start of its own
    for n in (0, 1, 2):
        r = start(work, a.machine, "alone%d" % n)
        try:
            r.key(SCAN["d"])
            r.wait(4)
            for _ in range(n):
                r.key(K["RIGHT"])
                r.wait(4)
            r.move(10, 10)
            alone = r.shot("alone-%d" % n)
        finally:
            r.stop()
        check("day %d through the week shows what it shows alone" % n, grid(shots[n]) == grid(alone))
    print("screenshots in", os.path.join(work, "*", "shots"))
    print("day/week test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
