#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""ECAL.PRG with ECAL.ACC installed, in Hatari under EmuTOS (plain TOS;
the AERA guidelines, 2.3 and 3.1):
  - the accessory opened and closed, then ECAL.PRG started from the
    desktop: it runs, with its menu bar;
  - ECal chosen in the Desk menu meanwhile: the accessory stands aside and
    the program's window stays the only one;
  - Hebrew dates turned off in the program's View menu, then Quit;
  - the accessory opened from the desktop: it reads ECAL.INF again (the
    program changed it), and shows no Hebrew dates either.
Screenshots in WORK/shots; the checks read C:\\ECAL\\ECAL.LOG.

    tests/hatari/twin_test.py WORK [--machine Falcon|TT]
"""
import argparse
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, SCAN, add_machine  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT
failed = 0


def check(ok, what):
    global failed
    print("%s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


def icon(folder, name):
    """where the desktop's window on folder shows name: folders first,
    then files, each by name, three to a row (what the folder holds
    depends on what ECal has written there: CACHE, ALARMS.IDX...)"""
    names = os.listdir(folder)
    order = sorted(n.upper() for n in names if os.path.isdir(os.path.join(folder, n))) + \
        sorted(n.upper() for n in names if not os.path.isdir(os.path.join(folder, n)))
    i = order.index(name)
    return 60 + 80 * (i % 3), 158 + 42 * (i // 3)


def same(a, b):
    """the share of pixels two screenshots have alike"""
    from PIL import Image
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    pa, pb = ia.load(), ib.load()
    w, h = ia.size
    n = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if pa[x, y] == pb[x, y])
    return n / ((w // 2) * (h // 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    folder = os.path.join(hd, "ECAL")
    os.makedirs(folder)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.ACC"), os.path.join(hd, "ECAL.ACC"))
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(folder, "ECAL.PRG"))
    ui_tour.seed(folder)
    # ECAL.INF in the root, FOLDER= naming the program's folder; a log
    with open(os.path.join(folder, "ECAL.INF"), newline="") as f:
        inf = f.read()
    os.remove(os.path.join(folder, "ECAL.INF"))
    with open(os.path.join(hd, "ECAL.INF"), "w", newline="") as f:
        inf = re.sub(r"(?im)^HEBDATES=0", "HEBDATES=1", inf)
        f.write("FOLDER=C:\\ECAL\r\n" + re.sub(r"(?im)^LOG=0", "LOG=1", inf))
    shots = os.path.join(a.work, "shots")
    r = Rig(a.work, a.machine, auto=None)
    try:
        time.sleep(16)
        r.click(18, 8)                  # the desktop's Desk menu: ECal
        time.sleep(1)
        r.click(60, r.ecal_row())
        time.sleep(8)
        r.shot("1-accessory")
        r.key(SCAN["w"], ctrl=True)     # closed, as one does
        time.sleep(2)
        r.click(195, 40, double=True)   # DISK C, the ECAL folder, ECAL.PRG
        time.sleep(3)
        r.click(60, 158, double=True)
        time.sleep(3)
        r.click(*icon(folder, "ECAL.PRG"), double=True)
        time.sleep(15)
        r.shot("2-program")
        r.click(184, 8)                 # View
        time.sleep(1)
        r.shot("3-view-menu")
        r.key(0x01)                     # Esc: the menu closes
        time.sleep(1)
        r.click(40, 8)                  # the Desk menu: ECal's own
        time.sleep(1)
        r.shot("4-desk-menu")
        r.click(60, r.ecal_row())     # its first accessory: ECAL.ACC
        time.sleep(5)
        r.shot("5-acc-chosen")
        r.click(184, 8)                 # View > Hebrew dates (off)
        time.sleep(1)
        r.click(200, 19 + 10 * 16 + 8)
        time.sleep(2)
        r.shot("6-no-hebrew")
        r.key(SCAN["q"], ctrl=True)     # Quit
        time.sleep(8)
        r.shot("7-desktop")
        r.click(18, 8)                  # the desktop's Desk menu: ECal
        time.sleep(1)
        r.click(60, r.ecal_row())
        time.sleep(8)
        r.shot("8-accessory-again")
    finally:
        r.stop()
    print("screenshots in", shots)
    s = lambda n: os.path.join(shots, n + ".png")
    if all(os.path.exists(s(n)) for n in ("2-program", "5-acc-chosen")):
        check(same(s("2-program"), s("5-acc-chosen")) > 0.97,
              "ECal chosen in the Desk menu while ECAL.PRG runs: no second window")
    log = os.path.join(folder, "ECAL.LOG")
    text = open(log, errors="replace").read() if os.path.exists(log) else ""
    check("the files changed since ECal last wrote them" in text,
          "the accessory read the files again after the program changed ECAL.INF")
    with open(os.path.join(hd, "ECAL.INF"), newline="") as f:
        got = f.read()
    check("HEBDATES=0" in got, "the program wrote HEBDATES=0")
    check(not os.path.exists(os.path.join(hd, "ECAL.IN$")), "no ECAL.IN$ left behind")
    print("%d failed" % failed)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
