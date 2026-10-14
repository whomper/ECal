#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Erez Yaary
"""ECal as a desk accessory in Hatari: ECAL.ACC in the boot
drive's root, its files in C:\\ECAL\\. Boots to the desktop, opens the
Desk menu, chooses ECal, and takes screenshots; then closes the window
and opens it again, as one does with an accessory.

    tests/hatari/acc_test.py WORKDIR etos512us.img [--machine falcon|st] [--acc ECALST.ACC]
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
    ap.add_argument("--acc", default="ECAL.ACC")
    ap.add_argument("--mono", action="store_true")
    ap.add_argument("--folder", help="its files in this folder (e.g. APPS\\ECAL.APP), "
                    "named by FOLDER= in C:\\ECAL.INF")
    ap.add_argument("--missing", action="store_true", help="with --folder: FOLDER= names a folder that isn't there")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    folder = os.path.join(hd, *(a.folder or "ECAL").split("\\"))
    os.makedirs(folder)
    shutil.copy(os.path.join(ROOT, "binary", a.acc), os.path.join(hd, "ECAL.ACC"))
    ui_tour.seed(folder)
    if a.folder:
        # ECAL.INF in the root, FOLDER= naming where the rest is
        with open(os.path.join(folder, "ECAL.INF"), newline="") as f:
            inf = f.read()
        os.remove(os.path.join(folder, "ECAL.INF"))
        where = "C:\\" + ("GONE\\" if a.missing else "") + a.folder
        with open(os.path.join(hd, "ECAL.INF"), "w", newline="") as f:
            f.write("FOLDER=" + where + "\r\n" + inf)
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
