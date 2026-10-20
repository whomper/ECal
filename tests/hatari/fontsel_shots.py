#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""The shared Font dialog (common/ui/fontsel.c) in ECal: opens Font...,
waits for the first look at the fonts, then walks it with the keys and
takes a screenshot at each step (scratch rig for the port).

    tests/hatari/fontsel_shots.py WORK [--machine Falcon] [--prg] [--steps ...]
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN, add_machine, is_st  # noqa: E402
import ui_tour  # noqa: E402
from font_acc import changed, wait_still, menu_button  # noqa: E402

ROOT = ui_tour.ROOT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--prg", action="store_true")
    ap.add_argument("--fnt", help="an ECAL.FNT to start with")
    ap.add_argument("--keep", action="store_true", help="keep WORK/hd (ECAL.FNT from the last run)")
    ap.add_argument("--steps", default="", help="space-separated keys after the dialog settles: "
                    "down up tab stab left right return esc home, a letter, a digit, shot:NAME, wait:S, "
                    "click:X,Y, font (Font... again)")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    hd = os.path.join(work, "hd")
    keep = a.keep and os.path.isdir(hd)
    if not keep:
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(hd)
    else:
        shutil.rmtree(os.path.join(work, "shots"), ignore_errors=True)
    acc = "ECALST.ACC" if is_st(a.machine) else "ECAL.ACC"
    binary = os.path.join(ROOT, "binary")
    if a.prg:
        shutil.copy(os.path.join(binary, "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
        folder = hd
    else:
        shutil.copy(os.path.join(binary, acc), os.path.join(hd, "ECAL.ACC"))
        folder = os.path.join(hd, "ECAL")
        os.makedirs(folder, exist_ok=True)
    if not keep:
        ui_tour.seed(folder)
    if a.fnt:
        shutil.copy(a.fnt, os.path.join(folder, "ECAL.FNT"))

    def open_acc():
        r.click(18, 8)
        time.sleep(1.5)
        r.click(60, 8 + 3 * 16 + 8)
        time.sleep(6)
        wait_still(r, "opening", 60)

    def font_menu():
        one = r.shot("open")
        at = menu_button(one)
        for _ in range(120):
            if at:
                break
            time.sleep(1)
            one = r.shot("open")
            at = menu_button(one)
        r.click(*at)
        time.sleep(1.5)
        for _ in range(4 if a.prg else 3):
            r.key(K["UP"])
            time.sleep(0.3)
        r.key(K["RETURN"])
        time.sleep(3)
        return one

    r = Rig(work, a.machine, auto="C:\\ECAL.PRG" if a.prg else None)
    t0 = time.time()
    try:
        time.sleep(10)
        desk = wait_still(r, "0-desktop", 400, desktop=True)
        if not a.prg:
            open_acc()
            if changed(desk, r.shot("1-open")) < 0.2:
                time.sleep(10)
                r.key(K["ESC"])
                open_acc()
        one = font_menu()
        for k in range(int(os.environ.get("BURST", "0"))):
            r.shot("burst-%02d" % k, wait=0.1)
            time.sleep(0.4)
        # the first look at the fonts: until the screen stays still 6 s
        prev, still, n = r.shot("dlg-000"), 0, 0
        while time.time() - t0 < 1500:
            time.sleep(3)
            n += 1
            cur = r.shot("dlg-%03d" % n)
            if changed(prev, cur) == 0:
                still += 1
                os.remove(cur)
                if still >= 2:
                    break
            else:
                still = 0
                prev = cur
        print("dialog settled after %.0f s" % (time.time() - t0), flush=True)
        r.shot("10-dialog")
        keys = dict(down="DOWN", up="UP", tab="TAB", left="LEFT", right="RIGHT", ret="RETURN",
                    esc="ESC", home="HOME", bs="BACKSPACE")
        for i, st in enumerate(a.steps.split()):
            if st.startswith("shot:"):
                time.sleep(2)
                r.shot("%02d-%s" % (11 + i, st[5:]))
            elif st.startswith("wait:"):
                time.sleep(float(st[5:]))
            elif st.startswith("click:"):
                x, y = st[6:].split(",")
                r.click(int(x), int(y))
            elif st.startswith("dclick:"):
                x, y = st[7:].split(",")
                r.click(int(x), int(y), double=True)
            elif st == "font":
                font_menu()
                time.sleep(4)
            elif st == "stab":
                r.key(K["TAB"], shift=True)
            elif st == "sdown":
                r.key(K["DOWN"], shift=True)
            elif st in keys:
                r.key(K[keys[st]])
            elif len(st) == 1:
                r.type(st)
            else:
                print("unknown step", st)
            time.sleep(0.5)
        time.sleep(3)
        r.shot("99-end")
    finally:
        r.stop()
    fnt = os.path.join(folder, "ECAL.FNT")
    if os.path.exists(fnt):
        shutil.copy(fnt, os.path.join(work, "ECAL.FNT"))
    print("screenshots in", os.path.join(work, "shots"))


if __name__ == "__main__":
    sys.exit(main())
