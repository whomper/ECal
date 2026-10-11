#!/usr/bin/env python3
"""Advance the build number and write version.c: run by the Makefile
whenever any part of the program was compiled again, so every build of
CALENDAR.ACC / CALST.ACC has its own number (shown in About, sent as the
User-Agent, written into events' PRODID).

    bump.py BUILD-FILE VERSION.H OUT.c
"""
import re
import sys
import time

build_file, header, out = sys.argv[1:4]
try:
    n = int(open(build_file).read().strip() or 0)
except FileNotFoundError:
    n = 0
n += 1
open(build_file, "w").write("%d\n" % n)
major = re.search(r'#define CAL_VERSION "([^"]+)"', open(header).read()).group(1)
version = "%s.%d" % (major, n)
open(out, "w").write(
    "/* written by tools/bump.py: do not edit */\n"
    'const char cal_version[] = "%s";\n'
    "const int cal_build = %d;\n"
    'const char cal_built[] = "%s";\n' % (version, n, time.strftime("%Y-%m-%d")))
print("Calendar", version)
