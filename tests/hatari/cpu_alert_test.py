#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Each edition on the other's machine (the guidelines, 7.4; Erez, 22 Nov
2026): ECAL.PRG and ECAL.ACC on the FireBee stand-in (FireTOS's _CF_
cookie), ECALFB.PRG and ECALFB.ACC on an ST and a Falcon. Each shows its
alert (ui/cpualert.c) at once; OK gives the desktop back, its menu bar
up and the Desk menu opening (an accessory that went quiet, not one
holding the machine); no CRASH.LOG. The screens are kept in WORK/CASE/shots
for the report.

    tests/hatari/cpu_alert_test.py WORK [--only CASE]
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, machines  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
# name, machine, file, as an accessory
CASES = [
    ("prg_on_firebee", "FireBee:16", "ecal.prg", False),
    ("acc_on_firebee", "FireBee:16", "ecal.acc", True),
    ("fbprg_on_st", "ST:mono", "ecalfb.prg", False),
    ("fbacc_on_st1", "ST-1:mono", "ecalfb.acc", True),
    ("fbacc_on_falcon", "Falcon:16", "ecalfb.acc", True),
]

fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def gray(path):
    from PIL import Image
    return Image.open(path).convert("L")


def menu_bar(im):
    """a menu bar: a dark line across nine tenths of the width under the top rows"""
    w, h = im.size
    px = im.load()
    return any(sum(1 for x in range(w) if px[x, y] < 128) > w * 9 // 10 for y in range(8, min(h, 30)))


def alert_box(im):
    """an alert: in the middle third of the screen, a row with a dark run (the box's
    edge) a quarter of the width or longer, and below it light inside"""
    w, h = im.size
    px = im.load()
    for y in range(h // 4, h * 3 // 4):
        run = best = 0
        for x in range(w // 8, w * 7 // 8):
            run = run + 1 if px[x, y] < 128 else 0
            best = max(best, run)
        if best > w // 4:
            return True
    return False


def run(work, name, machine, prog, acc):
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    os.makedirs(hd)
    shutil.copy(machines.ci_path(os.path.join(ROOT, "binary"), prog), os.path.join(hd, prog))
    print("%s: %s on %s" % (name, prog.upper(), machine), flush=True)
    r = Rig(d, machine, auto=None if acc else "C:\\" + prog, tls_ovl=False)
    try:
        print("  " + r.machine.describe(), flush=True)
        time.sleep(30 if acc else 25)
        first = gray(r.shot("1-alert"))
        check(name + ": the alert is up", alert_box(first))
        r.key(K["RETURN"])               # OK
        time.sleep(8)
        after = gray(r.shot("2-after"))
        check(name + ": the desktop's menu bar after OK", menu_bar(after) and not alert_box(after))
        r.click(30, 8)                   # the Desk menu
        time.sleep(2)
        menu = gray(r.shot("3-desk-menu"))
        # (the menu dropped under the bar: the rows below it changed, not only the pointer)
        drop = (40, 20, 200, 120)
        check(name + ": the Desk menu opens (the machine answers)",
              menu_bar(after) and menu.crop(drop).tobytes() != after.crop(drop).tobytes())
        r.click(320, 300)
        time.sleep(1)
    finally:
        r.stop()
    crash = [f for dd, _, fs in os.walk(hd) for f in fs if f.lower() == "crash.log"]
    check(name + ": no CRASH.LOG", not crash)
    shutil.rmtree(hd, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--only")
    a = ap.parse_args()
    for name, machine, prog, acc in CASES:
        if a.only and a.only != name:
            continue
        run(a.work, name, machine, prog, acc)
    print("cpu alerts: %d failed" % len(fails) if fails else "cpu alerts: all passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
