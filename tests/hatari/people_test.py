#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Inviting people with EContact (the guidelines, 4.1: PEOPLE messages),
in Hatari on a Falcon: ECAL.PRG, and EContact's accessory with the
contacts of its tests/hatari/sample.vcf.

    tests/hatari/people_test.py WORK [--machine Falcon|TT|ST:mono] [--acc] [--econtact v1.43]

EContact is its latest release (the guidelines, 1.2), or the one given:
ECONTACT.ACC from common/tools/getrelease.py, and card-cli and
sample.vcf from its source at the same tag, to write the contacts. The
release is printed, for the report. A new event's Add... opens
EContact's window to choose at once (no question first); "Grace" typed
into its search and Return choose Grace Baker, and the editor comes back
with her, by name.
Saved, the event invites her with her name and address; opened again,
she shows by the name ECal asks EContact for. With --acc ECal is the
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
from rig import Rig, K, SCAN, add_machine  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def fetch_econtact(work, want):
    """EContact's release: its accessory, and its source at that tag with
    card-cli built; the directory, and the release's tag"""
    ec = os.path.join(work, "econtact")
    app = "EContact@" + want if want else "EContact"
    subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"),
                    app, os.path.join(ec, "binary"), "ECONTACT.ACC"], check=True)
    tag = open(os.path.join(ec, "binary", "ECONTACT.VERSION")).read().strip()
    subprocess.run(["git", "clone", "-q", "--depth", "1", "--branch", tag,
                    "https://github.com/whomper/EContact", os.path.join(ec, "src")], check=True)
    subprocess.run(["make", "-s", "-C", os.path.join(ec, "src"), "build/card-cli"], check=True)
    for d in ("build", "tests"):
        os.symlink(os.path.join(ec, "src", d), os.path.join(ec, d))
    return ec, tag


def econtact(hd, ec):
    """EContact's accessory and its folder, as installed: ECONTACT.INF in
    the root, FOLDER= naming C:\\ECONTACT"""
    folder = os.path.join(hd, "ECONTACT")
    os.makedirs(folder)
    cli = os.path.join(ec, "build", "card-cli")
    subprocess.run([cli, folder, "local", "Contacts"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([cli, folder, "import", "Contacts", os.path.join(ec, "tests", "hatari", "sample.vcf")],
                   check=True, stdout=subprocess.DEVNULL)
    inf = open(os.path.join(folder, "ECONTACT.INF"), "rb").read()
    inf = inf.replace(b"\nSYNC_OPEN=1", b"\nSYNC_OPEN=0")
    inf = re.sub(rb"\nSTART=[^\r\n]*", rb"\nSTART=now", inf)
    inf = re.sub(rb"\nFOLDER=[^\r\n]*", rb"\nFOLDER=C:\\ECONTACT", inf)
    os.remove(os.path.join(folder, "ECONTACT.INF"))
    open(os.path.join(hd, "ECONTACT.INF"), "wb").write(inf)
    shutil.copy(os.path.join(ec, "binary", "ECONTACT.ACC"), os.path.join(hd, "ECONTACT.ACC"))


def cache_text(hd):
    t = ""
    for root, _, files in os.walk(hd):
        for f in files:
            if f.upper().endswith(".DAT") and "CAL" in f.upper():
                t += open(os.path.join(root, f), "rb").read().decode("utf-8", "replace")
    return re.sub(r"\r?\n[ \t]", "", t)		# lines unfolded


def menu_entry_width(shot, y):
    """how wide the Desk menu's entry at y is written, in pixels"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    px = im.load()
    ink = [x for x in range(24, 200) for dy in range(-5, 6) if px[x, y + dy] < 128]
    return max(ink) - min(ink) + 1 if ink else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--acc", action="store_true", help="ECal as the accessory, opened from the Desk menu")
    ap.add_argument("--econtact", help="EContact's release, vX.Y (default: the latest)")
    a = ap.parse_args()
    work, acc, want = os.path.abspath(a.work), a.acc, a.econtact
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    ec, tag = fetch_econtact(work, want)
    print("EContact", tag)
    if acc:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.ACC"), os.path.join(hd, "ECAL.ACC"))
        os.makedirs(os.path.join(hd, "ECAL"))
        ui_tour.seed(os.path.join(hd, "ECAL"), english=True)
    else:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
        ui_tour.seed(hd, english=True)
    econtact(hd, ec)
    r = Rig(work, a.machine, auto=None if acc else "C:\\ECAL.PRG")
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
        r.click(*(MONO_ADD if mono else (ADD_X, ADD_Y)))	# Add...: EContact's window at once
        r.wait(6)
        r.type("Grace")				# into its search field
        r.wait(3)
        r.shot("3-choosing")
        r.key(K["RETURN"])			# Grace chosen
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
        r.key(K["ESC"])
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
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("screenshots in", os.path.join(work, "shots"))
    print("people test, with EContact %s:" % tag, "all passed" if not fails else "%d failed" % len(fails))
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
