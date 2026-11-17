#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""An ECAL.INF of an older ECal (D43, D44; Erez, 17 Nov 2026), in Hatari:

    tests/hatari/oldkeys_test.py WORK [--machine ST:mono|Falcon-4:256|...]

The seed's ECAL.INF gets the keys an older ECal wrote: WIN_X, WIN_Y,
WIN_W, WIN_H, FONT_ID, FONT_PT and HEBFONT (the "Hebrew in font" setting,
gone). ECAL.PRG starts with it (its window where WIN_X... put it), quits
(^Q), and the ECAL.INF it writes has WINDOW= and FONT= in their place and
no old key at all. Screenshots in WORK/shots.
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, SCAN, add_machine, machines  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OLD = ("WIN_X=", "WIN_Y=", "WIN_W=", "WIN_H=", "FONT_ID=", "FONT_PT=", "HEBFONT=")
fails = []


def check(what, ok, more=""):
    print(("  ok   " if ok else "  FAIL ") + what + (": " + more if more else ""))
    if not ok:
        fails.append(what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "ST:mono")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(machines.ci_path(os.path.join(ROOT, "binary"), "ecal.prg"), os.path.join(hd, "ecal.prg"))
    ui_tour.seed(hd, english=True)
    inf = os.path.join(hd, "ecal.inf")
    with open(inf, newline="") as f:
        lines = f.read().split("\r\n")
    # the new keys out, the old ones in, in [OPTIONS] (after FOLDER=)
    lines = [l for l in lines if not l.upper().startswith(("WINDOW=", "FONT="))]
    i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
    lines[i + 1:i + 1] = ["WIN_X=8", "WIN_Y=40", "WIN_W=520", "WIN_H=300", "FONT_ID=1", "FONT_PT=0", "HEBFONT=1"]
    with open(inf, "w", newline="") as f:
        f.write("\r\n".join(lines))
    r = Rig(work, a.machine)
    try:
        r.wait(20)
        r.shot("1-started")
        r.key(SCAN["q"], ctrl=True)     # Quit: ECAL.INF written
        r.wait(8)
        r.shot("2-quit")
    finally:
        r.stop()
    text = open(machines.ci_path(hd, "ecal.inf"), errors="replace").read()
    keys = [l.split("=")[0].upper() + "=" for l in text.splitlines() if "=" in l and not l.startswith(";")]
    check("ECAL.PRG started with the old keys and quit", os.path.exists(os.path.join(work, "shots", "2-quit.png")))
    check("WINDOW= written", "WINDOW=" in keys)
    check("FONT= written", any(l.upper().startswith("FONT=SYSTEM,") for l in text.splitlines()),
          "; ".join(l for l in text.splitlines() if l.upper().startswith("FONT=")))
    left = [k for k in keys if k in OLD]
    check("no old key written again (WIN_*, FONT_ID, FONT_PT, HEBFONT)", not left, " ".join(left))
    print("oldkeys test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
