#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc and Undo (the guidelines, 3.5, GEM's way since 27 Oct 2026), in
Hatari on a Falcon with ECAL.PRG:

    tests/hatari/esc_test.py WORK [--machine Falcon|Falcon:256|TT]

- a new event (^N), a title typed, then Esc: the title field emptied and
  the dialog still open; then Undo: closed, no event added;
- Settings (^,), a box ticked (no text field on the page), then Esc, and
  again with Undo: ECAL.INF as it was;
- Delete on an event, then Esc at "Delete ...?", and again with Undo:
  the alert closes as Cancel (common/ui/alert.c), and nothing is deleted;
- Find (^F), a word typed, then Esc: the field emptied, the dialog open;
  then Undo: nothing found or opened.
Nothing ECal keeps on disk (ECAL.INF, the calendars) may change.
Screenshots in WORK/shots.
"""
import argparse
import hashlib
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, K, SCAN, add_machine  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
UNDO = 0x61
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def changed(work, a, b):
    """the share of the screen that differs between two screenshots"""
    from PIL import Image
    x = Image.open(os.path.join(work, "shots", a + ".png")).convert("L")
    y = Image.open(os.path.join(work, "shots", b + ".png")).convert("L")
    w, h = x.size
    px, py = x.load(), y.load()
    n = sum(1 for j in range(0, h, 2) for i in range(0, w, 2) if px[i, j] != py[i, j])
    return n / float((w // 2) * (h // 2))


def files(hd):
    """ECAL.INF and the calendars, as they are now"""
    out = {}
    for root, _, names in os.walk(hd):
        for n in names:
            if n.upper() == "ECAL.INF" or n.upper().endswith((".DAT", ".ICS")):
                p = os.path.join(root, n)
                out[os.path.relpath(p, hd)] = hashlib.sha1(open(p, "rb").read()).hexdigest()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    ui_tour.seed(hd, english=True)
    r = Rig(work, a.machine)
    try:
        r.wait(20)
        r.shot("1-month")
        before = files(hd)

        def unchanged(what):
            now = files(hd)
            check(what, now == before)

        r.key(SCAN["n"], ctrl=True)		# a new event
        r.wait(2)
        r.shot("2-new-empty")
        r.type("Cancelled")
        r.shot("2-new-typed")
        r.key(K["ESC"])
        r.wait(1.5)
        r.shot("2-new-esc")
        d = changed(work, "2-new-empty", "2-new-esc")
        check("a new event, Esc in the title: emptied, the dialog open (%.2f%% from before typing)" % (100 * d),
              d < 0.005 and changed(work, "2-new-typed", "2-new-esc") > 0)
        r.key(UNDO)
        r.wait(3)
        r.shot("2-new-undo")
        check("a new event, Undo: the dialog closed",
              changed(work, "2-new-esc", "2-new-undo") > 0.1)
        unchanged("a new event, Esc then Undo: nothing added")
        for key, name in ((K["ESC"], "Esc"), (UNDO, "Undo")):
            r.ctrl(",")				# Settings
            r.wait(2)
            r.click(325, 257)			# Show Hebrew dates: ticked or not, a change
            r.wait(1)
            r.shot("3-settings-" + name)
            r.key(key)
            r.wait(5)				# (a change would be written 3 s after)
            unchanged("Settings, %s: ECAL.INF as it was" % name)
        for key, name in ((K["ESC"], "Esc"), (UNDO, "Undo")):
            r.key(K["TAB"])			# an event chosen
            r.wait(1)
            r.key(K["DELETE"])
            r.wait(2)
            r.shot("4-delete-" + name)
            r.key(key)
            r.wait(3)
            unchanged("Delete, %s at the question: nothing deleted" % name)
        r.ctrl("f")
        r.wait(1.5)
        r.shot("5-find")
        r.type("Standup")
        r.key(K["ESC"])
        r.wait(1.5)
        r.shot("5-find-esc")
        check("Find, Esc: the field emptied, the dialog open",
              changed(work, "5-find", "5-find-esc") < 0.005)
        r.key(UNDO)
        r.wait(2)
        r.shot("5-find-undo")
        unchanged("Find, Undo: nothing changed")
        r.shot("6-end")
    finally:
        r.stop()
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("screenshots in", os.path.join(work, "shots"))
    print("esc test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
