#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# The reminder queue (src/alarms.c) against expanding every event, on
# calendars the size of a real one (tests/host/bigcal.py), in two zones.
set -euo pipefail
CLI=${CLI:-build/cal-cli}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
python3 tests/host/bigcal.py "$W/ics" >/dev/null
for c in Work Home Family Subs; do "$CLI" "$W/d" local "$c" >/dev/null; done
"$CLI" "$W/d" import 0 "$W/ics/WORK.ICS" >/dev/null
"$CLI" "$W/d" import 1 "$W/ics/HOME.ICS" >/dev/null
"$CLI" "$W/d" import 2 "$W/ics/FAMILY.ICS" >/dev/null
"$CLI" "$W/d" import 3 "$W/ics/SUBS.ICS" >/dev/null
for z in Asia/Jerusalem America/New_York; do
  "$CLI" "$W/d" zone Asia/Jerusalem "$z" >/dev/null
  out=$("$CLI" "$W/d" alarms)
  grep -q "Grandma's birthday" <<<"$out" || { echo "alarms ($z): no birthday on the day"; exit 1; }
  grep -q "Uncle's birthday" <<<"$out" || { echo "alarms ($z): no birthday the day before"; exit 1; }
  grep -q "Design review (moved)" <<<"$out" || { echo "alarms ($z): no moved occurrence"; exit 1; }
  echo "alarms ($z): $(head -1 <<<"$out"), the same as expanding every event"
done
