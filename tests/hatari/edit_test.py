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
        # 4. one occurrence of a repeating event: open today's Standup, rename
        #    it, "Only this"; then change that same occurrence again
        def open_found(what):
            r.ctrl("f")
            time.sleep(1.5)
            r.key(K["BACKSPACE"])          # the field remembers the last search
            for _ in range(20):
                r.key(K["BACKSPACE"], )
            r.type(what)
            r.key(K["RETURN"])
            time.sleep(2)
            r.key(K["RETURN"])             # the first from today on
            time.sleep(2)
            r.key(K["RETURN"])             # open it
            time.sleep(2)

        def standup_resource():
            text = cache_text(hd)
            for part in text.split("\nR "):
                if "UID:" in part and "SUMMARY:Standup" in part:
                    return part
            return ""

        open_found("Standup")
        r.shot("7-open-occurrence")
        r.key(0x61)                        # Undo: empty the title
        r.type("Standup moved")
        r.key(K["RETURN"])                 # OK
        time.sleep(1.5)
        r.shot("8-only-this")
        r.key(K["RETURN"])                 # the alert's default: Only this
        time.sleep(4)
        res = standup_resource()
        check("one occurrence changed: an exception", res.count("RECURRENCE-ID") == 1 and "SUMMARY:Standup moved" in res)
        check("the series keeps its title", "SUMMARY:Standup\r\n" in res)
        open_found("Standup moved")
        r.key(0x61)
        r.type("Standup moved twice")
        r.key(K["RETURN"])
        time.sleep(1.5)
        r.key(K["RETURN"])
        time.sleep(4)
        res = standup_resource()
        check("changed again: still one exception", res.count("RECURRENCE-ID") == 1)
        check("with the new title", "SUMMARY:Standup moved twice" in res)
        r.shot("9-moved-twice")
        # 5. the Custom repeat dialog
        r.key(SCAN["n"])
        time.sleep(2)
        r.shot("10-new")
        r.click(276, 235)                  # Repeat
        time.sleep(1)
        r.shot("11-repeat-menu")
        r.click(276, 401)                  # Custom...
        time.sleep(2)
        r.shot("12-custom")
        # monthly, on the last Friday, by the fields
        r.click(250, 92)                   # every ... weeks: the unit
        time.sleep(1)
        r.click(250, 187)                  # months
        time.sleep(1)
        r.click(98, 180)                   # "the"
        time.sleep(0.5)
        r.click(215, 180)                  # first ...
        time.sleep(1)
        r.click(215, 293)                  # last
        time.sleep(1)
        r.click(360, 180)                  # Sunday ...
        time.sleep(1)
        r.click(360, 293)                  # Friday
        time.sleep(1)
        r.shot("13-custom-last-friday")
        r.key(K["RETURN"])                 # OK
        time.sleep(1.5)
        r.shot("14-described")
        r.click(360, 81)                   # the title
        r.type("Book club")
        r.key(K["RETURN"])                 # Add
        time.sleep(4)
        text = cache_text(hd)
        check("a custom rule from the fields: the last Friday of each month",
              "SUMMARY:Book club" in text and "RRULE:FREQ=MONTHLY;BYDAY=-1FR" in text)
        # a rule typed as text
        r.key(SCAN["n"])
        time.sleep(2)
        r.click(276, 235)
        time.sleep(1)
        r.click(276, 401)
        time.sleep(2)
        r.click(340, 355)                  # the Rule line
        r.key(0x61)                        # Undo: empty it
        r.type("FREQ=YEARLY;BYMONTH=3;BYDAY=TH")
        r.key(K["RETURN"])
        time.sleep(1.5)
        r.click(360, 81)
        r.type("March Thursdays")
        r.key(K["RETURN"])
        time.sleep(4)
        text = cache_text(hd)
        check("a rule typed into the Rule line", "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=TH" in text)
        r.shot("15-month-after")
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))
    print("edit test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
