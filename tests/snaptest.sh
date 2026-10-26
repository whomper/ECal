#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The calendars' snapshots (calstore.c): read back, they give what the
# cache files' text gives, with the window around today as it was when
# they were written and moved on since (the records it now reaches read
# again from their text), with a cache file changed since, and with a
# byte of a snapshot damaged (the text read instead, the same events).
set -euo pipefail
CLI=${CLI:-build/cal-cli}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
for big in "" --big; do
  rm -rf "$W/d" "$W/ics"
  python3 tests/host/bigcal.py "$W/ics" $big >/dev/null
  for c in Work Home Family Subs; do "$CLI" "$W/d" local "$c" >/dev/null; done
  "$CLI" "$W/d" import 0 "$W/ics/WORK.ICS" >/dev/null
  "$CLI" "$W/d" import 1 "$W/ics/HOME.ICS" >/dev/null
  "$CLI" "$W/d" import 2 "$W/ics/FAMILY.ICS" >/dev/null
  "$CLI" "$W/d" import 3 "$W/ics/SUBS.ICS" >/dev/null
  for days in 0 10 100 300; do
    out=$("$CLI" "$W/d" snaptest $days) || { echo "$out"; exit 1; }
  done
  echo "${big:-bigcal}: $out (windows of 0, 10, 100 and 300 days, then 400)"
  "$CLI" "$W/d" list 20260101 20280101 > "$W/ref"
  # a view's collect in slices: the same occurrences in the same order
  "$CLI" "$W/d" sliced 20260101 20280101 2>/dev/null | cmp -s - "$W/ref" ||
    { echo "${big:-bigcal}: the collect in steps differs"; exit 1; }
  for off in 60 3000 7000; do
    f=$(ls "$W/d/CACHE/"*.SNP | head -1)
    printf 'Z' | dd of="$f" bs=1 seek=$off conv=notrunc 2>/dev/null
    "$CLI" "$W/d" list 20260101 20280101 | cmp -s - "$W/ref" ||
      { echo "snapshot damaged at byte $off: not the same events"; exit 1; }
    [ ! -e "$f" ] || { echo "snapshot damaged at byte $off: kept"; exit 1; }
    "$CLI" "$W/d" snaptest >/dev/null
  done
  echo "${big:-bigcal}: a damaged snapshot (three places) left for the text"
done
