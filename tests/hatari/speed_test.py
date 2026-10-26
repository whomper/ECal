#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Speed (the guidelines, "Speed: the stock machines set it"): the
longest stretch between event calls for each of the table's actions,
on the stock machines (ST:mono, ST:med, Falcon-4:16, with and without
NVDI).

ECal built with -DAES_PROFILE=20 (make OBJDIR=build/prof20
BIN=build/prof20/bin AESPROF=20 all) writes every stretch of 20 ms or
more between event calls to the emulator's console, "AESGAP <ms> ms
<what>", what naming the event the stretch answers ("key 11", "message
20", "click", "tick", "work"); and "AESSTART <ms> ms", from main() to
the first event call. Each action here is done alone, the screen let
settle, and the stretches that came meanwhile are its own.

With the calendars of tests/host/bigcal.py (580 events) and a day of
24 events today, ECAL.PRG starts and goes through the views, scrolls,
opens and closes a dialog, an alert and the event editor, types in a
field; with --acc, ECAL.ACC boots and is opened from the Desk menu.

    tests/hatari/speed_test.py WORKDIR [--machine ST:mono] [--acc] [--json OUT]
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from rig import Rig, K, SCAN, add_machine, is_st  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(ROOT, "build", "cal-cli")

# the guidelines' times, ms
LIMIT = dict(key=50, line=50, page=150, dialog=150, window=300, acc=500, start=2000)


def seed(hd, work, acc):
    """bigcal's four calendars and 24 events today, as installed:
    ECAL.INF in the root of C:, FOLDER= naming C:\\ECAL"""
    folder = os.path.join(hd, "ECAL")
    os.makedirs(folder, exist_ok=True)
    d = os.path.join(work, "seed")
    shutil.rmtree(d, ignore_errors=True)

    def cli(*a):
        subprocess.run([CLI, d] + list(a), check=True, stdout=subprocess.DEVNULL)

    ics = os.path.join(work, "ics")
    subprocess.run([sys.executable, os.path.join(ROOT, "tests", "host", "bigcal.py"), ics],
                   check=True, stdout=subprocess.DEVNULL)
    cli("zone", "Asia/Jerusalem")
    for i, name in enumerate(("Work", "Home", "Family", "Subs")):
        cli("local", name)
        cli("import", str(i), os.path.join(ics, name.upper() + ".ICS"))
    today = time.strftime("%Y%m%d")
    for i in range(24):
        h, m = 7 + i * 30 // 60, i * 30 % 60
        cli("add", str(i % 3), "Busy day item %d" % (i + 1), "%sT%02d%02d" % (today, h, m), "45")
    inf = os.path.join(hd, "ECAL.INF")
    shutil.copy(os.path.join(d, "ECAL.INF"), inf)
    shutil.copy(os.path.join(d, "ECAL.KEY"), os.path.join(folder, "ECAL.KEY"))
    os.makedirs(os.path.join(folder, "CACHE"), exist_ok=True)
    for f in os.listdir(os.path.join(d, "CACHE")):
        shutil.copy(os.path.join(d, "CACHE", f), os.path.join(folder, "CACHE", f.upper()))
    with open(inf, newline="") as f:
        lines = [l for l in f.read().split("\r\n") if not l.upper().startswith(("START=", "SYNC_OPEN=", "ALARMS="))]
    i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
    lines[i] = "FOLDER=C:\\ECAL"
    # no reminders: their alerts would take the keys meant for the window
    lines[i + 1:i + 1] = ["START=5" if acc else "START=0", "SYNC_OPEN=0", "ALARMS=0"]
    with open(inf, "w", newline="") as f:
        f.write("\r\n".join(lines))


