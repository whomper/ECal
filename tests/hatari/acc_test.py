#!/usr/bin/env python3
"""Calendar as a desk accessory in Hatari: CALENDAR.ACC in the boot
drive's root, its files in C:\\CALENDAR\\. Boots to the desktop, opens the
Desk menu, chooses Calendar, and takes screenshots; then closes the window
and opens it again, as one does with an accessory.

    tests/hatari/acc_test.py WORKDIR etos512us.img [--machine falcon|st] [--acc CALST.ACC]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("tos")
    ap.add_argument("--machine", default="falcon")
    ap.add_argument("--acc", default="CALENDAR.ACC")
    ap.add_argument("--mono", action="store_true")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    os.makedirs(os.path.join(hd, "CALENDAR"))
    shutil.copy(os.path.join(ROOT, a.acc), os.path.join(hd, "CALENDAR.ACC"))
    ui_tour.seed(os.path.join(hd, "CALENDAR"))
    extra = ["--memsize", "14" if a.machine == "falcon" else "4"]
    if a.mono:
        extra += ["--monitor", "mono"]
    r = Rig(a.work, a.tos, machine=a.machine, extra=extra, auto=None)
    try:
        time.sleep(16)
        r.shot("1-desktop")
        r.click(18, 8)                 # the Desk menu
        time.sleep(1)
        r.shot("2-desk-menu")
        r.click(60, 8 + 3 * (16 if not a.mono else 16) + 8)   # its first accessory
        time.sleep(5)
        r.shot("3-calendar")
        r.key(SCAN["w"], ctrl=True)    # close the window (^W)
        time.sleep(2)
        r.shot("4-closed")
        r.click(18, 8)
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(4)
        r.shot("5-again")
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))


if __name__ == "__main__":
    main()
