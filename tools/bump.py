#!/usr/bin/env python3
"""Count the version up and write version.c, as EMail does: VERSION holds
major.minor (e.g. 1.25), and every build of the program adds one to the
minor number. Run by the Makefile whenever any part of the program was
compiled again; the version shows in About and the User-Agent and goes
into events' PRODID. Commit VERSION with the program.

    bump.py VERSION-FILE OUT.c
"""
import sys

vfile, out = sys.argv[1:3]
try:
    major, minor = open(vfile).read().strip().split(".")[:2]
except (FileNotFoundError, ValueError):
    major, minor = "1", "0"
version = "%s.%d" % (major, int(minor) + 1)
open(vfile, "w").write(version + "\n")
open(out, "w").write("/* written by tools/bump.py: do not edit */\n"
                     'const char cal_version[] = "%s";\n' % version)
print("ECal", version)