class Meter:
    def __init__(self, r, log):
        self.r, self.log, self.rows = r, log, []

    def text(self):
        with open(self.log, errors="replace") as f:
            return f.read()

    def quiet(self, settle, limit, until=None):
        """until neither the screen nor the console has changed for two
        looks in a row (an emulated 8 MHz ST runs behind this computer's
        clock: a still screen alone can come before the work does)"""
        time.sleep(settle)
        end, last, calm = time.time() + limit, None, 0
        while time.time() < end and calm < 2:
            time.sleep(0.8)
            path = self.r.shot("_settle", wait=0.1)
            with open(path, "rb") as f:
                now = (f.read(), self.text().count("AES"))   # (not the screenshots' own lines)
            os.remove(path)
            calm = calm + 1 if now == last and (not until or until in self.text()) else 0
            last = now

    def act(self, name, kind, fn, settle=1.0, limit=60, until=None, done=1):
        """do fn, wait for done "AESDONE" lines (ECal back in its main loop
        from a key, click or message: 0 for one that opens a dialog, whose
        loop answers it) and then for the screen and the console to
        settle, and keep the stretches that came meanwhile"""
        pos = len(self.text())
        fn()
        end = time.time() + 150
        while done and self.text()[pos:].count("AESDONE") < done and time.time() < end:
            time.sleep(0.5)
        self.quiet(settle, limit, until)
        new = self.text()[pos:]
        gaps = [(int(m.group(1)), m.group(2).strip()) for m in re.finditer(r"AESGAP (\d+) ms ([^\n]*)", new)]
        start = [int(m.group(1)) for m in re.finditer(r"AESSTART (\d+) ms", new)]
        # background work (reading the calendars, the reminder check) isn't the action's
        own = [g for g in gaps if not g[1].startswith(("work", "reminder", "settings"))]
        worst = max(own)[0] if own else 0
        if start:
            worst = max(worst, start[0])
        views = re.findall(r"(?:VIEWMS|REDRAWMS) ([^\n]*)", new)
        row = dict(action=name, kind=kind, limit=LIMIT[kind], ms=worst, start=start[0] if start else None,
                   gaps=own, background=[g for g in gaps if g not in own], where=views)
        self.rows.append(row)
        print("  %5d ms %s %-34s %s%s" % (worst, "OVER" if worst > LIMIT[kind] else "    ", name,
              ", ".join("%d %s" % g for g in sorted(own, reverse=True)[:4]),
              ("  (AESSTART %d)" % start[0]) if start else ""), flush=True)
        for v in views:
            print("             " + v, flush=True)
        return row


def run_prg(r, m, st):
    k = r.key
    m.act("the program started (window drawn)", "start", lambda: None, settle=8, limit=300, until="AESSTART")
    # the calendars, read in the background since the window opened: the
    # longest stretch of that work (a key waits at most that long)
    pos = len(m.text())
    time.sleep(70 if st else 25)
    work = [int(x) for x in re.findall(r"AESGAP (\d+) ms work", m.text()[pos:])]
    print("  %5d ms longest background stretch while the calendars were read" % max(work or [0]), flush=True)
    m.act("month view drawn whole", "window", lambda: k(SCAN["m"]))
    m.act("week view drawn whole", "window", lambda: k(SCAN["w"]))
    m.act("next week (Shift+right)", "page", lambda: k(K["RIGHT"], shift=True))
    m.act("today (Clr/Home)", "window", lambda: k(K["HOME"]))
    m.act("day view, 24 events", "window", lambda: k(SCAN["d"]))
    m.act("a line scrolled (down key)", "line", lambda: k(K["DOWN"]))
    m.act("a line scrolled (up key)", "line", lambda: k(K["UP"]))
    m.act("year view drawn whole", "window", lambda: k(SCAN["y"]))
    m.act("month view again", "window", lambda: k(SCAN["m"]))
    m.act("next month (right arrow in year/month: a page)", "page", lambda: k(K["RIGHT"], shift=True))
    m.act("back to today", "window", lambda: k(K["HOME"]))
    m.act("week view again", "window", lambda: k(SCAN["w"]))
    m.act("Tab: the next event selected", "key", lambda: k(K["TAB"]))
    m.act("Settings opened (^,)", "dialog", lambda: k(SCAN[","], ctrl=True), settle=1.5, done=0)
    m.act("Settings closed with Esc (window uncovered)", "window", lambda: k(K["ESC"]), settle=1.5, done=2)
    m.act("an alert opened (Delete)", "dialog", lambda: k(K["DELETE"]), settle=1.5, done=0)
    m.act("the alert closed with Esc (window uncovered)", "window", lambda: k(K["ESC"]), settle=1.5, done=2)
    m.act("the event editor opened (n)", "dialog", lambda: k(SCAN["n"]), settle=1.5, done=0)
    for ch in "abc":
        m.act("a letter typed in a field (%s)" % ch, "key", lambda ch=ch: k(SCAN[ch]), settle=0.6, done=0)
    m.act("the editor closed with Esc (window uncovered)", "window", lambda: k(K["ESC"]), settle=1.5, done=2)
    k(K["ESC"])                        # a "discard?" question, if one came
    time.sleep(1)


