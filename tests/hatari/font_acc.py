#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A GDOS font chosen in ECal's accessory (or, with --prg, the program),
as Erez did under NVDI on 19 Oct 2026 with 2.14: the window should redraw
in the new font, and the accessory, closed and opened again, should draw.

    tests/hatari/font_acc.py WORK [--machine Falcon] [--prg]
                             [--release v2.14 | --bin DIR] [--font-id N]

Opens the window, then Font... from its menu (the toolbar's last button),
moves to the first font after the system font (the first GDOS font, if a
GDOS lists any) and presses Return. Screenshots and the share of the
screen each step changed are printed; it fails when the window doesn't
redraw after the choice, or the accessory, opened again, draws nothing.
Without a GDOS the list has only the system font (or the accessory says
to choose a GDOS font in ECAL.PRG): "no GDOS fonts, skipped", and it
passes. --release fetches that ECal release (common/tools/getrelease.py)
instead of binary/ (--bin: another folder with ECAL.ACC, ECALST.ACC,
ECAL.PRG); --font-id puts FONT_ID= in ECAL.INF first, as a font chosen
before the machine started. Under NVDI it needs a machine with it
installed (AERA_WITH_NVDI=1 on the hub, common/tools/machines.py).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN, add_machine, is_st  # noqa: E402
import ui_tour  # noqa: E402

ROOT = ui_tour.ROOT


def changed(a, b, box=None):
    """the share of pixels that differ between two screenshots (in box)"""
    from PIL import Image, ImageChops
    x, y = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if box is None:
        w, h = x.size
        box = (0, h // 20, w, h)	# below the menu bar
    d = ImageChops.difference(x.crop(box), y.crop(box)).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float((box[2] - box[0]) * (box[3] - box[1]))


def menu_button(shot):
    """where the toolbar's menu button (three short bars) is: the right
    quarter of the screen, its top third. None if not found"""
    from PIL import Image
    im = Image.open(shot).convert("L")
    w, h = im.size
    px = im.load()

    def dark(x, y):
        return px[x, y] < 128
    runs = {}
    for y in range(h // 25, h // 3):
        x = w * 3 // 4
        while x < w - 1:
            if dark(x, y) and not dark(x - 1, y):
                s = x
                while x < w and dark(x, y):
                    x += 1
                n = x - s
                if 4 <= n <= 12 and x < w and not dark(x, y):
                    runs.setdefault((s, n), []).append(y)
            x += 1
    best = None
    for (s, n), ys in runs.items():
        # three bars or more, each one to three rows, within 14 rows
        groups, start, last = [], None, None
        for y in ys:
            if start is None or y != last + 1:
                if start is not None:
                    groups.append((start, last))
                start = y
            last = y
        groups.append((start, last))
        bars = [g for g in groups if g[1] - g[0] <= 2]
        for i in range(len(bars) - 2):
            if bars[i + 2][1] - bars[i][0] <= 14:
                cand = (s + n // 2, (bars[i][0] + bars[i + 2][1]) // 2)
                if best is None or cand[0] > best[0]:
                    best = cand
                break
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--prg", action="store_true", help="ECAL.PRG instead of the accessory, for comparison")
    ap.add_argument("--release", help="an ECal release to test (e.g. v2.14), fetched from GitHub")
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"), help="a folder with ECal's programs")
    ap.add_argument("--font-id", type=int, help="FONT_ID= in ECAL.INF before the machine starts")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    acc = "ECALST.ACC" if is_st(a.machine) else "ECAL.ACC"
    binary = a.bin
    if a.release:
        binary = os.path.join(work, "release")
        subprocess.run([sys.executable, os.path.join(ROOT, "common", "tools", "getrelease.py"),
                        "ECal@" + a.release, binary, acc, "ECAL.PRG"], check=True)
    print("ECal from", a.release or binary)
    if a.prg:
        shutil.copy(os.path.join(binary, "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
        folder = hd
    else:
        shutil.copy(os.path.join(binary, acc), os.path.join(hd, "ECAL.ACC"))
        folder = os.path.join(hd, "ECAL")
        os.makedirs(folder)
    ui_tour.seed(folder)
    inf = os.path.join(folder, "ECAL.INF")
    if a.font_id:
        with open(inf, newline="") as f:
            t = "".join(l for l in f.read().splitlines(True) if not l.upper().startswith("FONT_ID="))
        with open(inf, "w", newline="") as f:
            f.write("FONT_ID=%d\r\n" % a.font_id + t)

    fails = []

    def check(what, ok, detail=""):
        print("  %s %s%s" % ("ok  " if ok else "FAIL", what, ("  (" + detail + ")") if detail else ""))
        if not ok:
            fails.append(what)

    def open_acc():
        r.click(18, 8)                  # the Desk menu
        time.sleep(1)
        r.click(60, 8 + 3 * 16 + 8)     # its first accessory
        time.sleep(6)

    r = Rig(work, a.machine, auto="C:\\ECAL.PRG" if a.prg else None)
    skipped = False
    try:
        time.sleep(16)
        desk = r.shot("0-desktop")
        if not a.prg:
            open_acc()
        one = r.shot("1-open")
        check("the window drew", changed(desk, one) > 0.2, "%.0f%% of the screen" % (100 * changed(desk, one)))
        at = menu_button(one)
        if not at:
            check("the toolbar's menu button found", False)
            return 1
        r.click(*at)
        time.sleep(1.5)
        r.shot("2-menu")
        # Font... is the third entry from the end (the accessory's menu
        # ends About, Help, Font; the program's has Quit after them)
        for _ in range(4 if a.prg else 3):
            r.key(K["UP"])
            time.sleep(0.3)
        r.key(K["RETURN"])
        time.sleep(4)
        dlg = r.shot("3-font-dialog")
        r.key(K["DOWN"])                # the first font after the system font
        time.sleep(3)
        moved = r.shot("4-first-gdos-font")
        if changed(dlg, moved) < 0.0005:
            print("  no GDOS fonts listed (no GDOS, or the accessory's \"choose it in ECAL.PRG\"), skipped")
            skipped = True
            r.key(K["ESC"])
            time.sleep(2)
        else:
            r.key(K["RETURN"])          # OK
            time.sleep(5)
            after = r.shot("5-chosen")
            check("the window redrew in the new font", changed(one, after) > 0.01,
                  "%.1f%% changed from step 1" % (100 * changed(one, after)))
            time.sleep(4)
            later = r.shot("6-later")
            check("the screen stayed as drawn (no dialog left)", changed(after, later) < 0.02,
                  "%.1f%% changed since step 5" % (100 * changed(after, later)))
            if not a.prg:
                r.key(SCAN["w"], ctrl=True)     # close the window
                time.sleep(3)
                closed = r.shot("7-closed")
                open_acc()
                again = r.shot("8-opened-again")
                check("the accessory, opened again, drew", changed(closed, again) > 0.2,
                      "%.0f%% of the screen" % (100 * changed(closed, again)))
                check("... as it was before closing", changed(later, again) < 0.1,
                      "%.1f%% differs from step 6" % (100 * changed(later, again)))
    finally:
        r.stop()
    if os.path.exists(inf):
        ids = [l.strip() for l in open(inf, errors="replace") if l.upper().startswith("FONT_ID=")]
        print("  ECAL.INF:", ids[0] if ids else "no FONT_ID=")
    print("screenshots in", os.path.join(work, "shots"))
    if skipped and not fails:
        print("font_acc: no GDOS fonts, skipped")
        return 0
    print("font_acc:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
