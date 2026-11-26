#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ECal in Hatari, end to end: as a desk accessory, add an event with
an English title and one with a Hebrew title typed on the Israeli
keyboard, change one, delete it, and check what ECAL.ACC wrote to its
cache (the iCalendar text that would go to the server).

    tests/hatari/edit_test.py WORKDIR [--machine Falcon|TT|ST-2:mono|ST-2:med|...]

The accessory is ECAL.ACC on every machine (TLS.OVL beside it on a TT or Falcon).
Exit status 0 when every check passes.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN, add_machine, is_st, machines  # noqa: E402
import ui_tour  # noqa: E402


def cache_records(data):
    """The events in a CALnnnnn.DAT as ECal reads it: a changed event is
    added again further on (the later copy wins), a deleted one gets an
    "X href" line."""
    recs, pos = {}, data.find(b"\n") + 1      # past "CALCACHE 1"
    while pos < len(data):
        nl = data.find(b"\n", pos)
        if nl < 0:
            break
        line = data[pos:nl].decode("utf-8", "replace")
        pos = nl + 1
        if line.startswith("X "):
            recs.pop(line[2:], None)
        elif line.startswith("R "):
            etag, href, n = line[2:].split("\t")
            recs[href] = data[pos:pos + int(n)]
            pos += int(n) + 1
        elif not line.startswith("C "):
            break
    return recs


def cache_text(hd):
    out = ""
    cache = machines.ci_path(hd, "ecal/cache")
    for f in sorted(os.path.join(cache, n) for n in os.listdir(cache) if n.lower().endswith(".dat")):
        for raw in cache_records(open(f, "rb").read()).values():
            out += "\nR " + raw.decode("utf-8", "replace")
    return out


ENDS_TIME = (311, 213)      # the event's Ends time on Falcon:16
ROW_1H30 = 52               # the list's third row (1\u00bd hours), below it


