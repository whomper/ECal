#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""A quiet start (the AERA guidelines, 2.2): how long the accessory holds
the machine between its event calls, at boot, idle, at its reminder
checks and while it reads the calendars in the background.

ECAL.ACC built with make profile (-DAES_PROFILE=50) writes every stretch
of 50 ms or more between event calls to the emulator's console as
"AESGAP <ms> ms <what>". With the calendars of tests/host/bigcal.py (580
events: Work 250, Home 150, Family 60 with 30 yearly birthdays, a 120-
event calendar), it boots with START=20, sits on the desktop through
the background reading and several reminder checks, and fails on any
stretch over 50 ms on a Falcon or TT (100 ms on an ST).

    make profile && tests/hatari/quiet_test.py WORKDIR [--machine Falcon|TT|ST:mono|...] [--big] [--secs 150]

--big seeds about 1050 events (bigcal.py --big), as Erez's calendars hold.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, add_machine, is_st  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "cal-cli")
PROF = os.path.join(ROOT, "build", "prof", "bin")


def seed(folder, work, big=False):
    """the four calendars as ECal keeps them, in folder (C:\\ECAL)"""
    d = os.path.join(work, "seed")
    shutil.rmtree(d, ignore_errors=True)

    def cli(*a):
        subprocess.run([CLI, d] + list(a), check=True, stdout=subprocess.DEVNULL)

    ics = os.path.join(work, "ics")
    subprocess.run([sys.executable, os.path.join(ROOT, "tests", "host", "bigcal.py"), ics] + (["--big"] if big else []),
                   check=True, stdout=subprocess.DEVNULL)
    cli("zone", "Asia/Jerusalem")
    for i, name in enumerate(("Work", "Home", "Family", "Subs")):
        cli("local", name)
        cli("import", str(i), os.path.join(ics, name.upper() + ".ICS"))
    # as installed: ECAL.INF in the root of C:, FOLDER= naming the rest
    inf = os.path.join(os.path.dirname(folder), "ECAL.INF")
    shutil.copy(os.path.join(d, "ECAL.INF"), inf)
    shutil.copy(os.path.join(d, "ECAL.KEY"), os.path.join(folder, "ECAL.KEY"))
    os.makedirs(os.path.join(folder, "CACHE"), exist_ok=True)
    for f in os.listdir(os.path.join(d, "CACHE")):
        shutil.copy(os.path.join(d, "CACHE", f), os.path.join(folder, "CACHE", f.upper()))
    with open(inf, newline="") as f:
        lines = [l for l in f.read().split("\r\n") if not l.upper().startswith(("START=", "SYNC_OPEN="))]
    i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
    lines[i] = "FOLDER=C:\\ECAL"
    lines[i + 1:i + 1] = ["START=20", "SYNC_OPEN=0"]
    with open(inf, "w", newline="") as f:
        f.write("\r\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--secs", type=int, default=150, help="how long to watch after boot")
    ap.add_argument("--big", action="store_true", help="about 1050 events (bigcal.py --big), as Erez's calendars")
    a = ap.parse_args()
    limit = 100 if is_st(a.machine) else 50
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    folder = os.path.join(hd, "ECAL")
    os.makedirs(folder)
    acc = "ECAL.ACC"
    shutil.copy(os.path.join(PROF, acc), os.path.join(hd, "ECAL.ACC"))
    seed(folder, a.work, a.big)
    r = Rig(a.work, a.machine, auto=None)
    try:
        time.sleep(a.secs)
        r.shot("desktop")
    finally:
        r.stop()
    idx = os.path.join(folder, "ALARMS.IDX")
    log = open(os.path.join(a.work, "hatari.log"), errors="replace").read()
    gaps = [(int(m.group(1)), m.group(2).strip()) for m in re.finditer(r"AESGAP (\d+) ms ([^\n]*)", log)]
    worst = {}
    for ms, what in gaps:
        worst[what] = max(ms, worst.get(what, 0))
    print("%s: %d stretches of 50 ms or more between event calls" % (a.machine, len(gaps)))
    for what, ms in sorted(worst.items(), key=lambda x: -x[1]):
        print("  %5d ms  %s%s" % (ms, what, "   OVER %d ms" % limit if ms > limit else ""))
    if "AESPROF ECal" not in log:
        print("  no AESPROF line on the console: not the make profile build, or NatFeats is off")
        print("quiet test: failed")
        sys.exit(1)
    ok = os.path.exists(idx)
    print("  ALARMS.IDX %s" % ("written: the calendars were read" if ok else "MISSING: the calendars weren't read"))
    over = [g for g in gaps if g[0] > limit]
    print("quiet test: " + ("passed" if ok and not over else "failed"))
    sys.exit(0 if ok and not over else 1)


if __name__ == "__main__":
    main()
