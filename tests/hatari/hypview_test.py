#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The user guide in EHypView, its latest release (the guidelines, 1.2)
or the one given, fetched with common/tools/getrelease.py: ECAL.PRG's
Help key opens binary/ECAL.HYP (make help) on its front page: on the
Falcon with 16 and with 256 colours, and on a TT's mono screen (EHypView
1.8 has no 68000 build, so not on an ST).

    tests/hatari/hypview_test.py WORK [--machine TT:mono] [--ehypview v1.8]

EHypView's log (natfeats) must say it opened the guide's Main page.
Screenshots of the front page and below it in WORK/*/shots, to look at.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, K  # noqa: E402

ROOT = ui_tour.ROOT
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def run(work, machine, viewer):
    hd = os.path.join(work, "hd")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(hd)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), hd)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.HYP"), hd)
    # an accessory named so that ECal's Help finds it as it finds ST-Guide
    shutil.copy(viewer, os.path.join(hd, "ST-GUIDE.ACC"))
    ui_tour.seed(hd, english=True)
    r = Rig(work, machine)
    try:
        time.sleep(20)
        r.key(K["HELP"])
        time.sleep(8)
        r.shot("1-front")
        for _ in range(12):
            r.key(K["DOWN"])
        time.sleep(2)
        r.shot("2-below")
    finally:
        r.stop()
    log = open(os.path.join(work, "hatari.log"), errors="replace").read()
    return log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--machine", action="append",
                    help="a machine of common/tools/machines.py (default: Falcon:16, Falcon:256, TT:mono)")
    ap.add_argument("--ehypview", help="EHypView's release, vX.Y (default: the latest)")
    a = ap.parse_args()
    work, want = os.path.abspath(a.work), a.ehypview
    ehv = os.path.join(work, "ehypview")
    shutil.rmtree(ehv, ignore_errors=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"),
                    "EHypView@" + want if want else "EHypView", ehv, "EHYPVIEW.ACC"], check=True)
    tag = open(os.path.join(ehv, "EHYPVIEW.VERSION")).read().strip()
    for name in a.machine or ("Falcon:16", "Falcon:256", "TT:mono"):
        log = run(os.path.join(work, name.replace(":", "-")), name, os.path.join(ehv, "EHYPVIEW.ACC"))
        check("%s: the guide opens on its front page" % name, "EHV page C:\\ECAL.HYP Main" in log)
        check("%s: no crash" % name, "Panic" not in log)
    print("screenshots in", os.path.join(work, "*", "shots"))
    print("hypview test, with EHypView %s:" % tag, "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
