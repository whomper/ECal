#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Start a release: count the version in VERSION up and say what to do next.

A normal build never changes VERSION (it shows "x.y-dev"); only this does.
The tag starts the release workflow, which builds the programs and
publishes them as a GitHub Release.

    tools/bump.py          1.97 -> 1.98
    tools/bump.py major    1.97 -> 2.0
"""
import sys

major, minor = open("VERSION").read().strip().split(".")[:2]
if sys.argv[1:] == ["major"]:
    version = "%d.0" % (int(major) + 1)
elif sys.argv[1:] in ([], ["minor"]):
    version = "%s.%d" % (major, int(minor) + 1)
else:
    sys.exit(__doc__)
open("VERSION", "w").write(version + "\n")
print("VERSION is now %s. Then:" % version)
print('  git commit -am "Release %s"' % version)
print('  git tag -a v%s -m "What is new in %s"' % (version, version))
print("  git push && git push origin v%s" % version)
