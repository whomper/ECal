#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""corecal.h for tests/corecheck.c: each calendar file given, as a C string
with its name and length (the ColdFire build reads no files).

    mkcorecal.py OUT.h FILE...
"""
import os
import sys

out, files = sys.argv[1], sys.argv[2:]
with open(out + ".new", "w") as f:
    f.write("/* written by tests/coldfire/mkcorecal.py: do not edit */\n")
    f.write("static const struct { const char *name; const char *text; long len; } CORECAL[] = {\n")
    for p in files:
        data = open(p, "rb").read()
        f.write('\t{ "%s", "' % os.path.basename(p))
        for i, b in enumerate(data):
            if i and i % 120 == 0:
                f.write('"\n\t  "')
            f.write("\\%03o" % b)
        f.write('", %d },\n' % len(data))
    f.write("};\n#define CORECAL_N %d\n" % len(files))
os.replace(out + ".new", out)
