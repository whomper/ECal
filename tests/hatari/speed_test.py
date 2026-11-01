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
LIMIT = dict(key=50, line=50, page=150, dialog=150, window=300, acc=500, start=2000, slice=100)   # slice: a step of a longer job (a key answered within 100 ms)


def seed(hd, work, acc, font=None):
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
        lines = [l for l in f.read().split("\r\n")
                 if not l.upper().startswith(("START=", "SYNC_OPEN=", "ALARMS=") + (("FONT_ID=", "FONT_PT=") if font else ()))]
    i = next(k for k, l in enumerate(lines) if l.upper().startswith("FOLDER="))
    lines[i] = "FOLDER=C:\\ECAL"
    # no reminders: their alerts would take the keys meant for the window
    lines[i + 1:i + 1] = ["START=5" if acc else "START=0", "SYNC_OPEN=0", "ALARMS=0"] + \
        (["FONT_ID=%d" % font[0], "FONT_PT=%d" % font[1]] if font else [])
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

    def act(self, name, kind, fn, settle=1.0, limit=60, until=None, done=1, since=None):
        """do fn, wait for done "AESDONE" lines (ECal back in its main loop
        from a key, click or message: 0 for one that opens a dialog, whose
        loop answers it) and then for the screen and the console to
        settle, and keep the stretches that came meanwhile"""
        pos = len(self.text()) if since is None else since
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
        if start and not (kind == "start" and re.search(r"VIEWDONE", new)):
            worst = max(worst, start[0])
        # a start whose window is drawn in slices: AESSTART (loaded to the
        # first event call) against the start's 2 s, each stretch after it
        # against a slice's 100 ms (the guidelines' Speed)
        start_over = bool(start) and kind == "start" and start[0] > LIMIT["start"]
        views = re.findall(r"(?:VIEWMS|REDRAWMS) ([^\n]*)", new)
        # a view drawn in slices: the stretches are its slices; when the
        # whole picture was there
        done = [int(x) for x in re.findall(r"VIEWDONE (\d+) ms", new)]
        if done:
            kind = "slice"        # judged as a longer job: each stretch within 100 ms (the hub, 26 Oct)
        row = dict(action=name, kind=kind, limit=LIMIT[kind], ms=worst, start=start[0] if start else None,
                   gaps=own, background=[g for g in gaps if g not in own], where=views,
                   picture=done[-1] if done else None)
        if start_over:
            row["ms"] = max(worst, LIMIT[kind] + 1)
        self.rows.append(row)
        print("  %5d ms %s %-34s %s%s%s" % (worst, "OVER" if row["ms"] > LIMIT[kind] else "    ", name,
              ", ".join("%d %s" % g for g in sorted(own, reverse=True)[:4]),
              ("  (AESSTART %d)" % start[0]) if start else "",
              ("  (the picture whole after %d)" % done[-1]) if done else ""), flush=True)
        for v in views:
            print("             " + v, flush=True)
        return row


def run_prg(r, m, st):
    k = r.key
    # from the log's start: on a fast machine ECal can be up before we look
    pos = 0
    m.act("the program started (window drawn)", "start", lambda: None, settle=8, limit=300, until="AESSTART",
          since=0)
    # the calendars, read in the background since the window opened: how
    # long that took, and the longest stretch of it (a key waits at most
    # that long)
    end = time.time() + 600
    while "AESREADY" not in m.text()[pos:] and time.time() < end:
        time.sleep(2)
    time.sleep(10)                     # (the reminders and the snapshots after)
    new = m.text()[pos:]
    ready = re.search(r"AESREADY (\d+) ms", new)
    work = sorted(((int(g), t.strip()) for g, t in re.findall(r"AESGAP (\d+) ms (work[^\n]*)", new)), reverse=True)
    m.rows.append(dict(action="the calendars read (background)", kind="slice", limit=LIMIT["slice"],
                       ms=work[0][0] if work else 0, ready=int(ready.group(1)) if ready else None,
                       gaps=work[:12], background=[], where=[]))
    print("  %5s ms     the calendars read in the background" % (ready.group(1) if ready else "?"), flush=True)
    print("  %5d ms %s longest background stretch meanwhile  %s" %
          (work[0][0] if work else 0, "OVER" if work and work[0][0] > LIMIT["slice"] else "    ",
           ", ".join("%d %s" % g for g in work[:4])), flush=True)
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
    if clicks(r, m):
        double_check(r, m, "the program")
        k(K["TAB"])                    # (an event chosen again, for Delete below)
        time.sleep(2)
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


def human_double(r, x, y):
    """a double click as a hand makes it: each press held 80 ms, 120 ms
    between them (xdotool's own lets go at once, shorter than the
    AES's look at the button can catch)"""
    r.move(x, y)
    for i in range(2):
        r.xdo("mousedown", "1")
        time.sleep(0.08)
        r.xdo("mouseup", "1")
        if not i:
            time.sleep(0.12)


