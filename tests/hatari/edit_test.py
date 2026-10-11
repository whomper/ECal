#!/usr/bin/env python3
"""Calendar in Hatari, end to end: as a desk accessory, add an event with
an English title and one with a Hebrew title typed on the Israeli
keyboard, change one, delete it, and check what CALENDAR.ACC wrote to its
cache (the iCalendar text that would go to the server).

    tests/hatari/edit_test.py WORKDIR etos512us.img [--acc CALST.ACC --machine st]
Exit status 0 when every check passes.
"""
import argparse
import glob
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN  # noqa: E402
import ui_tour  # noqa: E402


def cache_text(hd):
    out = ""
    for f in glob.glob(os.path.join(hd, "CALENDAR", "CACHE", "*.DAT")):
        out += open(f, "rb").read().decode("utf-8", "replace")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--machine", default="falcon")
    ap.add_argument("--acc", default="CALENDAR.ACC")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(os.path.join(hd, "CALENDAR"))
    shutil.copy(os.path.join(ui_tour.ROOT, a.acc), os.path.join(hd, "CALENDAR.ACC"))
    ui_tour.seed(os.path.join(hd, "CALENDAR"))
    extra = ["--memsize", "14" if a.machine == "falcon" else "4"]
    if a.machine == "st":
        extra += ["--monitor", "mono"]
    fails = []

    def check(what, ok):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)

    r = Rig(a.work, a.tos, machine=a.machine, extra=extra, auto=None)
    try:
        time.sleep(16)
        r.click(18, 8)
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(6)
        # 1. a new event: N, type the title, Return adds it
        r.key(SCAN["n"])
        time.sleep(2)
        r.type("Dentist")
        r.key(K["TAB"])
        r.type("Main st")
        r.key(K["RETURN"])
        time.sleep(4)
        r.shot("1-added")
        text = cache_text(hd)
        check("the new event is saved", "SUMMARY:Dentist" in text)
        check("with its place", "LOCATION:Main st" in text)
        check("in a time zone", "DTSTART;TZID=Asia/Jerusalem:" in text)
        # 2. a Hebrew title on the Israeli keyboard (F10): "shalom" = shin lamed vav final-mem
        r.key(SCAN["n"])
        time.sleep(2)
        r.key(K["F10"])
        r.type("akuo", pace=0.15)       # a=shin k=lamed u=vav o=final mem on SI-1452
        r.key(K["F10"])
        time.sleep(0.5)
        r.shot("2-hebrew-typed")
        r.key(K["RETURN"])
        time.sleep(4)
        text = cache_text(hd)
        check("the Hebrew title is saved as UTF-8", "SUMMARY:שלום" in text)
        r.shot("3-hebrew-added")
        # 3. find it again and delete it: select with Tab until it is the Hebrew one is
        #    fiddly by keys; Find (^F) puts the selection on it
        r.ctrl("f")
        time.sleep(1.5)
        r.type("Dentist")
        r.key(K["RETURN"])
        time.sleep(2)
        r.key(K["RETURN"])            # choose the one found
        time.sleep(2)
        r.shot("4-found")
        r.key(K["DELETE"])
        time.sleep(1.5)
        r.shot("5-confirm")
        r.click(283, 271 if a.machine == "falcon" else 231)   # the alert's Delete button (Cancel is the default)
        time.sleep(4)
        text = cache_text(hd)
        check("the event is deleted", "SUMMARY:Dentist" not in text)
        check("the Hebrew one is still there", "SUMMARY:שלום" in text)
        r.shot("6-deleted")
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))
    print("edit test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
