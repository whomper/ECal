#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The screenshots in docs/ and the README, taken in Hatari with English
events, Hebrew dates off and a New York clock:

    TZ=America/New_York tests/hatari/docs_shots.py WORK etos512us.img [--update]

They land in WORK/*/shots; --update copies them into docs/.
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT
BIN = os.path.join(ROOT, "binary")


def program(work, tos, machine, shots):
    """ECAL.PRG on its own: the views, a new event and the settings"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(BIN, "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    ui_tour.seed(hd, english=True)
    extra = ["--memsize", "14"] if machine == "falcon" else ["--memsize", "4", "--monitor", "mono"]
    # a Mega ST: a plain ST has no clock and would start in 2024
    r = Rig(work, tos, machine="megast" if machine == "st" else machine, extra=extra)
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
    r.shot("new-event")
    r.key(K["ESC"]); time.sleep(1.5)
    r.click(600, 73); time.sleep(1.5)       # the menu
    r.click(460, 363); time.sleep(2)        # Settings...
    r.shot("settings")
    r.key(K["ESC"]); time.sleep(1)


def st_shots(r):
    time.sleep(4)
    r.shot("st-mono")


def accessory(work, tos):
    """the Desk menu with ECalendar in it"""
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(hd, "ECAL"))
    shutil.copy(os.path.join(BIN, "ECAL.ACC"), os.path.join(hd, "ECAL.ACC"))
    ui_tour.seed(os.path.join(hd, "ECAL"), english=True)
    r = Rig(work, tos, machine="falcon", extra=["--memsize", "14"], auto=None)
    try:
        time.sleep(16)
        r.click(18, 8)
        time.sleep(1)
        r.shot("desk-menu")
    finally:
        r.stop()
    return os.path.join(work, "shots")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--update", action="store_true", help="copy them into docs/")
    a = ap.parse_args()
    dirs = [program(os.path.join(a.work, "falcon"), a.tos, "falcon", falcon_shots),
            program(os.path.join(a.work, "st"), a.tos, "st", st_shots),
            accessory(os.path.join(a.work, "acc"), a.tos)]
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if not f.endswith(".png") or f.startswith("grab"):
                continue
            print(os.path.join(d, f))
            if a.update:
                shutil.copy(os.path.join(d, f), os.path.join(ROOT, "docs", f))


if __name__ == "__main__":
    main()