def double_check(r, m, where):
    """the second release over a calendar's name opens Calendar info
    (the guidelines 5.8; one click asked for, press_double timing it)"""
    pos = len(m.text())
    human_double(r, 60, 114)
    time.sleep(4)
    ok = "DLGMS" in m.text()[pos:]
    print("  %s a double click on a calendar's name (%s): %s" %
          ("    " if ok else "FAIL", where, "Calendar info opened" if ok else "nothing opened"), flush=True)
    m.rows.append(dict(action="a double click (%s)" % where, kind="key", limit=LIMIT["key"],
                       ms=0 if ok else 9999, gaps=[], background=[], where=[], picture=None))
    r.key(K["ESC"])
    time.sleep(2)


def clicks(r, m):
    """the guidelines 5.8: a press drawn at once (a key's 50 ms), the
    action on the release over the same thing, timed apart. Where ECal's
    window fills a 640x400 screen (ST:mono, the Falcon's 640x400): the
    toolbar's Today, a calendar's name in the list, and a day in the
    small month"""
    from PIL import Image
    if Image.open(r.shot("_size")).size != (640, 400):
        print("  (the clicks: only on a 640x400 screen)", flush=True)
        return False

    def row(name, kind, ms, note):
        m.rows.append(dict(action=name, kind=kind, limit=LIMIT[kind], ms=ms, gaps=[], background=[],
                           where=[], picture=None))
        print("  %5d ms %s %-34s %s" % (ms, "OVER" if ms > LIMIT[kind] else "    ", name, note), flush=True)

    def press(name, x, y):
        # the profiled build's AESPRESS: from the click's coming to what
        # the press shows drawn (the stretch itself goes on while held)
        pos = len(m.text())
        r.move(x, y)
        r.xdo("mousedown", "1")
        time.sleep(1.0)
        got = re.search(r"AESPRESS (\d+) ms", m.text()[pos:])
        row(name, "key", int(got.group(1)) if got else 0, "" if got else "(no AESPRESS line)")

    def release(name):
        # AESRELEASE (the button seen up, from the click) to AESDONE (the
        # loop back): the action's own turn; then its slices, if any
        pos = len(m.text())
        r.xdo("mouseup", "1")
        end = time.time() + 20
        while "AESDONE" not in m.text()[pos:] and time.time() < end:
            time.sleep(0.3)
        m.quiet(1.5, 60, None)
        new = m.text()[pos:]
        rel = re.search(r"AESRELEASE (\d+) ms", new)
        done = re.search(r"AESDONE (\d+) ms", new)
        slices = [int(g) for g in re.findall(r"AESGAP (\d+) ms view", new)]
        turn = int(done.group(1)) - int(rel.group(1)) if rel and done else 0
        row(name, "slice", max([turn] + slices),
            "the action's turn %s, its slices %s" % ("%d ms" % turn if rel else "(no release seen: nothing to do)",
                                                     ", ".join(map(str, sorted(slices, reverse=True)[:4])) or "none"))

    press("Today pressed (mouse down: drawn pressed)", 100, 68)
    release("Today let go (mouse up: today shown)")
    press("a calendar's name pressed (mouse down: chosen)", 60, 114)
    release("the calendar's name let go (mouse up: nothing more)")
    press("a day in the small month pressed (drawn pressed)", 63, 300)
    release("the small month's day let go (mouse up: the day shown)")
    return True


def run_acc(r, m, st):
    m.act("boot to the desktop", "start", lambda: None, settle=10, limit=300, until="AESPROF", done=0)
    time.sleep(40 if st else 20)       # START=5, then the calendars read in the background

    def open_acc():
        r.click(18, 8)
        r.click(60, r.ecal_row())
    m.act("the accessory opened", "acc", open_acc, settle=2, limit=120, done=2)
    m.act("week view drawn whole", "window", lambda: r.key(SCAN["w"]))
    m.act("day view, 24 events", "window", lambda: r.key(SCAN["d"]))
    if clicks(r, m):
        double_check(r, m, "the accessory")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "ST:mono")
    ap.add_argument("--acc", action="store_true", help="ECAL.ACC from the Desk menu, not ECAL.PRG")
    ap.add_argument("--bin", default=os.path.join(ROOT, "build", "prof20", "bin"))
    ap.add_argument("--json", help="the rows, for the table")
    ap.add_argument("--display", default=":99")
    ap.add_argument("--font", help="ID,PT: a GDOS font chosen in ECAL.INF (NVDI's Courier 14 as Erez runs it:"
                    " the guidelines' Speed); with AERA_WITH_NVDI=1")
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
        seed(hd, a.work, a.acc, tuple(int(x) for x in a.font.split(",")) if a.font else None)
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
