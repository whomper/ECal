#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Help opens the user guide in ST-Guide. ECAL.PRG runs with a stand-in
ST-Guide accessory (build/ST-GUIDE.ACC, make fakeguide) that writes what
VA_START hands it into C:\\GUIDE.TXT:

  1. ECAL.HYP next to ECal: Help sends its full path;
  2. no ECAL.HYP there: Help sends *:\\ECAL.HYP, for ST-Guide's own paths;
  3. no ST-Guide: Help shows the keys (a screenshot).

    tests/hatari/help_test.py WORKDIR [--machine Falcon|TT]
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, add_machine  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT


def run(work, machine, guide, hyp):
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    ui_tour.seed(hd, english=True)
    if guide:
        shutil.copy(os.path.join(ROOT, "build", "ST-GUIDE.ACC"), hd)
    if hyp:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.HYP"), hd)
    r = Rig(work, machine)
    try:
        time.sleep(14)
        r.key(K["HELP"])
        time.sleep(3)
        r.shot("help")
    finally:
        r.stop()
    out = os.path.join(hd, "GUIDE.TXT")
    return open(out).read() if os.path.exists(out) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = a.work
    ok = True
    for name, guide, hyp, want in (("with-hyp", True, True, "C:\\ECAL.HYP"),
                                   ("no-hyp", True, False, "*:\\ECAL.HYP"),
                                   ("no-guide", False, True, None)):
        got = run(os.path.join(work, name), a.machine, guide, hyp)
        good = got == want
        ok &= good
        print("%-9s %s: ST-Guide got %r" % (name, "ok" if good else "FAILED", got))
    print("screenshots in", os.path.join(work, "*", "shots"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
