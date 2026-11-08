#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Inviting people through EContact (the guidelines, 4.1: PEOPLE messages),
in Hatari on a Falcon: ECAL.PRG and a stand-in EContact
(tests/hatari/fakecontact.c, ECONTACT.ACC: the guidelines, 1.2, no other
AERA program in an app's tests; the real EContact with ECal is the hub's
family checks).

    tests/hatari/people_test.py WORK [--machine Falcon|TT|ST:mono] [--acc]

A new event's Add... asks EContact to pick (no question first); the
stand-in picks Grace Baker, as a user would in EContact's window, and the
editor comes back with her, by name.
Saved, the event invites her with her name and address; opened again,
she shows by the name ECal asks EContact for. The stand-in writes down
what it was asked (C:\\FAKECONT.TXT): a pick, and Grace's name. With --acc ECal is the
accessory (ECAL.ACC), opened from the Desk menu, as both usually are.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, K, SCAN, add_machine, machines  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def fake_econtact(hd):
    """the stand-in EContact (tests/hatari/fakecontact.c, the guidelines,
    1.2: no other AERA program in an app's tests) in the root of C:, as
    ECONTACT.ACC: the AES knows it as ECONTACT, and ECal asks it"""
    subprocess.run(["make", "-s", "-C", ROOT, "fakecontact"], check=True)
    shutil.copy(os.path.join(ROOT, "build", "ECONTACT.ACC"), os.path.join(hd, "econtact.acc"))


def fake_questions(hd):
    """the questions the stand-in was asked, a line each ("pick:ECAL ",
    "names:ECAL a@b c@d")"""
    f = machines.ci_path(hd, "fakecont.txt")
    return open(f, errors="replace").read().splitlines() if os.path.exists(f) else []


def cache_text(hd):
    t = ""
    for root, _, files in os.walk(hd):
        for f in files:
            if f.lower().endswith(".dat") and "cal" in f.lower():
                t += open(os.path.join(root, f), "rb").read().decode("utf-8", "replace")
    return re.sub(r"\r?\n[ \t]", "", t)		# lines unfolded


def menu_entry_width(shot, y):
    """how wide the Desk menu's entry at y is written, in pixels"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    px = im.load()
    # the entries' text only, short of the menu box's right edge (x 176):
    # the accessories come in the order TOS finds their files
    ink = [x for x in range(24, 160) for dy in range(-5, 6) if px[x, y + dy] < 128]
    return max(ink) - min(ink) + 1 if ink else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--acc", action="store_true", help="ECal as the accessory, opened from the Desk menu")
    a = ap.parse_args()
    work, acc = os.path.abspath(a.work), a.acc
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    if acc:
        shutil.copy(machines.ci_path(os.path.join(ROOT, "binary"), "ecal.acc"), os.path.join(hd, "ecal.acc"))
        os.makedirs(os.path.join(hd, "ecal"))
        ui_tour.seed(os.path.join(hd, "ecal"), english=True)
    else:
        shutil.copy(machines.ci_path(os.path.join(ROOT, "binary"), "ecal.prg"), os.path.join(hd, "ecal.prg"))
        ui_tour.seed(hd, english=True)
    fake_econtact(hd)
    r = Rig(work, a.machine, auto=None if acc else "C:\\ecal.prg")
    try:
        r.wait(20)
        if acc:
            # the two accessories in the Desk menu, in the order TOS found
            # them: ECal's entry is the shorter
            r.click(18, 8)
            r.wait(1)
            r.shot("0-desk-menu")
            first, second = 59, 75		# the rows' middles
            menu = os.path.join(work, "shots", "0-desk-menu.png")
            y = first if menu_entry_width(menu, first) < menu_entry_width(menu, second) else second
            r.click(60, y)
            r.wait(8)
        r.shot("1-month")
        r.key(SCAN["n"])			# a new event
        r.wait(2)
        r.type("Planning")
        r.shot("2-new")
        mono = r.machine.screen == "mono"
        box = MONO_PEOPLE_BOX if mono else PEOPLE_BOX
        r.click(*(MONO_ADD if mono else (ADD_X, ADD_Y)))	# Add...: EContact picks (the stand-in: Grace)
        r.wait(6)
        r.shot("4-back")			# the editor again, Grace Baker in People
        r.wait(10)
        r.shot("4b-later")
        r.key(K["RETURN"])			# OK: saved
        r.wait(5)
        r.shot("5-saved")
        # opened again: her name asked of EContact (people_name)
        r.ctrl("f")
        r.wait(1.5)
        for _ in range(20):
            r.key(K["BACKSPACE"])
        r.type("Planning")
        r.key(K["RETURN"])
        r.wait(3)
        r.key(K["RETURN"])			# the one found
        r.wait(2)
        r.key(K["RETURN"])			# open it
        r.wait(8)
        r.shot("6-opened")
        r.key(0x61)                   # Undo: the dialog cancelled (Esc empties a field, 3.5)
        r.wait(2)
    finally:
        r.stop()
    text = cache_text(hd)
    ev = text[text.find("SUMMARY:Planning") - 2000:] if "SUMMARY:Planning" in text else ""
    att = [l for l in ev.splitlines() if l.startswith("ATTENDEE")]
    check("the event is saved", "SUMMARY:Planning" in text)
    check("Grace invited, by address", any("mailto:grace.baker@example.net" in l for l in att))
    check("with her name from EContact", any("CN=Grace Baker" in l or 'CN="Grace Baker"' in l for l in att))
    shots = os.path.join(work, "shots")
    check("back from the pick, she shows by her name",
          text_width(os.path.join(shots, "4b-later.png"), box, inverse=True) in range(80, 100))
    check("opened again, her name asked of EContact",
          text_width(os.path.join(shots, "6-opened.png"), box) in range(80, 100))
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.lower() == "crash.log"]
    check("no crash (CRASH.LOG)", not crash)
    asked = fake_questions(hd)
    check("EContact asked to pick", any(q.startswith("pick:") for q in asked))
    check("and for Grace's name", any(q.startswith("names:") and "grace.baker@example.net" in q for q in asked))
    print("screenshots in", os.path.join(work, "shots"))
    print("people test, with the stand-in EContact:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


def text_width(shot, box, inverse=False):
    """how wide the first person's label is in box, in pixels (8 a
    character: "Grace Baker" 88, her address 184); its colour is black,
    or white on black when chosen"""
    from PIL import Image
    if not os.path.exists(shot):
        return 0
    im = Image.open(shot).convert("L").crop(box)
    w, h = im.size
    px = im.load()
    ink = [x for x in range(w) for y in range(h) if (px[x, y] > 128) == inverse]
    if inverse:			# white letters inside the black bar: up to where the bar ends
        bar = [x for x in range(w) if px[x, h // 2] < 128]
        return max(bar) - min(bar) + 1 if bar else 0
    return max(ink) - min(ink) + 1 if ink else 0


# where the editor's Add... button is on the Falcon's 640x480, and the
# first line of its People field
ADD_X, ADD_Y = 517, 125
PEOPLE_BOX = (170, 117, 466, 134)
# and on an ST's 640x400 (peoplefile_test.py takes them from here)
MONO_ADD = (517, 103)
MONO_PEOPLE_BOX = (170, 95, 466, 112)

if __name__ == "__main__":
    sys.exit(main())