def run_acc(r, m, st):
    m.act("boot to the desktop", "start", lambda: None, settle=10, limit=300, until="AESPROF", done=0)
    time.sleep(40 if st else 20)       # START=5, then the calendars read in the background

    def open_acc():
        r.click(18, 8)
        r.click(60, r.ecal_row())
    m.act("the accessory opened", "acc", open_acc, settle=2, limit=120, done=2)
    m.act("week view drawn whole", "window", lambda: r.key(SCAN["w"]))
    m.act("day view, 24 events", "window", lambda: r.key(SCAN["d"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "ST:mono")
    ap.add_argument("--acc", action="store_true", help="ECAL.ACC from the Desk menu, not ECAL.PRG")
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "prof20", "bin"))
    ap.add_argument("--json", help="the rows, for the table")
    ap.add_argument("--display", default=":99")
    ap.add_argument("--again", action="store_true",
                    help="WORKDIR as an earlier run left it: the files ECal wrote then (its snapshots) kept")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    if a.again:
        for f in ("hatari.log",):
            if os.path.exists(os.path.join(a.work, f)):
                os.rename(os.path.join(a.work, f), os.path.join(a.work, f + ".1"))
    else:
        shutil.rmtree(a.work, ignore_errors=True)
        os.makedirs(hd)
        seed(hd, a.work, a.acc)
    if a.acc:
        shutil.copy(os.path.join(a.bin, "ECAL.ACC"), os.path.join(hd, "ECAL.ACC"))
        auto = None
    else:
        shutil.copy(os.path.join(a.bin, "ECAL.PRG"), os.path.join(hd, "ECAL", "ECAL.PRG"))
        auto = "C:\\ECAL\\ECAL.PRG"
    st = is_st(a.machine)
    r = Rig(a.work, a.machine, display=a.display, auto=auto)
    m = Meter(r, os.path.join(a.work, "hatari.log"))
    nvdi = " with NVDI" if os.environ.get("AERA_WITH_NVDI") and not st else ""
    print("%s%s, %s" % (a.machine, nvdi, "ECAL.ACC" if a.acc else "ECAL.PRG"), flush=True)
    try:
        (run_acc if a.acc else run_prg)(r, m, st)
        r.shot("end")
    finally:
        r.stop()
    text = m.text()
    if "AESPROF ECal" not in text:
        print("no AESPROF line on the console: not a profiling build, or NatFeats is off")
        print("speed test: failed")
        sys.exit(1)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(dict(machine=a.machine + nvdi, acc=a.acc, rows=m.rows), f, indent=1)
    over = [x for x in m.rows if x["ms"] > x["limit"]]
    print("speed test: %d of %d over their time" % (len(over), len(m.rows)))
    sys.exit(1 if over else 0)


if __name__ == "__main__":
    main()
