#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The day view's divider: the line between the hours and the selected
event's details can be dragged, and the width is kept (PANES= in
ECAL.INF) for the next start.

An event with long notes is selected in the day view; the divider is
found in the screenshot and dragged 100 pixels to the left; the details
pane is then wider by that, and after a quit and a new start it is
still where it was let go.

    tests/hatari/divider_test.py WORKDIR [--machine Falcon|TT|ST:mono|...]

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

NOTES = ("The long notes of a meeting, " * 12).strip()
MOVE = 100
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def divider_x(shot):
    """the divider's left edge: 4 columns alike from the pane's top to its
    foot, neither white nor black (grey in colour), or 4 dotted ones (in
    mono and 4 colours: half black, half white)"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    w, h = im.size
    px = im.load()
    ys = range(h // 3, h * 5 // 6)

    def col(x):
        return tuple(px[x, y] for y in ys)

    def plain(c):
        return set(c) <= {0} or set(c) <= {255}
    def dotted(x):
        c = col(x)
        k = sum(1 for v in c if v < 128) / len(c)
        return 0.3 < k < 0.7 and len(set(c)) == 2
    for x in range(w // 4, w - 40):
        c = col(x)
        if plain(c):
            continue
        if all(col(x + i) == c for i in range(1, 4)) and col(x + 4) != c and col(x - 1) != c:
            return x
        if all(dotted(x + i) for i in range(4)) and not dotted(x - 1) and not dotted(x + 4):
            return x
    return None


def inf_value(top, key):
    path = machines.ci_path(top, "ecal.inf")
    if not os.path.exists(path):
        return None
    for line in open(path, errors="replace"):
        if line.upper().startswith(key + "="):
            return line.split("=", 1)[1].strip()
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    day = time.strftime("%Y%m%d", time.localtime(time.time() + 86400))
    ics = os.path.join(work, "notes.ics")
    with open(ics, "w", newline="") as f:
        f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nBEGIN:VEVENT\r\n"
                "UID:divider-1@example.org\r\nDTSTAMP:20261020T100000Z\r\nDTSTART:%sT110000\r\n"
                "DTEND:%sT120000\r\nSUMMARY:Planning\r\nDESCRIPTION:%s\r\n"
                "END:VEVENT\r\nEND:VCALENDAR\r\n" % (day, day, NOTES))
    shutil.copy(machines.ci_path(os.path.join(ui_tour.ROOT, "binary"), "ecal.prg"), os.path.join(hd, "ecal.prg"))
    top = hd
    ui_tour.seed(top, english=True, more=lambda cli: cli("import", "1", ics))
    x0 = x1 = None
    r = Rig(work, a.machine, auto="C:\\ecal.prg")
    try:
        r.wait(16)
        r.key(SCAN["d"])              # the day view
        r.wait(3)
        r.key(K["RIGHT"])             # tomorrow, the event's day
        r.wait(3)
        r.click(25, 73)               # the calendars hidden: room for the details
        r.wait(3)
        r.click(200, 245)             # the event at 11:00 (floating) selected: its details shown
        r.wait(3)
        s = r.shot("1-details")
        x0 = divider_x(s)
        check("the divider drawn (at x %s)" % x0, x0 is not None)
        if x0 is not None:
            w, h = r.size()
            y = h * 2 // 3
            r.drag(x0 + 1, y, x0 + 1 - MOVE, y, secs=1.0)
            r.move(w - 60, 60)        # (the pointer off the divider for the picture)
            r.wait(4)
            s = r.shot("2-dragged")
            x1 = divider_x(s)
            check("the divider moved %d to the left (now at x %s)" % (MOVE, x1),
                  x1 is not None and abs((x0 - x1) - MOVE) <= 2)
        r.ctrl("q")
        r.wait(6)
    finally:
        r.stop()
    panes = inf_value(top, "PANES")
    check("PANES= in ECAL.INF (%s)" % panes, panes is not None and panes.isdigit() and int(panes) > 0)
    if x1 is not None:
        r = Rig(work, a.machine, auto="C:\\ecal.prg")
        try:
            r.wait(16)
            r.key(K["RIGHT"])         # (the day view and the hidden calendars kept)
            r.wait(3)
            s = r.shot("3-next-start")
            x2 = divider_x(s)
            check("after a new start it is where it was let go (x %s)" % x2, x2 == x1)
            r.ctrl("q")
            r.wait(6)
        finally:
            r.stop()
    print("divider test:", "all passed" if not fails else "%d failed" % len(fails))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
