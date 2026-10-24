#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Direct connection ticked (the guidelines, 7.4): ECAL.PRG on a Falcon,
Options > Direct connection (TLS), first without TLS.OVL in ECal's
folder, then with it. Without it an alert says so, the tick stays off
and ECAL.INF keeps FALCON=0; with it TLS.OVL is loaded at once (the
log says "TLS.OVL 1.0 loaded"), the tick is on and FALCON=1 is written.
No crash either way.

    tests/hatari/tlsovl_test.py WORK [--machine Falcon|TT|...]
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from rig import Rig, K, add_machine  # noqa: E402

fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def run(work, machine, with_ovl):
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    shutil.copy(os.path.join(ui_tour.ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))
    shutil.copy(os.path.join(ui_tour.ROOT, "binary", "CACERT.PEM"), os.path.join(hd, "CACERT.PEM"))
    ui_tour.seed(hd, english=True)
    inf = os.path.join(hd, "ECAL.INF")
    t = open(inf, "rb").read().replace(b"\r\nLOG=0", b"\r\nLOG=1")
    if b"\r\nLOG=1" not in t:
        t = t.replace(b"[OPTIONS]\r\n", b"[OPTIONS]\r\nLOG=1\r\n", 1)
    open(inf, "wb").write(t)
    r = Rig(work, machine, auto="C:\\ECAL.PRG", tls_ovl=with_ovl)
    try:
        r.wait(16)
        r.click(OPTIONS_X, 8)               # the Options menu
        time.sleep(1.5)
        r.shot("1-menu")
        r.click(OPTIONS_X + 40, DIRECT_Y)   # Direct connection (TLS)
        time.sleep(4)
        r.shot("2-ticked")
        r.key(K["RETURN"])                  # the alert's OK, if there is one
        time.sleep(2)
        r.ctrl("q")
        time.sleep(4)
    finally:
        r.stop()
    t = open(inf, "rb").read().decode("latin-1")
    log = ""
    for d, _, fs in os.walk(hd):
        for f in fs:
            if f.upper() == "ECAL.LOG":
                log += open(os.path.join(d, f), "rb").read().decode("latin-1")
    crash = [f for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    return t, log, crash, os.path.join(work, "shots")


# ECAL.PRG's menu bar on 640x480: Options, and its Direct connection line
OPTIONS_X, DIRECT_Y = 244, 91


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    t, log, crash, shots = run(os.path.join(work, "without"), a.machine, False)
    check("without TLS.OVL: FALCON=0 kept", "\r\nFALCON=0" in t)
    check("without TLS.OVL: nothing loaded", "TLS.OVL 1.0 loaded" not in log)
    check("without TLS.OVL: no crash (CRASH.LOG)", not crash)
    print("  (the alert: %s)" % os.path.join(shots, "2-ticked.png"))
    t, log, crash, shots = run(os.path.join(work, "with"), a.machine, True)
    check("with TLS.OVL: loaded at once (%s)" % next((l.strip() for l in log.splitlines() if "TLS.OVL" in l), "no log line"),
          "TLS.OVL 1.0 loaded" in log)
    check("with TLS.OVL: FALCON=1 written", "\r\nFALCON=1" in t)
    check("with TLS.OVL: no crash (CRASH.LOG)", not crash)
    print("TLS.OVL test:", "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
