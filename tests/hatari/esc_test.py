#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Esc cancels (the guidelines, 3.5), in Hatari on a Falcon with ECAL.PRG:

    tests/hatari/esc_test.py WORK [--machine Falcon|Falcon:256|TT]

- a new event (^N), a title typed, then Esc: no event added; the same
  with Undo;
- Settings (^,), a box ticked, then Esc, and again with Undo: ECAL.INF
  as it was;
- Delete on an event, then Esc at "Delete ...?", and again with Undo:
  the alert closes as Cancel (common/ui/alert.c), and nothing is deleted;
- Find (^F), a word typed, then Esc: nothing found or opened.
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

        for key, name in ((K["ESC"], "Esc"), (UNDO, "Undo")):
            r.key(SCAN["n"], ctrl=True)		# a new event
            r.wait(2)
            r.type("Cancelled " + name)
            r.shot("2-new-" + name)
            r.key(key)
            r.wait(3)
            unchanged("a new event, %s: nothing added" % name)
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
        r.type("Standup")
        r.key(K["ESC"])
        r.wait(2)
        r.shot("5-find-esc")
        unchanged("Find, Esc: nothing changed")
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
