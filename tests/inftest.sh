#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ECAL.INF is written only when a setting changed (guidelines 6.1, decided
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
# the old keys read once, the new ones written in their place (D43, D44)
sed -i 's/^WINDOW=.*$/WIN_X=10\r\nWIN_Y=20\r\nWIN_W=300\r\nWIN_H=200\r/; s/^FONT=.*$/FONT_ID=1\r\nFONT_PT=16\r/' "$inf"
grep -q "^WIN_X=10" "$inf" && grep -q "^FONT_ID=1" "$inf" || { echo "inftest: the old keys not put in"; exit 1; }
"$CLI" "$W/d" save >/dev/null
grep -q "^WINDOW=10,20,300,200" "$inf" || { echo "inftest: WIN_X... not read into WINDOW="; exit 1; }
grep -q "^FONT=System,16" "$inf" || { echo "inftest: FONT_ID= and FONT_PT= not read into FONT="; exit 1; }
! grep -q "^WIN_\|^FONT_ID\|^FONT_PT" "$inf" || { echo "inftest: an old key written again"; exit 1; }
# a new key wins over an old one after it
sed -i 's/^FONT=System,16\r$/FONT=Courier,10\r\nFONT_ID=1\r/' "$inf"
"$CLI" "$W/d" save >/dev/null
grep -q "^FONT=Courier,10" "$inf" || { echo "inftest: FONT_ID= after FONT= won"; exit 1; }
# HEBFONT= (the Hebrew in font setting, gone: decided 17 Nov 2026) read once
# and left out of the next ECAL.INF; ECal starts with it as with any key
sed -i 's/^WINDOW=\(.*\)\r$/WINDOW=\1\r\nHEBFONT=1\r/' "$inf"
grep -q "^HEBFONT=1" "$inf" || { echo "inftest: HEBFONT= not put in"; exit 1; }
"$CLI" "$W/d" save >/dev/null
! grep -qi "^HEBFONT" "$inf" || { echo "inftest: HEBFONT= written again"; exit 1; }
echo "inftest: saved unchanged, ECAL.INF untouched; a calendar added, written; WINDOW= and FONT= from the old keys; HEBFONT= dropped"
