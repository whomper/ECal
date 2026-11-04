#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ECal as a desk accessory in Hatari: ECAL.ACC in the boot
drive's root, its files in C:\\ECAL\\. Boots to the desktop, opens the
Desk menu, chooses ECal, and takes screenshots; then closes the window
and opens it again, as one does with an accessory.

    tests/hatari/acc_test.py WORKDIR [--machine Falcon|TT|ST-1:mono|...]

The accessory is ECAL.ACC on every machine (TLS.OVL beside it on a TT or Falcon).
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN, add_machine, is_st, machines  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--folder", help="its files in this folder (e.g. APPS\\ECAL.APP), "
                    "named by FOLDER= in C:\\ECAL.INF")
    ap.add_argument("--missing", action="store_true", help="with --folder: FOLDER= names a folder that isn't there")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    folder = os.path.join(hd, *(a.folder or "ecal").split("\\"))
    os.makedirs(folder)
    shutil.copy(machines.ci_path(os.path.join(ROOT, "binary"), "ecal.acc"),
                os.path.join(hd, "ecal.acc"))
    ui_tour.seed(folder)
    if a.folder:
        # ECAL.INF in the root, FOLDER= naming where the rest is
        with open(os.path.join(folder, "ecal.inf"), newline="") as f:
            inf = f.read()
        os.remove(os.path.join(folder, "ecal.inf"))
        where = "C:\\" + ("GONE\\" if a.missing else "") + a.folder
        with open(os.path.join(hd, "ecal.inf"), "w", newline="") as f:
            f.write("FOLDER=" + where + "\r\n" + inf)
    r = Rig(a.work, a.machine, auto=None)
    try:
        time.sleep(16)
        r.shot("1-desktop")
        r.click(18, 8)                 # the Desk menu
        time.sleep(1)
        r.shot("2-desk-menu")
        r.click(60, r.ecal_row())     # its first accessory
        time.sleep(5)
        r.shot("3-calendar")
        r.key(SCAN["u"], ctrl=True)    # close the window (^U, the guidelines 3.2)
        time.sleep(2)
        r.shot("4-closed")
        r.click(18, 8)
        time.sleep(1)
        r.click(60, r.ecal_row())
        time.sleep(4)
        r.shot("5-again")
    finally:
        r.stop()
    print("screenshots in", os.path.join(a.work, "shots"))


if __name__ == "__main__":
    main()
