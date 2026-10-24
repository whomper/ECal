#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Reminders from ALARMS.IDX: the accessory reminds at boot without
reading a calendar (the guidelines, 2.2: a quiet start).

First boot with START=now: ECAL.ACC reads the calendars in the
background, builds the reminders and writes ALARMS.IDX. Second boot with
START=opened: it reads nothing but ALARMS.IDX, and an event's reminder
two minutes on still comes, over the desktop.

    tests/hatari/alarm_test.py WORKDIR [--machine Falcon|TT|ST:mono|...]

Run with TZ set to the seed's zone (TZ=Asia/Jerusalem). Exit status 0
when every check passes.
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, add_machine, is_st  # noqa: E402

ROOT = ui_tour.ROOT
failed = 0


def check(what, ok):
    global failed
    print("  %s %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failed += 1


def set_start(folder, value):
    inf = os.path.join(folder, "ECAL.INF")
    with open(inf, newline="") as f:
        lines = f.read().split("\r\n")
    lines = [l for l in lines if not l.upper().startswith("START=")]
    i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
    lines.insert(i + 1, "START=" + value)
    with open(inf, "w", newline="") as f:
        f.write("\r\n".join(lines))


def changed(a, b):
    """the share of the middle of the screen that differs: an alert box"""
    from PIL import Image, ImageChops
    x, y = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    w, h = x.size
    box = (w // 4, h // 3, 3 * w // 4, 2 * h // 3)
    d = ImageChops.difference(x.crop(box), y.crop(box)).convert("L").point(lambda v: 255 if v else 0)
    return d.histogram()[255] / float((box[2] - box[0]) * (box[3] - box[1]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    hd = os.path.join(a.work, "hd")
    shutil.rmtree(a.work, ignore_errors=True)
    folder = os.path.join(hd, "ECAL")
    os.makedirs(folder)
    shutil.copy(os.path.join(ROOT, "binary", "ECAL.ACC"),
                os.path.join(hd, "ECAL.ACC"))
    # an event 13 minutes on: its reminder (10 minutes before) comes in 3
    soon = time.localtime(time.time() + 13 * 60)
    ui_tour.seed(folder, more=lambda cli: cli("add", "0", "Tea with Dana", time.strftime("%Y%m%dT%H%M", soon), "30"))
    set_start(folder, "now")
    idx = os.path.join(folder, "ALARMS.IDX")
    r = Rig(a.work, a.machine, auto=None)
    try:
        for _ in range(60):
            time.sleep(2)
            if os.path.exists(idx) and b"Tea with Dana" in open(idx, "rb").read():
                break
        r.shot("1-first-boot")
    finally:
        r.stop()
    check("ALARMS.IDX written in the background", os.path.exists(idx))
    check("with the coming reminder", os.path.exists(idx) and b"Tea with Dana" in open(idx, "rb").read())
    # the second boot: nothing read but ALARMS.IDX
    set_start(folder, "opened")
    before = {f: os.path.getmtime(os.path.join(folder, "CACHE", f)) for f in os.listdir(os.path.join(folder, "CACHE"))}
    r = Rig(a.work, a.machine, auto=None)
    try:
        time.sleep(20)
        r.shot("2-desktop")
        due = time.mktime(soon) - 10 * 60 + 45
        while time.time() < due:
            time.sleep(5)
        r.shot("3-reminder")
        shots = os.path.join(a.work, "shots")
        diff = changed(os.path.join(shots, "2-desktop.png"), os.path.join(shots, "3-reminder.png"))
        check("the reminder shown over the desktop (%.0f%% of the middle changed)" % (diff * 100), diff > 0.1)
    finally:
        r.stop()
    after = {f: os.path.getmtime(os.path.join(folder, "CACHE", f)) for f in before}
    check("no calendar written", before == after)
    print("screenshots in", os.path.join(a.work, "shots"))
    print("alarm test: " + ("all passed" if not failed else "%d failed" % failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
