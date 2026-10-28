#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The screenshots in docs/ and the README, taken in Hatari with English
events, Hebrew dates off and a New York clock:

    TZ=America/New_York tests/hatari/docs_shots.py WORK [--update]

On the Falcon in 256 colours (Erez, 28 Oct 2026: every app picture
the 256-colour one, as it looks best), and st-mono.png on an ST in
mono, under EmuTOS (as anyone building ECal would take them). The
people invited in new-event.png come from a contacts file as EContact
writes it (Settings > Contacts). They land in WORK/*/shots; --update
copies them into docs/. --help-picture makes docs/help/WINDOW.IMG, the
help front page's picture, from a week taken in 16 colours (ST-Guide
shows a picture of more planes in wrong colours on a 16-colour screen).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT
BIN = os.path.join(ROOT, "binary")
os.environ["AERA_TOS"] = "emutos"


def contacts(hd):
    """EContact's CONTACTS.TXT in C:\\ECONTACT, and ECAL.INF reading it"""
    os.makedirs(os.path.join(hd, "ECONTACT"))
    with open(os.path.join(hd, "ECONTACT", "CONTACTS.TXT"), "w", newline="") as f:
        f.write("; EContact's contacts\r\n"
                "P\tAlex Morgan\talex.morgan@example.org\thome\r\n"
                "P\tJo Lee\tjo@example.org\twork\r\n"
                "P\tSam Taylor\tsam.taylor@example.net\twork\r\n")
    inf = os.path.join(hd, "ECAL.INF")
    t = re.sub(rb"\r\n(PEOPLEFILE|PEOPLEDIR)=[^\r\n]*", b"", open(inf, "rb").read())
    t = t.replace(b"[OPTIONS]\r\n", b"[OPTIONS]\r\nPEOPLEFILE=1\r\nPEOPLEDIR=C:\\ECONTACT\r\n", 1)
    open(inf, "wb").write(t)


def program(work, machine, shots):
    """ECAL.PRG on its own: the views, a new event and the settings"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(BIN, "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    ui_tour.seed(hd, english=True)
    contacts(hd)
    r = Rig(work, machine)
    try:
        time.sleep(16)
        shots(r)
    finally:
        r.stop()
    return os.path.join(work, "shots")


def falcon_shots(r):
    r.shot("month")
    r.key(SCAN["w"]); time.sleep(1.5)
    r.shot("week")
    r.key(SCAN["d"]); time.sleep(1.5)
    r.key(K["TAB"]); time.sleep(1)
    r.shot("day")
    r.key(SCAN["y"]); time.sleep(2)
    r.shot("year")
    r.key(SCAN["m"]); time.sleep(1.5)
    r.key(SCAN["n"]); time.sleep(1.5)
    r.type("Coffee with Alex", pace=0.08)
    time.sleep(0.5)
    for who in ("alex", "sam", "jo"):     # People: Add..., the contacts file's
        r.click(515, 125); time.sleep(6)
        r.type(who, pace=0.08)
        time.sleep(2)
        r.key(K["RETURN"]); time.sleep(4)
    r.click(170, 81); time.sleep(0.5)       # back to the title
    r.shot("new-event")
    r.key(0x61); time.sleep(1.5)
    r.key(SCAN[","], ctrl=True); time.sleep(2)     # Settings...
    r.shot("settings")
    r.key(0x61); time.sleep(1)


def st_shots(r):
    time.sleep(4)
    r.shot("st-mono")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--update", action="store_true", help="copy them into docs/")
    ap.add_argument("--help-picture", action="store_true",
                    help="docs/help/WINDOW.IMG from a week taken on Falcon:16")
    a = ap.parse_args()
    dirs = [program(os.path.join(a.work, "falcon"), "Falcon:256", falcon_shots),
            program(os.path.join(a.work, "st"), "ST:mono", st_shots)]
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if not f.endswith(".png") or f.startswith("grab"):
                continue
            print(os.path.join(d, f))
            if a.update:
                shutil.copy(os.path.join(d, f), os.path.join(ROOT, "docs", f))
    if a.help_picture:
        week16 = os.path.join(program(os.path.join(a.work, "falcon16"), "Falcon:16", falcon_shots), "week.png")
        # numbered: 1 the calendar list, 2 the view buttons, 3 today's
        # Team meeting (in today's column of the week), 4 the little month
        wday = (time.localtime().tm_wday + 1) % 7
        out = os.path.join(ROOT, "docs", "help", "WINDOW.IMG")
        subprocess.check_call([sys.executable, os.path.join(ROOT, "common", "tools", "helpfront.py"), "thumb",
                               week16, out, "16,18,600,396",
                               "60,118", "404,73", "%d,215" % (275 + 52 * wday), "104,348"])
        os.remove(os.path.splitext(out)[0] + ".png")
        print(out)


if __name__ == "__main__":
    main()
