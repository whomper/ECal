#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""EContact's contacts file when EContact isn't loaded (the guidelines,
4.1: Settings > Contacts, PEOPLEFILE=1 and PEOPLEDIR=C:\\ECONTACT), in
Hatari on a Falcon. C:\\ECONTACT\\CONTACTS.TXT is written here as
EContact (1.40's format) writes it: "; " notes, "P<TAB>name<TAB>email<TAB>label"
a line per address and "G<TAB>group<TAB>Name <email>, ..." a line per
group, CRLF.

Without EContact: a new event's Add... opens the people dialog on the
file; "Grace" typed into its Find line and Return take Grace Baker, and
the event is saved inviting her by name; then "Team" taken adds the
group's two. An event that invites Alice by address alone shows her
name from the file.

With --econtact (its latest release loaded, the file's names made
different): the names are EContact's, and the file is never read.
--cards N adds N more people (600: a 65 KB file, as EContact writes for
600 cards); --heapkb K gives the accessory K KB (HEAPKB=), and with
--too-small the list mustn't fit: an alert says so, and the address is
asked for instead.

    tests/hatari/peoplefile_test.py WORK [--machine Falcon|TT|ST:mono] [--acc] [--econtact]
                                    [--cards N] [--heapkb K [--too-small]]
"""
import argparse
import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import ui_tour  # noqa: E402
from people_test import (cache_text, text_width, fetch_econtact, econtact, menu_entry_width,  # noqa: E402
                         ADD_X, ADD_Y, PEOPLE_BOX, MONO_ADD, MONO_PEOPLE_BOX)
from rig import Rig, K, SCAN, add_machine, is_st  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
fails = []


def check(what, ok):
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        fails.append(what)


def contacts_file(hd, suffix="", cards=0, folder="ECONTACT"):
    folder = os.path.join(hd, folder)
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "CONTACTS.TXT"), "w", newline="") as f:
        f.write("; EContact's contacts, for EMail and ECal when EContact isn't loaded\r\n")
        f.write("P\tAlice Archer%s\talice@example.org\thome\r\n" % suffix)
        f.write("P\tAvi Levi%s\tavi@example.org\twork\r\n" % suffix)
        f.write("P\tGrace Baker%s\tgrace.baker@example.net\twork\r\n" % suffix)
        f.write("P\tZed Zulu%s\tzed@example.com\t\r\n" % suffix)
        f.write("G\tTeam\tGrace Baker%s <grace.baker@example.net>, Avi Levi%s <avi@example.org>\r\n"
                % (suffix, suffix))
        for k in range(cards):          # EContact's 600 cards: about 65 KB
            f.write("P\tMember %04d Someone-Longname\tmember%04d.someone@example-company.org\twork\r\n"
                    % (k, k))
            if k % 3 == 0:
                f.write("P\tMember %04d Someone-Longname\tm%04d@home.example.net\thome\r\n" % (k, k))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("work")
    add_machine(ap, "Falcon")
    ap.add_argument("--acc", action="store_true", help="ECal as the accessory, opened from the Desk menu")
    ap.add_argument("--econtact", action="store_true", help="EContact's latest release loaded too")
    ap.add_argument("--cards", type=int, default=0, help="more people in the file")
    ap.add_argument("--heapkb", type=int, default=0, help="HEAPKB= for the accessory")
    ap.add_argument("--too-small", action="store_true", help="the list mustn't fit: the alert expected")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)
    hd = os.path.join(work, "hd")
    os.makedirs(hd)
    top = os.path.join(hd, "ECAL") if a.acc else hd
    if a.acc:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.ACC"),
                    os.path.join(hd, "ECAL.ACC"))
        os.makedirs(top)
    else:
        shutil.copy(os.path.join(ROOT, "binary", "ECAL.PRG"), os.path.join(hd, "ECAL.PRG"))

    def more(cli):
        cli("add", "1", "Budget talk", time.strftime("%Y%m%d") + "T1600", "30")
        cli("invite", "Budget talk", "alice@example.org")
    ui_tour.seed(top, english=True, more=more)
    inf = os.path.join(top, "ECAL.INF")
    t = re.sub(rb"\r\n(PEOPLEFILE|PEOPLEDIR)=[^\r\n]*", b"", open(inf, "rb").read())   # the last one counts
    if a.heapkb:
        t = re.sub(rb"\r\n(HEAPKB|MEMKB)=[^\r\n]*", b"", t)
        t = re.sub(rb"\r\nLOG=[^\r\n]*", b"", t)
        t = t.replace(b"[OPTIONS]\r\n", b"[OPTIONS]\r\nHEAPKB=%d\r\nLOG=1\r\n" % a.heapkb, 1)
    # with EContact loaded (1.58 writes its own CONTACTS.TXT in C:\ECONTACT), a
    # folder of the rig's: its access time tells whether ECal read the file
    dirname = "ECFILE" if a.econtact else "ECONTACT"
    t = t.replace(b"[OPTIONS]\r\n", b"[OPTIONS]\r\nPEOPLEFILE=1\r\nPEOPLEDIR=C:\\" + dirname.encode() + b"\r\n", 1)
    if a.acc:       # in the root, where the accessory reads HEAPKB= at boot, naming its folder
        os.remove(inf)
        inf = os.path.join(hd, "ECAL.INF")
        t = re.sub(rb"\r\nFOLDER=[^\r\n]*", lambda m: b"\r\nFOLDER=C:\\ECAL", t)
    open(inf, "wb").write(t)
    tag = None
    if a.econtact:
        ec, tag = fetch_econtact(work, None)
        print("EContact", tag)
        econtact(hd, ec)
        contacts_file(hd, " FILE", folder=dirname)  # names EContact doesn't have: never to be seen
    else:
        contacts_file(hd, cards=a.cards)
    cfile = os.path.join(hd, dirname, "CONTACTS.TXT")
    print("CONTACTS.TXT: %d KB" % ((os.path.getsize(cfile) + 1023) // 1024))
    os.utime(cfile, (1, os.stat(cfile).st_mtime))   # its access time: read or not
    r = Rig(work, a.machine, auto=None if a.acc else "C:\\ECAL.PRG")
    try:
        r.wait(20)
        if a.acc:
            r.click(18, 8)
            r.wait(1)
            r.shot("0-desk-menu")
            y = r.ecal_row()
            if a.econtact:              # two accessories: ECal's entry is the shorter
                menu = os.path.join(work, "shots", "0-desk-menu.png")
                y = 59 if menu_entry_width(menu, 59) < menu_entry_width(menu, 75) else 75
            r.click(60, y)
            r.wait(8)
        r.shot("1-month")
        r.key(SCAN["n"])                # a new event
        r.wait(2)
        r.type("Planning")
        r.shot("1b-new")
        add = (ADD_X, ADD_Y) if r.machine.screen != "mono" else MONO_ADD
        r.click(*add)                   # Add...: the people dialog on the file, or EContact's window
        r.wait(8 if is_st(a.machine) else 6)
        r.shot("2-dialog")
        if a.too_small:
            r.key(K["RETURN"])          # the alert's OK
            r.wait(3)
            r.shot("2b-invite")         # the address asked for instead
            r.type("grace.baker@example.net")
            r.key(K["RETURN"])
            r.wait(3)
            r.shot("4-back")
            r.key(K["RETURN"])          # OK: saved
            r.wait(5)
            r.stop()
            text = cache_text(hd)
            check("the list didn't fit: Grace added by address instead", "mailto:grace.baker@example.net" in text)
            print("screenshots in", os.path.join(work, "shots"), "(2-dialog: the alert)")
            print("people file test (too small):", "all passed" if not fails else "%d failed" % len(fails))
            return 1 if fails else 0
        r.type("Grace")                 # its Find line
        r.wait(3)
        r.shot("3-found")
        r.key(K["RETURN"])              # Grace taken
        r.wait(6)
        r.shot("4-back")
        if not a.econtact:
            r.click(*add)               # Add... again: the group
            r.wait(6)
            r.type("Team")
            r.wait(3)
            r.shot("4b-group")
            r.key(K["RETURN"])
            r.wait(6)
            r.shot("4c-back")
        r.key(K["RETURN"])              # OK: saved
        r.wait(5)
        # the event inviting Alice by address: her name
        r.ctrl("f")
        r.wait(1.5)
        for _ in range(20):
            r.key(K["BACKSPACE"])
        r.type("Budget talk")
        r.key(K["RETURN"])
        r.wait(3)
        r.key(K["RETURN"])
        r.wait(2)
        r.key(K["RETURN"])              # open it
        r.wait(8)
        r.shot("5-budget")
        r.key(K["ESC"])
        r.wait(2)
        r.ctrl(",")                     # Settings
        r.wait(2)
        r.click(100, 367)               # its Contacts section (Falcon 640x480)
        r.wait(2)
        r.shot("6-settings")
        r.click(497, 257)               # Choose...: the file selector, in EContact's folder
        r.wait(3)
        r.shot("7-choose")
        r.key(K["RETURN"])              # its OK: the folder stays
        r.wait(3)
        r.shot("8-chosen")
        r.key(K["ESC"])
        r.wait(2)
    finally:
        r.stop()
    text = cache_text(hd)
    ev = text[text.find("SUMMARY:Planning") - 2000:] if "SUMMARY:Planning" in text else ""
    att = [ln for ln in ev.splitlines() if ln.startswith("ATTENDEE")]
    shots = os.path.join(work, "shots")
    check("the event is saved", "SUMMARY:Planning" in text)
    check("Grace invited, by address", any("mailto:grace.baker@example.net" in ln for ln in att))
    check("with her name from %s" % ("EContact" if a.econtact else "the file"),
          any("CN=Grace Baker" in ln or 'CN="Grace Baker"' in ln for ln in att))
    check("no name from the file", not any("FILE" in ln for ln in att))
    if not a.econtact:
        check("the group Team added its two: Avi too, once Grace",
              any("mailto:avi@example.org" in ln for ln in att)
              and sum("mailto:grace.baker@example.net" in ln for ln in att) == 1)
    w = text_width(os.path.join(shots, "5-budget.png"), MONO_PEOPLE_BOX if is_st(a.machine) else PEOPLE_BOX)
    if a.econtact:
        # EContact's sample hasn't Alice: her address (136 px), not the file's name
        check("Alice by her address, not the file's name (%d px)" % w, w in range(130, 145))
        check("the file never read", os.stat(cfile).st_atime < 10)
    else:
        check("Alice by her name from the file (%d px)" % w, w in range(90, 100))
    crash = [os.path.join(d, f) for d, _, fs in os.walk(hd) for f in fs if f.upper() == "CRASH.LOG"]
    check("no crash (CRASH.LOG)", not crash)
    print("screenshots in", shots)
    print("people file test%s:" % (", with EContact %s" % tag if tag else ""),
          "all passed" if not fails else "%d failed" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
