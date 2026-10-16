#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Calendars the size of a real one, for timing and for the reminder
queue: Work (250 events), Home (150), Family (60, 30 of them yearly
birthdays from long ago) and a 120-event calendar standing in for a
subscription, around today, with series, exceptions and alerts.

    bigcal.py OUTDIR [YYYYMMDD]   writes WORK.ICS HOME.ICS FAMILY.ICS SUBS.ICS

Import each with cal-cli DIR import CAL FILE. The same seed always gives
the same events.
"""
import datetime as dt
import os
import random
import sys

out = sys.argv[1]
today = dt.date.today() if len(sys.argv) < 3 else dt.datetime.strptime(sys.argv[2], "%Y%m%d").date()
rnd = random.Random(1960)
TZ = "Asia/Jerusalem"
VTZ = """BEGIN:VTIMEZONE\r
TZID:Asia/Jerusalem\r
BEGIN:STANDARD\r
DTSTART:19701025T020000\r
RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r
TZOFFSETFROM:+0300\r
TZOFFSETTO:+0200\r
END:STANDARD\r
BEGIN:DAYLIGHT\r
DTSTART:19700327T020000\r
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=FR;BYMONTHDAY=23,24,25,26,27,28,29\r
TZOFFSETFROM:+0200\r
TZOFFSETTO:+0300\r
END:DAYLIGHT\r
END:VTIMEZONE\r
"""


def stamp(d, h=0, m=0):
    return "%04d%02d%02dT%02d%02d00" % (d.year, d.month, d.day, h, m)


def alarm(mins):
    if mins is None:
        return ""
    sign = "-" if mins >= 0 else ""
    m = abs(mins)
    trig = ("P%dD" % (m // 1440)) if m and m % 1440 == 0 else ("PT%dH" % (m // 60) if m and m % 60 == 0 else "PT%dM" % m)
    if mins < 0:
        trig = "PT%dH" % (m // 60)
    return "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:%s%s\r\nEND:VALARM\r\n" % (sign, trig)


class Cal:
    def __init__(self, name):
        self.name, self.ev, self.n = name, [], 0

    def add(self, summary, start, mins=60, rrule=None, alert=None, allday=False, extra=""):
        self.n += 1
        uid = "%s-%d@bigcal" % (self.name.lower(), self.n)
        if allday:
            when = "DTSTART;VALUE=DATE:%04d%02d%02d\r\nDTEND;VALUE=DATE:%s\r\n" % (
                start.year, start.month, start.day, (start + dt.timedelta(days=1)).strftime("%Y%m%d"))
        else:
            end = start + dt.timedelta(minutes=mins)
            when = "DTSTART;TZID=%s:%s\r\nDTEND;TZID=%s:%s\r\n" % (TZ, stamp(start, start.hour, start.minute),
                                                                   TZ, stamp(end, end.hour, end.minute))
        r = "RRULE:%s\r\n" % rrule if rrule else ""
        self.ev.append("BEGIN:VEVENT\r\nUID:%s\r\nDTSTAMP:20260101T000000Z\r\n%s%sSUMMARY:%s\r\n%s%sEND:VEVENT\r\n"
                       % (uid, when, r, summary, extra, alarm(alert)))
        return uid

    def write(self):
        with open(os.path.join(out, self.name.upper() + ".ICS"), "w", newline="") as f:
            f.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:bigcal\r\n" + VTZ + "".join(self.ev) + "END:VCALENDAR\r\n")


def at(day_off, h, m=0):
    d = today + dt.timedelta(days=day_off)
    return dt.datetime(d.year, d.month, d.day, h, m)


os.makedirs(out, exist_ok=True)
work = Cal("Work")
work.add("Standup", at(-120, 9, 15), 15, "FREQ=DAILY;BYDAY=SU,MO,TU,WE,TH", alert=5)
work.add("Team meeting", at(-300, 10), 60, "FREQ=WEEKLY;BYDAY=SU", alert=15)
work.add("One to one", at(-200, 14), 30, "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU", alert=10)
work.add("Month-end report", at(-400, 17), 60, "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1", alert=60)
uid = work.add("Design review", at(-60, 12), 60, "FREQ=WEEKLY;BYDAY=WE", alert=10)
# one occurrence moved, with its own alert
mv = today + dt.timedelta(days=(2 - today.weekday()) % 7)      # this week's Wednesday
work.ev.append("BEGIN:VEVENT\r\nUID:%s\r\nDTSTAMP:20260101T000000Z\r\nRECURRENCE-ID;TZID=%s:%s\r\n"
               "DTSTART;TZID=%s:%s\r\nDTEND;TZID=%s:%s\r\nSUMMARY:Design review (moved)\r\n%sEND:VEVENT\r\n"
               % (uid, TZ, stamp(mv, 12), TZ, stamp(mv, 15), TZ, stamp(mv, 16), alarm(30)))
while work.n < 250:
    off = rnd.randint(-90, 40)
    work.add("Meeting %d" % work.n, at(off, rnd.randint(8, 17), rnd.choice([0, 30])), rnd.choice([30, 60, 90]),
             alert=rnd.choice([None, None, 10, 15, 30]))
work.write()

home = Cal("Home")
home.add("Gym", at(-500, 7), 45, "FREQ=WEEKLY;BYDAY=MO,TH", alert=30)
home.add("Rubbish out", at(-800, 20), 15, "FREQ=WEEKLY;BYDAY=TU", alert=0)
while home.n < 150:
    off = rnd.randint(-120, 60)
    home.add("Errand %d" % home.n, at(off, rnd.randint(9, 20)), 60, alert=rnd.choice([None, 60, 1440]))
home.write()

fam = Cal("Family")
for i in range(30):
    born = dt.date(rnd.randint(1950, 1995), rnd.randint(1, 12), rnd.randint(1, 28))
    fam.add("Birthday %d" % i, born, allday=True, rrule="FREQ=YEARLY", alert=rnd.choice([-540, 900, None]))
# two that always fall in the next days: on the day (9:00) and the day before
fam.add("Grandma's birthday", dt.date(1941, (today + dt.timedelta(days=3)).month, (today + dt.timedelta(days=3)).day),
        allday=True, rrule="FREQ=YEARLY", alert=-540)
fam.add("Uncle's birthday", dt.date(1958, (today + dt.timedelta(days=5)).month, (today + dt.timedelta(days=5)).day),
        allday=True, rrule="FREQ=YEARLY", alert=900)
while fam.n < 60:
    off = rnd.randint(-60, 60)
    fam.add("Family %d" % fam.n, at(off, rnd.randint(10, 19)), 120, alert=rnd.choice([None, 60]))
fam.write()

subs = Cal("Subs")
for i in range(120):
    off = rnd.randint(-100, 100)
    subs.add("Holiday %d" % i, today + dt.timedelta(days=off), allday=True)
subs.write()
print(out)
