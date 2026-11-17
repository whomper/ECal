#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ECAL.INF is written only when a setting changed (guidelines 6.1, Erez
# 12 Nov 2026): saved with nothing different, the file keeps its date
# and its bytes; with a change, it is written once.
set -euo pipefail
CLI=${CLI:-build/cal-cli}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
"$CLI" "$W/d" local Work >/dev/null
inf="$W/d/ecal.inf"
[ -f "$inf" ] || { echo "inftest: no ECAL.INF made"; exit 1; }
touch -d '2001-01-01 00:00' "$inf"
cp "$inf" "$W/before"
"$CLI" "$W/d" save >/dev/null
[ "$(stat -c %Y "$inf")" = "$(date -d '2001-01-01 00:00' +%s)" ] ||
  { echo "inftest: saved with nothing changed, ECAL.INF was written"; exit 1; }
cmp -s "$inf" "$W/before" || { echo "inftest: ECAL.INF changed"; exit 1; }
"$CLI" "$W/d" local Home >/dev/null
[ "$(stat -c %Y "$inf")" != "$(date -d '2001-01-01 00:00' +%s)" ] ||
  { echo "inftest: a calendar added, ECAL.INF not written"; exit 1; }
grep -q "NAME=Home" "$inf" || { echo "inftest: the new calendar is not in ECAL.INF"; exit 1; }
echo "inftest: saved unchanged, ECAL.INF untouched; a calendar added, written"
