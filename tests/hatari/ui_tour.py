#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""ECal in Hatari: seed a calendar with cal-cli (the same core as the
Atari program), boot an emulated Atari with ECAL.PRG and take
screenshots of the month, week, day and year views, the event dialog
with Hebrew typed on the Israeli keyboard, and the menu.

    tests/hatari/ui_tour.py WORKDIR etos512us.img [--machine falcon|st]

Screenshots land in WORKDIR/shots. The emulated clock follows TZ, so run
it with TZ=Asia/Jerusalem for times as in Israel.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "cal-cli")


def seed(hd, english=False):
    """An On My Atari calendar with this week's events, written by cal-cli.
    english: no Hebrew titles or dates and a New York clock, for the
    screenshots in docs/"""
    d = os.path.join(hd, "seed")
    shutil.rmtree(d, ignore_errors=True)

    def cli(*a):
        subprocess.run([CLI, d] + list(a), check=True, stdout=subprocess.DEVNULL)

    today = time.strftime("%Y%m%d")
    tomorrow = time.strftime("%Y%m%d", time.localtime(time.time() + 86400))
    in3 = time.strftime("%Y%m%d", time.localtime(time.time() + 3 * 86400))
    cli("zone", "America/New_York" if english else "Asia/Jerusalem")
    cli("local", "Home")
    cli("local", "Work")
    cli("local", "Family")
    cli("add", "0", "Team meeting" if english else "ישיבת צוות", today + "T1000", "60", "FREQ=WEEKLY")
    cli("add", "1", "Design review", today + "T1400", "90")
    cli("add", "1", "Lunch with Sam" if english else "Lunch with Avi", today + "T1230", "45")
    cli("add", "2", "Dana's birthday" if english else "יום הולדת לדנה", tomorrow + "T0000", "0", "FREQ=YEARLY")
    cli("add", "0", "Atari club", in3 + "T1900", "120", "FREQ=MONTHLY")
    cli("add", "1", "Standup", today + "T0915", "15", "FREQ=DAILY;COUNT=20")
    # the files as the Atari keeps them: ECAL.INF and CACHE\ next to the program
    shutil.copy(os.path.join(d, "ECAL.INF"), os.path.join(hd, "ECAL.INF"))
    shutil.copy(os.path.join(d, "ECAL.KEY"), os.path.join(hd, "ECAL.KEY"))
    os.makedirs(os.path.join(hd, "CACHE"), exist_ok=True)
    for f in os.listdir(os.path.join(d, "CACHE")):
        shutil.copy(os.path.join(d, "CACHE", f), os.path.join(hd, "CACHE", f.upper()))
    shutil.rmtree(d)
    # no refreshing at start: there is no network here
    with open(os.path.join(hd, "ECAL.INF"), "a") as f:
        f.write("sync_open=0\r\n")
    if english:
        inf = os.path.join(hd, "ECAL.INF")
        with open(inf, newline="") as f:
            t = f.read()
        with open(inf, "w", newline="") as f:
            f.write(t.replace("HEBDATES=1", "HEBDATES=0"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--machine", default="falcon")
    ap.add_argument("--prg", default="ECAL.PRG")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", a.prg), os.path.join(hd, "ECAL.PRG"))
    seed(hd)
    extra = ["--memsize", "4"] if a.machine != "falcon" else ["--memsize", "14"]
    if a.machine == "st":
        extra += ["--monitor", "mono"]
    r = Rig(a.work, a.tos, machine=a.machine, extra=extra)
    try:
        time.sleep(14)
        r.shot("1-month")
        r.key(SCAN["w"])
        time.sleep(1.5)
        r.shot("2-week")
        r.key(SCAN["d"])
        time.sleep(1.5)
        r.key(K["TAB"])
        time.sleep(1)
        r.shot("3-day")
        r.key(SCAN["y"])
        time.sleep(2)
        r.shot("4-year")
        r.key(SCAN["m"])
        time.sleep(1.5)
        # a new event, with a Hebrew title typed on the Israeli keyboard (F10)
        r.key(SCAN["n"])
        time.sleep(1.5)
        r.key(K["F10"])
        r.type("ehb,", pace=0.1)      # "קפה" on the Israeli layout... and a few letters
        r.key(K["F10"])
        r.type(" 4pm")
        time.sleep(0.5)
        r.shot("5-new-event")
        r.key(K["ESC"])
        time.sleep(1)
        r.shot("6-after")
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))


if __name__ == "__main__":
    main()