def empty(r):
    """the field with the cursor emptied: to its end, then back over it
    all (Undo cancels the dialog: guideline 3.5)"""
    r.key(0x47, shift=True)
    for _ in range(60):
        r.key(K["BACKSPACE"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(os.path.join(hd, "ecal"))
    shutil.copy(machines.ci_path(os.path.join(ui_tour.ROOT, "binary"), "ecal.acc"),
                os.path.join(hd, "ecal.acc"))
    ui_tour.seed(os.path.join(hd, "ecal"))
    fails = []

    def check(what, ok):
        print(("  ok   " if ok else "  FAIL ") + what)
        if not ok:
            fails.append(what)

    r = Rig(a.work, a.machine, auto=None)
    try:
        r.wait(16)
        r.click(18, 8)
        r.wait(1)
        r.click(60, r.ecal_row())
        r.wait(6)
        # 1. a new event: N, type the title, Return adds it
        r.key(SCAN["n"])
        r.wait(2)
        r.type("Dentist")
        r.key(K["TAB"])
        r.type("Main st")
        r.key(K["RETURN"])
        r.wait(4)
        r.shot("1-added")
        text = cache_text(hd)
        check("the new event is saved", "SUMMARY:Dentist" in text)
        check("with its place", "LOCATION:Main st" in text)
        check("in a time zone", "DTSTART;TZID=Asia/Jerusalem:" in text)
        # 2. a Hebrew title on the Israeli keyboard (F10): "shalom" = shin lamed vav final-mem
        r.key(SCAN["n"])
        r.wait(2)
        r.key(K["F10"])
        r.wait(0.5)
        # a=shin k=lamed u=vav o=final mem on SI-1452 (slower on an 8 MHz ST,
        # which can drop a key while it draws the last)
        r.type("akuo", pace=0.4 if is_st(a.machine) else 0.15)
        r.key(K["F10"])
        r.wait(0.5)
        r.shot("2-hebrew-typed")
        r.key(K["RETURN"])
        r.wait(4)
        text = cache_text(hd)
        check("the Hebrew title is saved as UTF-8", "SUMMARY:שלום" in text)
        r.shot("3-hebrew-added")
        # 3. find it again and delete it: select with Tab until it is the Hebrew one is
        #    fiddly by keys; Find (^F) puts the selection on it
        r.ctrl("f")
        r.wait(1.5)
        r.type("Dentist")
        r.key(K["RETURN"])
        r.wait(2)
        r.key(K["RETURN"])            # choose the one found
        r.wait(2)
        r.shot("4-found")
        r.key(K["DELETE"])
        r.wait(1.5)
        r.shot("5-confirm")
        # the alert's Delete button (Cancel is the default): where each screen puts it
        r.click(283, {"mono": 231, "med": 249}.get(r.machine.screen, 271))
        r.wait(4)
        text = cache_text(hd)
        check("the event is deleted", "SUMMARY:Dentist" not in text)
        check("the Hebrew one is still there", "SUMMARY:שלום" in text)
        r.shot("6-deleted")
        # 4 and 5 click where a 640x480 screen puts the dialogs, and an
        # 8 MHz ST searches a daily series slower than they allow
        if not is_st(a.machine):
            # 4. one occurrence of a repeating event: open today's Standup, rename
            #    it, "Only this"; then change that same occurrence again
            def open_found(what):
                r.ctrl("f")
                r.wait(1.5)
                r.key(K["BACKSPACE"])          # the field remembers the last search
                for _ in range(20):
                    r.key(K["BACKSPACE"], )
                r.type(what)
                r.key(K["RETURN"])
                # searching three years of a daily series: the screen stands still
                # under the busy pointer meanwhile, which wait() can't tell from done
                r.wait(2)
                r.key(K["RETURN"])             # the first from today on
                r.wait(2)
                r.key(K["RETURN"])             # open it
                r.wait(2)

            def standup_resource():
                text = cache_text(hd)
                for part in text.split("\nR "):
                    if "UID:" in part and "SUMMARY:Standup" in part:
                        return part
                return ""

            open_found("Standup")
            r.shot("7-open-occurrence")
            empty(r)                           # the title
            r.type("Standup moved")
            r.key(K["RETURN"])                 # OK
            r.wait(1.5)
            r.shot("8-only-this")
            r.key(K["RETURN"])                 # the alert's default: Only this
            r.wait(4)
            res = standup_resource()
            check("one occurrence changed: an exception", res.count("RECURRENCE-ID") == 1 and "SUMMARY:Standup moved" in res)
            check("the series keeps its title", "SUMMARY:Standup\r\n" in res)
            open_found("Standup moved")
            empty(r)
            r.type("Standup moved twice")
            r.key(K["RETURN"])
            r.wait(1.5)
            r.key(K["RETURN"])
            r.wait(4)
            res = standup_resource()
            check("changed again: still one exception", res.count("RECURRENCE-ID") == 1)
            check("with the new title", "SUMMARY:Standup moved twice" in res)
            r.shot("9-moved-twice")
            # 5. the Custom repeat dialog
            r.key(SCAN["n"])
            r.wait(2)
            r.shot("10-new")
            r.click(276, 257)                  # Repeat
            r.wait(1)
            r.shot("11-repeat-menu")
            r.click(276, 401)                  # Custom...
            r.wait(2)
            r.shot("12-custom")
            # monthly, on the last Friday, by the fields
            r.click(250, 92)                   # every ... weeks: the unit
            r.wait(1)
            r.click(250, 110)                  # months: a row below "weeks" (ticked), which opens over the button (D27)
            r.wait(1)
            r.click(98, 180)                   # "the"
            r.wait(0.5)
            r.click(215, 180)                  # first ...
            r.wait(1)
            r.click(215, 271)                  # last
            r.wait(1)
            r.click(360, 180)                  # Sunday ...
            r.wait(1)
            r.click(360, 271)                  # Friday
            r.wait(1)
            r.shot("13-custom-last-friday")
            r.key(K["RETURN"])                 # OK
            r.wait(1.5)
            r.shot("14-described")
            r.click(360, 81)                   # the title
            r.type("Book club")
            r.key(K["RETURN"])                 # Add
            r.wait(4)
            text = cache_text(hd)
            check("a custom rule from the fields: the last Friday of each month",
                  "SUMMARY:Book club" in text and "RRULE:FREQ=MONTHLY;BYDAY=-1FR" in text)
            # a rule typed as text
            r.key(SCAN["n"])
            r.wait(2)
            r.click(276, 257)
            r.wait(1)
            r.click(276, 401)
            r.wait(2)
            r.click(340, 355)                  # the Rule line
            empty(r)
            r.type("FREQ=YEARLY;BYMONTH=3;BYDAY=TH")
            r.key(K["RETURN"])
            r.wait(1.5)
            r.click(360, 81)
            r.type("March Thursdays")
            r.key(K["RETURN"])
            r.wait(4)
            text = cache_text(hd)
            check("a rule typed into the Rule line", "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=TH" in text)
            r.shot("15-month-after")
            # 6. the month under a date field (decided 24 Nov 2026): a click
            #    opens it, Esc closes it, typing goes to the field, the arrows
            #    and Return take a day
            r.key(SCAN["n"])
            r.wait(2)
            for at, key, want in (((215, 190), 0x4d, "11.03"), ((215, 213), 0x50, "17.03")):
                r.click(*at)                   # the picker opens
                r.wait(1.5)
                r.key(K["ESC"])                # closed, the field keeps the cursor
                r.wait(0.5)
                empty(r)
                r.type("10.03.2027")
                r.click(*at)                   # March 2027, the 10th chosen
                r.wait(1.5)
                r.shot("16-picker-" + want)
                r.key(key)                     # a day, a week ahead
                r.key(K["RETURN"])             # taken
                r.wait(1)
            r.shot("17-picked")
            r.click(360, 81)
            r.type("Picked")
            r.key(K["RETURN"])
            r.wait(4)
            text = cache_text(hd)
            picked = [p for p in text.split("\nR ") if "SUMMARY:Picked" in p]
            check("dates taken from the picker: the 11th to the 17th of March 2027",
                  bool(picked) and ":20270311T" in picked[0] and ":20270317T" in picked[0])
            # 7. a start typed and left with Tab moves the end by as many days
            #    (decided 24 Nov 2026): the end comes to March 2027 with it
            r.key(SCAN["n"])
            r.wait(2)
            r.click(215, 190)
            r.wait(1.5)
            r.key(K["ESC"])
            r.wait(0.5)
            empty(r)
            r.type("10.03.2027")
            r.key(K["TAB"])
            r.wait(1)
            r.shot("18-end-followed")
            r.click(360, 81)
            r.type("Followed")
            r.key(K["RETURN"])
            r.wait(4)
            text = cache_text(hd)
            fol = [p for p in text.split("\nR ") if "SUMMARY:Followed" in p]
            check("the end follows a typed start into March 2027",
                  bool(fol) and ":20270310T" in fol[0] and "DTEND;TZID=Asia/Jerusalem:202703" in fol[0])
            # 8. the end times under the Ends time (decided 26 Nov 2026):
            #    every half hour after the start to 3 hours; 1½ hours after
            #    22:30 ends at midnight, the next day
            r.key(SCAN["n"])
            r.wait(2)
            r.click(215, 190)
            r.wait(1.5)
            r.key(K["ESC"])
            r.wait(0.5)
            empty(r)
            r.type("10.03.2027")
            r.key(K["TAB"])                    # the start time
            empty(r)
            r.type("22:30")
            r.key(K["TAB"])
            r.wait(1)
            r.click(*ENDS_TIME)                # the list opens under it
            r.wait(1.5)
            r.shot("19-end-times")
            r.click(ENDS_TIME[0], ENDS_TIME[1] + ROW_1H30)   # 1½ hours
            r.wait(1)
            r.click(*ENDS_TIME)                # again: 00:00 (1½ hours) ticked
            r.wait(1.5)
            r.shot("20-end-times-ticked")
            r.key(K["ESC"])
            r.wait(0.5)
            r.click(360, 81)
            r.type("Late")
            r.key(K["RETURN"])
            r.wait(4)
            text = cache_text(hd)
            late = [p for p in text.split("\nR ") if "SUMMARY:Late" in p]
            check("an end time from the list: 1½ hours after 22:30, at midnight the next day",
                  bool(late) and "DTSTART;TZID=Asia/Jerusalem:20270310T223000" in late[0]
                  and "DTEND;TZID=Asia/Jerusalem:20270311T000000" in late[0])
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))
    print("edit test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
