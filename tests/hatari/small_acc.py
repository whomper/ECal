#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""No accessory on a 1 MB machine (decided 26 Nov 2026, the guidelines 2.1):
on ST-1, TOS 1.04, ECAL.ACC takes nothing beyond the image TOS loaded. The
memory a program started from the desktop gets is the same

  1. before the accessory's first turn (FREE.PRG started by Hatari at boot),
  2. after it started (another boot, FREE.PRG from the desktop 30 s later),
  3. after its Desk entry was chosen: the AES's alert, "ECal needs an Atari
     with 2 MB or more", and OK (FREE.PRG again),

and nothing is written next to it (no ECAL.INF, no ECAL.LOG). FREE.PRG is
written here: it gives back all but 1.75 KB (Mshrink) and writes Malloc(-1)
to C:\\FREE.TXT in hex. The desktop's own memory is the same in each.

--held, on ST-2 (2 MB, where the accessory runs): what ECAL.ACC takes is
its own, whichever program is in front when it takes it (the hub, 26 Nov
2026: on plain TOS, memory taken while another program is in front went
back to TOS when that program ended, and the next program's TPA covered
the accessory's events). Three boots, ECAL.ACC and its calendars in each:

  ref     the desktop in front at boot;
  boot    HOLDFRNT.PRG (tests/hatari/holdfront.c, make holdfront) started
          at boot, in front while the accessory starts, quitting after 30 s;
  front   HOLDFRNT.PRG started from the desktop after boot, in front 30 s
          while the accessory works in the background.

Then in each A.PRG, not fast-load: Malloc(-1) to C:\\FREE.TXT, that block
filled with A5A5A5A5 and given back. Then ECal opened from the Desk menu.
Each must leave a program what ref leaves, the window's calendar the same
as ref's, and Hatari no address or bus error.

    tests/hatari/small_acc.py WORK [--machine ST-1:mono] [--bin binary]
    tests/hatari/small_acc.py WORK --held [--machine ST-2:mono] [--bin binary]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, machines  # noqa: E402
from cpu_alert_test import gray, menu_bar, alert_box  # noqa: E402
import ui_tour  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
# FREE.PRG's code (68000): keeps 0x700 bytes of its TPA (Mshrink), then
# Fcreate("C:\FREE.TXT"), Malloc(-1) as 8 hex digits, Fclose, Pterm0
FREE = bytes.fromhex(
    "2a6f0004203c000007004ff508002f002f0d42673f3c004a4e414fef000c2f3cffffffff3f3c00484e415c8f2e00"
    "4267487a00503f3c003c4e41508f3c0041fa004e7207e99f14070202000f060200300c0200396f025e0210c251c9"
    "ffe8487a002e2f3c000000083f063f3c00404e414fef000c3f063f3c003e4e41588f42674e41433a5c465245452e"
    "545854000000000000000000")
KEPT = 0x700
# A.PRG's code: FREE.PRG's, then the block Malloc(-1) said taken, filled with
# A5A5A5A5 and Mfree'd (not fast-load: TOS clears its TPA as well)
FILL = bytes.fromhex(
    "2a6f0004203c000007004ff508002f002f0d42673f3c004a4e414fef000c2f3cffffffff3f3c00484e415c8f2e002a00"
    "4267487a007a3f3c003c4e41508f3c0041fa00787207e99f14070202000f060200300c0200396f025e0210c251c9ffe8"
    "487a00582f3c000000083f063f3c00404e414fef000c3f063f3c003e4e41588f2f053f3c00484e415c8f4a806f1c2040"
    "2205e489243ca5a5a5a520c2538166fa2f003f3c00494e415c8f42674e41433a5c465245452e545854000000000000000000")
HOLD = os.path.join(ROOT, "build", "HOLDFRNT.PRG")
# TOS 1.04's desktop in ST high: the hard disk's icon, and in its window
# (AUTO, ECAL, ECAL.ACC, FREE.PRG, TLS.OVL) FREE.PRG's
HD_ICON = (110, 38)
FREE_ICON = (285, 170)
# --held: (AUTO, ECAL, A.PRG, ECAL.ACC, HOLDFRNT.PRG, TLS.OVL); after a
# program started at boot the desktop's window is smaller and lower
A_ICON, A_ICON_AUTO, HOLD_ICON = (220, 170), (220, 235), (365, 170)
GRID = (0, 80, 640, 390)    # ECal's window below its status line (the clock there)
ACC_Y = 59              # the Desk menu's first accessory line (cpu_alert_test.py)

fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    if not ok:
        fails.append(what)


def free_kb(hd, secs=60):
    t0 = time.time()
    while time.time() - t0 < secs and not os.path.exists(machines.ci_path(hd, "free.txt")):
        time.sleep(1)       # (its name's case known once it is there: ci_path each time)
    time.sleep(1)
    f = machines.ci_path(hd, "free.txt")
    if not os.path.exists(f):
        return None
    v = int(open(f).read().strip() or "0", 16) + KEPT
    os.remove(f)
    return v


def held_boot(work, machine, acc, how):
    """one --held boot: (free for A.PRG, ECal's window, Hatari's errors)"""
    d = os.path.join(work, how)
    shutil.rmtree(d, ignore_errors=True)
    hd = os.path.join(d, "hd")
    os.makedirs(os.path.join(hd, "ecal"))
    machines.write_prg(os.path.join(hd, "A.PRG"), FILL, 0x500)
    shutil.copy(HOLD, os.path.join(hd, "HOLDFRNT.PRG"))
    shutil.copy(acc, os.path.join(hd, "ecal.acc"))
    ui_tour.seed(os.path.join(hd, "ecal"))
    r = Rig(d, machine, auto="C:\\HOLDFRNT.PRG" if how == "boot" else None)
    try:
        if how == "ref":
            print("  " + r.machine.describe(), flush=True)
        time.sleep(55 if how == "boot" else 40)
        r.click(*HD_ICON, double=True)
        time.sleep(3)
        if how == "front":
            r.click(*HOLD_ICON, double=True)     # in front 30 s, the accessory working meanwhile
            time.sleep(45)
        r.shot("1-desktop")
        r.click(*(A_ICON_AUTO if how == "boot" else A_ICON), double=True)
        free = free_kb(hd)
        r.click(30, 8)                           # the Desk menu
        time.sleep(2)
        r.click(60, ACC_Y)                       # "  ECal"
        time.sleep(12)
        win = gray(r.shot("2-ecal"))
    except RuntimeError as e:                    # (no screenshot: the machine stopped)
        free = free if "free" in locals() else None
        win = None
        print("  %s: %s" % (how, e), flush=True)
    finally:
        r.stop()
    errs = [l.strip() for l in open(os.path.join(d, "hatari.log"), errors="replace")
            if "Address Error" in l or "Bus Error" in l]
    crash = [f for dd, _, fs in os.walk(hd) for f in fs if f.lower() == "crash.log"]
    shutil.rmtree(hd, ignore_errors=True)
    return free, win, errs + crash


def held(a):
    work = os.path.abspath(a.work)
    if not os.path.exists(HOLD):
        subprocess.run(["make", "-C", ROOT, "holdfront"], check=True, stdout=subprocess.DEVNULL)
    acc = machines.ci_path(a.bin, "ecal.acc")
    got = {how: held_boot(work, a.machine, acc, how) for how in ("ref", "boot", "front")}
    rf, rw, re_ = got["ref"]
    check("ref: the desktop in front, no error (%s)" % "; ".join(re_[:2]), not re_ and rw is not None)
    for how in ("boot", "front"):
        f, w, e = got[how]
        print("  %s: %s bytes free for a program, ref %s" % (how, f, rf), flush=True)
        # (not more than ref leaves: a program run and ended leaves TOS a few bytes the less)
        check("%s: nothing of the accessory's given back to TOS (A.PRG gets no more than ref leaves)" % how,
              f is not None and rf is not None and f <= rf)
        check("%s: no address or bus error, no CRASH.LOG%s" % (how, " (%s)" % "; ".join(e[:2]) if e else ""), not e)
        check("%s: ECal's calendar as ref's after the memory was filled" % how,
              w is not None and rw is not None and w.crop(GRID).tobytes() == rw.crop(GRID).tobytes())
    print("small acc (held):", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    ap.add_argument("--held", action="store_true", help="ST-2: the heap kept whoever is in front")
    ap.add_argument("--machine")
    ap.add_argument("--bin", default=os.path.join(ROOT, "binary"), help="the built files (binary/)")
    a = ap.parse_args()
    if not a.machine:
        a.machine = "ST-2:mono" if a.held else "ST-1:mono"
    if a.held:
        return held(a)
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(os.path.join(hd, "ecal"))
    machines.write_prg(os.path.join(hd, "FREE.PRG"), FREE, 0x500)
    shutil.copy(machines.ci_path(a.bin, "ecal.acc"), os.path.join(hd, "ecal.acc"))
    ui_tour.seed(os.path.join(hd, "ecal"))     # its calendars: what a running one would read
    inf = open(machines.ci_path(os.path.join(hd, "ecal"), "ecal.inf"), "rb").read()
    r = Rig(work, a.machine, auto="C:\\FREE.PRG")     # 1. at boot
    try:
        print("  " + r.machine.describe(), flush=True)
        at_boot = free_kb(hd, 180)
    finally:
        r.stop()
    r = Rig(work, a.machine, auto=None)
    try:
        time.sleep(30)
        r.click(*HD_ICON, double=True)
        time.sleep(3)
        r.shot("1-desktop")
        r.click(*FREE_ICON, double=True)   # 2. after the accessory's start
        started = free_kb(hd)
        r.click(30, 8)                     # the Desk menu
        time.sleep(2)
        r.shot("2-desk-menu")
        r.click(60, ACC_Y)                 # "  ECal"
        time.sleep(4)
        check("its Desk entry gives the alert", alert_box(gray(r.shot("3-alert"))))
        alert = gray(r.shot("3-alert"))
        r.key(K["RETURN"])                 # OK
        time.sleep(4)
        # (the alert's place changed back: the window under it again)
        box = (180, 140, 460, 280)
        check("OK takes the alert away", gray(r.shot("4-after")).crop(box).tobytes() != alert.crop(box).tobytes())
        r.click(*FREE_ICON, double=True)   # 3. after its alert
        opened = free_kb(hd)
    finally:
        r.stop()
    print("  free for a program: %s bytes at boot, %s after the accessory's start, %s after its alert" %
          (at_boot, started, opened), flush=True)
    check("FREE.PRG ran each time", None not in (at_boot, started, opened))
    check("nothing taken by its start (Malloc(-1) the same)", at_boot is not None and at_boot == started)
    check("nothing taken by its Desk entry and alert", started is not None and started == opened)
    written = [f for d, _, fs in os.walk(hd) for f in fs
               if f.lower() in ("ecal.log", "crash.log", "alarms.idx") or (d == hd and f.lower() == "ecal.inf")]
    check("nothing written: no ECAL.INF, ECAL.LOG, ALARMS.IDX or CRASH.LOG" + (" (%s)" % written if written else ""),
          not written and open(machines.ci_path(os.path.join(hd, "ecal"), "ecal.inf"), "rb").read() == inf)
    shutil.rmtree(hd, ignore_errors=True)
    print("small acc:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
