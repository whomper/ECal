#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# ECal's core against a real CalDAV server (Radicale) and a published
# feed, with cal-cli (the same calstore/CalDAV/HTTP/TLS code as the Atari):
#
#   tests/integration.sh             plain HTTP, straight to the server
#   GATEWAY=1 tests/integration.sh   through the Pi gateway (ecalgate.py)
#   GZIP=1 tests/integration.sh      through a proxy that answers like iCloud
#                                    (HTTP/1.1 kept open, gzip); with FALCON=1 too
#   FALCON=1 tests/integration.sh    Falcon mode: TLS in ECal itself,
#                                    the server's certificate checked
#                                    against a test CA
#
# Needs: pip install radicale; FALCON=1 also needs openssl.
set -euo pipefail
cd "$(dirname "$0")/.."
# the Atari clock runs in its home zone: so does this one
export TZ=Asia/Jerusalem
CLI=$PWD/build/cal-cli
# no file open where the accessory makes event calls (guidelines 2.2)
export CAL_FILECHECK=1
# The events were written for October to December 2026. ECal copies
# nothing from the past and only FUTURE_DAYS (400) ahead, so they are
# moved by whole weeks (weekdays stay as they were) to start after today:
# d 20261013 gives the moved date, the same weekday. (The time goes
# first: date(1) reads "12:00 + 21 days" as a zone, +21, and no days.)
SHIFT=$(( ( ($(date -d "tomorrow 12:00" +%s) - $(date -d "2026-10-01 12:00" +%s)) / 86400 + 6) / 7 * 7 ))
[ "$SHIFT" -lt 0 ] && SHIFT=0
d() { date -d "12:00 ${1:0:4}-${1:4:2}-${1:6:2} + $SHIFT days" +%Y%m%d; }
# HH:MM in zone $1 of $2 (a date and time with its zone, as date(1) reads it)
hm() { TZ=$1 date -d "$2" +%H:%M; }
# the last weekday (Monday to Friday) of the month that starts on $1 (YYYY-MM-01)
lastwd() {
  local x; x=$(date -d "12:00 $1 + 1 month - 1 day" +%Y-%m-%d)
  while [ "$(date -d "12:00 $x" +%u)" -gt 5 ]; do x=$(date -d "12:00 $x - 1 day" +%Y-%m-%d); done
  echo "${x//-/}"
}
W=$(mktemp -d)
PIDS=()
cleanup() { for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done; rm -rf "$W"; }
trap cleanup EXIT
PORT=$((20000 + RANDOM % 20000))
FEEDPORT=$((PORT + 1))
GWPORT=$((PORT + 2))
fails=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fails=$((fails + 1)); }
expect() { # expect "description" "needle" "haystack"
  if grep -qF -- "$2" <<<"$3"; then ok "$1"; else bad "$1: no '$2' in:"; echo "$3" | sed 's/^/       /'; fi
}
absent() {
  if grep -qF -- "$2" <<<"$3"; then bad "$1: '$2' still there"; else ok "$1"; fi
}

# ---- the server ----
echo "alice:wonderland" > "$W/users"
SCHEME=http
SSL=()
if [ -n "${FALCON:-}" ]; then
  SCHEME=https
  openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj "/CN=ECal Test CA" \
    -keyout "$W/ca.key" -out "$W/ca.pem" -addext "basicConstraints=critical,CA:true" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
  openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$W/srv.key" -out "$W/srv.csr" 2>/dev/null
  printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > "$W/ext"
  openssl x509 -req -in "$W/srv.csr" -CA "$W/ca.pem" -CAkey "$W/ca.key" -CAcreateserial \
    -days 30 -extfile "$W/ext" -out "$W/srv.pem" 2>/dev/null
  SSL=(--server-ssl --server-certificate "$W/srv.pem" --server-key "$W/srv.key")
  export CACERT=$W/ca.pem
fi
if [ -n "${GZIP:-}" ]; then
  # Radicale behind gzproxy.py, which answers like iCloud: HTTP/1.1 kept
  # open, compressed with gzip (and does the TLS in Falcon mode)
  python3 -m radicale --server-hosts "127.0.0.1:$((PORT + 3))" --storage-filesystem-folder "$W/store" \
    --auth-type htpasswd --auth-htpasswd-filename "$W/users" --auth-htpasswd-encryption plain \
    > "$W/radicale.log" 2>&1 &
  PIDS+=($!)
  PX=()
  [ -n "${FALCON:-}" ] && PX=(--tls "$W/srv.pem" "$W/srv.key")
  python3 tests/host/gzproxy.py "$PORT" "$((PORT + 3))" "${PX[@]}" 2> "$W/gzproxy.log" &
  PIDS+=($!)
else
  python3 -m radicale --server-hosts "127.0.0.1:$PORT" --storage-filesystem-folder "$W/store" \
    --auth-type htpasswd --auth-htpasswd-filename "$W/users" --auth-htpasswd-encryption plain \
    "${SSL[@]}" > "$W/radicale.log" 2>&1 &
  PIDS+=($!)
fi
BASE="$SCHEME://localhost:$PORT"
CURL=(curl -s -u alice:wonderland)
[ -n "${FALCON:-}" ] && CURL+=(--cacert "$W/ca.pem")
for i in $(seq 50); do "${CURL[@]}" -o /dev/null "$BASE/" && break; sleep 0.2; done

# two calendars, one with a Hebrew name and a colour; and a task list (must be ignored)
mk() { "${CURL[@]}" -X MKCALENDAR "$BASE/alice/$1/" -H "Content-Type: application/xml" --data-binary @- >/dev/null; }
mk home <<XML
<?xml version="1.0" encoding="utf-8"?>
<C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:A="http://apple.com/ns/ical/">
<D:set><D:prop><D:displayname>בית</D:displayname><A:calendar-color>#FF3B30FF</A:calendar-color>
<C:supported-calendar-component-set><C:comp name="VEVENT"/></C:supported-calendar-component-set></D:prop></D:set></C:mkcalendar>
XML
mk work <<XML
<?xml version="1.0" encoding="utf-8"?>
<C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>Work</D:displayname>
<C:supported-calendar-component-set><C:comp name="VEVENT"/></C:supported-calendar-component-set></D:prop></D:set></C:mkcalendar>
XML
mk tasks <<XML
<?xml version="1.0" encoding="utf-8"?>
<C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>Tasks</D:displayname>
<C:supported-calendar-component-set><C:comp name="VTODO"/></C:supported-calendar-component-set></D:prop></D:set></C:mkcalendar>
XML

put() { "${CURL[@]}" -X PUT "$BASE/alice/$1" -H "Content-Type: text/calendar" --data-binary @- -o /dev/null -w "%{http_code}"; }
# a weekly meeting in Hebrew, made by "an iPhone", with an attendee ECal doesn't edit
put home/weekly.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//Apple Inc.//iOS 18.0//EN
BEGIN:VTIMEZONE
TZID:Asia/Jerusalem
BEGIN:STANDARD
DTSTART:20131027T020000
TZOFFSETFROM:+0300
TZOFFSETTO:+0200
RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU
END:STANDARD
BEGIN:DAYLIGHT
DTSTART:20130329T020000
TZOFFSETFROM:+0200
TZOFFSETTO:+0300
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=FR;BYMONTHDAY=23,24,25,26,27,28,29
END:DAYLIGHT
END:VTIMEZONE
BEGIN:VEVENT
UID:weekly-hebrew@test
DTSTAMP:20261001T000000Z
DTSTART;TZID=Asia/Jerusalem:$(d 20261004)T100000
DTEND;TZID=Asia/Jerusalem:$(d 20261004)T110000
RRULE:FREQ=WEEKLY;BYDAY=SU
SUMMARY:ישיבת צוות
ORGANIZER;CN=Avi:mailto:avi@example.com
ATTENDEE;CN=Dana:mailto:dana@example.com
X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC
END:VEVENT
END:VCALENDAR
ICS
put work/review.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:review@test
DTSTAMP:20261001T000000Z
DTSTART:$(d 20261013)T120000Z
DTEND:$(d 20261013)T130000Z
SUMMARY:Design review
END:VEVENT
END:VCALENDAR
ICS

# ---- a published calendar (subscription) ----
mkdir -p "$W/feed"
cat > "$W/feed/holidays.ics" <<ICS
BEGIN:VCALENDAR
VERSION:2.0
X-WR-CALNAME:Holidays in Israel
BEGIN:VEVENT
UID:sukkot-2026
DTSTART;VALUE=DATE:$(d 20261026)
DTEND;VALUE=DATE:$(d 20261027)
SUMMARY:Sukkot test day
END:VEVENT
BEGIN:VEVENT
UID:long-ago
DTSTART;VALUE=DATE:20100101
SUMMARY:Long ago
END:VEVENT
END:VCALENDAR
ICS
(cd "$W/feed" && exec python3 -m http.server "$FEEDPORT" --bind 127.0.0.1) > /dev/null 2>&1 &
PIDS+=($!)

D=$W/atari
if [ -n "${GATEWAY:-}" ]; then
  python3 gateway/ecalgate.py --port "$GWPORT" --bind 127.0.0.1 --allow-local > "$W/gw.log" 2>&1 &
  PIDS+=($!)
  sleep 0.5
  "$CLI" "$D" gateway 127.0.0.1 "$GWPORT"
  echo "(through the gateway on port $GWPORT)"
fi
if [ -n "${FALCON:-}" ]; then
  "$CLI" "$D" falcon 1
  echo "(Falcon mode: TLS in ECal)"
fi
"$CLI" "$D" zone Asia/Jerusalem
export CAL_LOG=$W/ECAL.LOG

# a one-off event before today: not copied (nothing from the past is)
put home/past.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//test//EN
BEGIN:VEVENT
UID:past-1@test
DTSTAMP:20261001T000000Z
DTSTART:$(date -u -d "-6 days" +%Y%m%d)T100000Z
DTEND:$(date -u -d "-6 days" +%Y%m%d)T110000Z
SUMMARY:Old dinner
END:VEVENT
END:VCALENDAR
ICS

# a weekly series its organizer cancelled: not shown at all
put home/cancelled.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//test//EN
BEGIN:VEVENT
UID:cancelled-1@test
DTSTAMP:20261001T000000Z
DTSTART:$(date -u -d "+1 day" +%Y%m%d)T120000Z
DTEND:$(date -u -d "+1 day" +%Y%m%d)T130000Z
RRULE:FREQ=WEEKLY
STATUS:CANCELLED
SUMMARY:Called-off series
END:VEVENT
END:VCALENDAR
ICS

echo "== account and first sync"
"$CLI" "$D" caldav Radicale "$BASE/" alice wonderland || bad "caldav account"
out=$("$CLI" "$D" calendars)
expect "the Hebrew calendar found" "בית" "$out"
expect "the work calendar found" "Work" "$out"
absent "the task list left out" "Tasks" "$out"
out=$("$CLI" "$D" list $(d 20261001) $(d 20261101))
expect "weekly meeting in Hebrew, 10:00 Jerusalem" "$(d 20261004) 10:00-11:00 [" "$out"
expect "it repeats" "$(d 20261025) 10:00-11:00" "$out"
absent "a cancelled series isn't shown" "Called-off series" "$("$CLI" "$D" list $(date +%Y%m%d) $(date -d "+40 days" +%Y%m%d))"
absent "a one-off event before today isn't copied" "Old dinner" "$("$CLI" "$D" list $(date -d "-30 days" +%Y%m%d) $(date -d "+30 days" +%Y%m%d))"
R=$(d 20261013); RD="${R:0:4}-${R:4:2}-${R:6:2}"
expect "a UTC event shown in Israel time" "$R $(hm Asia/Jerusalem "$RD 12:00 UTC")-$(hm Asia/Jerusalem "$RD 13:00 UTC") [Work] Design review" "$out"

echo "== nothing changed: nothing fetched"
out=$("$CLI" "$D" sync)
expect "second sync is quiet" "0 calendars changed" "$out"

echo "== download everything again"
# the Atari's copy spoilt behind ECal's back, the server's unchanged: only
# "again" fetches it, as the others keep a copy whose etag matches
sed -i 's/Design review/Design reviex/' "$D"/CACHE/*.DAT
expect "the spoilt copy" "Design reviex" "$("$CLI" "$D" list $(d 20261013) $(d 20261014))"
"$CLI" "$D" sync force >/dev/null
expect "kept by Refresh everything" "Design reviex" "$("$CLI" "$D" list $(d 20261013) $(d 20261014))"
out=$("$CLI" "$D" sync again)
expect "fetched again" "calendars changed" "$out"
out=$("$CLI" "$D" list $(d 20261013) $(d 20261014))
expect "the server's copy back" "[Work] Design review" "$out"
absent "the spoilt one gone" "Design reviex" "$out"
out=$("$CLI" "$D" list $(d 20261001) $(d 20261101))
expect "the other events still there" "$(d 20261025) 10:00-11:00" "$out"

echo "== new event from the Atari"
WORK=$("$CLI" "$D" calendars | awk '/ Work /{print $1}')
"$CLI" "$D" add "$WORK" "Lunch with Avi" $(d 20261014)T1230 45 || bad "add"
srv=$("${CURL[@]}" "$BASE/alice/work/")
expect "on the server" "SUMMARY:Lunch with Avi" "$srv"
expect "with its zone" "DTSTART;TZID=Asia/Jerusalem:$(d 20261014)T123000" "$srv"
expect "and its reminder" "TRIGGER:-PT10M" "$srv"
out=$("$CLI" "$D" list $(d 20261014) $(d 20261015))
expect "in the local copy" "12:30-13:15 [Work] Lunch with Avi" "$out"

echo "== moving an event to another calendar"
# iCloud keeps one event per UID in an account, so the copy in the new
# calendar is refused until the old one is gone (gzproxy.py does the same)
HOMEC=$("$CLI" "$D" calendars | awk '/ בית /{print $1}')
"$CLI" "$D" add "$HOMEC" "Dentist" $(d 20261015)T0900 30 || bad "add to Home"
"$CLI" "$D" move-to "Dentist" "$WORK" || bad "move-to"
expect "now in Work on the server" "SUMMARY:Dentist" "$("${CURL[@]}" "$BASE/alice/work/")"
absent "and gone from Home" "SUMMARY:Dentist" "$("${CURL[@]}" "$BASE/alice/home/")"
out=$("$CLI" "$D" list $(d 20261015) $(d 20261016))
expect "and in the local copy" "09:00-09:30 [Work] Dentist" "$out"
absent "only once" "[בית] Dentist" "$out"
out=$("$CLI" "$D" sync)
expect "still there after a sync" "09:00-09:30 [Work] Dentist" "$("$CLI" "$D" list $(d 20261015) $(d 20261016))"

echo "== inviting people"
"$CLI" "$D" invite "Dentist" "Dana Levi <dana@example.org>, sam@example.net" || bad "invite"
srv=$("${CURL[@]}" "$BASE/alice/work/" | sed -e ':a' -e 'N' -e '$!ba' -e 's/\r\n //g')
dana=$(grep "mailto:dana@example.org" <<<"$srv" || true)
expect "Dana invited on the server, by name" 'CN=' "$dana"
expect "waiting for her answer" "PARTSTAT=NEEDS-ACTION" "$dana"
expect "and Sam" "mailto:sam@example.net" "$srv"
out=$("$CLI" "$D" people "Dentist")
expect "read back" "invited: Dana Levi <dana@example.org>, sam@example.net" "$out"
# Dana answers (her calendar server writes it into the event)
href=""
for h in $("${CURL[@]}" -X PROPFIND -H "Depth: 1" "$BASE/alice/work/" | grep -o '/alice/work/[^<]*\.ics'); do
  if [[ "$("${CURL[@]}" "$BASE$h")" == *"SUMMARY:Dentist"* ]]; then href=$h; break; fi
done
ics=$("${CURL[@]}" "$BASE$href" | sed -e ':a' -e 'N' -e '$!ba' -e 's/\r\n //g' | sed '/mailto:dana/s/PARTSTAT=NEEDS-ACTION/PARTSTAT=ACCEPTED/')
printf '%s\n' "$ics" | sed 's/$/\r/' | "${CURL[@]}" -X PUT -H "Content-Type: text/calendar" --data-binary @- -o /dev/null "$BASE$href"
"$CLI" "$D" sync >/dev/null
"$CLI" "$D" invite "Dentist" "Dana Levi <dana@example.org>, kim@example.net" || bad "change the invitees"
srv=$("${CURL[@]}" "$BASE$href" | sed -e ':a' -e 'N' -e '$!ba' -e 's/\r\n //g')
expect "Dana's answer kept" "PARTSTAT=ACCEPTED" "$(grep "mailto:dana@example.org" <<<"$srv" || true)"
absent "Sam taken away" "sam@example.net" "$srv"
expect "Kim added" "mailto:kim@example.net" "$srv"
n=$(grep -c "dana@example.org" <<<"$srv" || true)
if [ "$n" = 1 ]; then ok "Dana once"; else bad "Dana $n times"; fi
"$CLI" "$D" invite "Dentist" "" || bad "uninvite all"
absent "nobody invited any more" "ATTENDEE" "$("${CURL[@]}" "$BASE$href")"

echo "== a Hebrew title, renaming a series"
"$CLI" "$D" rename "ישיבת צוות" "ישיבת צוות שבועית" || bad "rename"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "renamed on the server" "ישיבת צוות שבועית" "$srv"
expect "the organizer kept" "ORGANIZER;CN=Avi:mailto:avi@example.com" "$srv"
expect "the attendee kept" "ATTENDEE;CN=Dana:mailto:dana@example.com" "$srv"
expect "the iPhone's property kept" "X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC" "$srv"

echo "== moving one occurrence"
"$CLI" "$D" move-one "ישיבת צוות שבועית" $(d 20261018)T1000 $(d 20261018)T1400 || bad "move-one"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "an exception on the server" "RECURRENCE-ID;TZID=Asia/Jerusalem:$(d 20261018)T100000" "$srv"
n=$(grep -c "^ORGANIZER" <<<"$srv" || true)
if [ "$n" = 2 ]; then ok "the series and the exception keep the organizer"; else bad "$n ORGANIZER lines"; fi
out=$("$CLI" "$D" list $(d 20261018) $(d 20261019))
expect "shown at the new time" "$(d 20261018) 14:00-15:00" "$out"
absent "not at the old time" "$(d 20261018) 10:00" "$out"

echo "== changing the same occurrence again: one exception, not two"
"$CLI" "$D" move-one "ישיבת צוות שבועית" $(d 20261018)T1400 $(d 20261018)T1600 || bad "move the moved one"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
n=$(grep -c "RECURRENCE-ID" <<<"$srv" || true)
if [ "$n" = 1 ]; then ok "still one exception on the server"; else bad "$n exceptions on the server"; fi
expect "it starts at 16:00" "DTSTART;TZID=Asia/Jerusalem:$(d 20261018)T160000" "$srv"
n=$(grep -c "^ORGANIZER\|^ATTENDEE" <<<"$srv" || true)
if [ "$n" = 4 ]; then ok "the exception keeps its organizer and attendee"; else bad "$n ORGANIZER and ATTENDEE lines"; fi
n=$(grep -c "^RRULE:FREQ=WEEKLY" <<<"$srv" || true)
if [ "$n" = 1 ]; then ok "the rule only in the series"; else bad "$n RRULE lines"; fi
out=$("$CLI" "$D" list $(d 20261018) $(d 20261019))
expect "shown at 16:00" "$(d 20261018) 16:00-17:00" "$out"
absent "not at 14:00 any more" "$(d 20261018) 14:00" "$out"

echo "== a server's change to this and all future occurrences"
put home/future.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:future@test
DTSTAMP:20261001T000000Z
DTSTART;TZID=Asia/Jerusalem:$(d 20261005)T080000
DTEND;TZID=Asia/Jerusalem:$(d 20261005)T083000
RRULE:FREQ=WEEKLY;COUNT=6
SUMMARY:Gym
END:VEVENT
BEGIN:VEVENT
UID:future@test
DTSTAMP:20261001T000000Z
RECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Asia/Jerusalem:$(d 20261019)T080000
DTSTART;TZID=Asia/Jerusalem:$(d 20261019)T070000
DTEND;TZID=Asia/Jerusalem:$(d 20261019)T074500
SUMMARY:Gym (earlier)
END:VEVENT
END:VCALENDAR
ICS
M1=$(date -d "12:00 $(d 20261001 | sed 's/\(....\)\(..\)\(..\)/\1-\2-\3/') + 7 days" +%Y-%m-01)
M2=$(date -d "$M1 + 1 month" +%Y-%m-01)
M3=$(date -d "$M1 + 2 months" +%Y-%m-01)
M4=$(date -d "$M1 + 3 months" +%Y%m%d)
L1=$(lastwd "$M1"); L2=$(lastwd "$M2"); L3=$(lastwd "$M3")
put work/lastday.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:lastday@test
DTSTAMP:20261001T000000Z
DTSTART;TZID=Asia/Jerusalem:${L1}T170000
DTEND;TZID=Asia/Jerusalem:${L1}T180000
RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1;COUNT=3
SUMMARY:Month-end report
END:VEVENT
END:VCALENDAR
ICS
"$CLI" "$D" sync >/dev/null
out=$("$CLI" "$D" list $(d 20261005) $(d 20261110))
expect "before the change: 8:00" "$(d 20261012) 08:00-08:30 [בית] Gym" "$out"
expect "from the change on: 7:00, longer, new title" "$(d 20261019) 07:00-07:45 [בית] Gym (earlier)" "$out"
expect "...and the weeks after it too" "$(d 20261102) 07:00-07:45 [בית] Gym (earlier)" "$out"
absent "no old 8:00 after the change" "$(d 20261026) 08:00" "$out"
out=$("$CLI" "$D" list $(d 20261001) $M4)
expect "last weekday of the month" "$L1 17:00-18:00 [Work] Month-end report" "$out"
expect "last weekday of the next" "$L2 17:00-18:00 [Work] Month-end report" "$out"
expect "last weekday of the one after" "$L3 17:00-18:00 [Work] Month-end report" "$out"

echo "== deleting one occurrence, then the rest of the series"
"$CLI" "$D" delete "ישיבת צוות שבועית" this $(d 20261018)T1600 || bad "delete the moved one"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "the moved one becomes an EXDATE" "EXDATE;TZID=Asia/Jerusalem:$(d 20261018)T100000" "$srv"
absent "and its exception goes" "RECURRENCE-ID" "$srv"
"$CLI" "$D" delete "ישיבת צוות שבועית" this $(d 20261011)T1000 || bad "delete this"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "an EXDATE on the server" "EXDATE;TZID=Asia/Jerusalem:$(d 20261011)T100000" "$srv"
out=$("$CLI" "$D" list $(d 20261001) $(d 20261101))
absent "the second Sunday gone" "$(d 20261011) 10:00" "$out"
expect "the first still there" "$(d 20261004) 10:00" "$out"
"$CLI" "$D" delete "ישיבת צוות שבועית" future $(d 20261101)T1000 || bad "delete future"
out=$("$CLI" "$D" list $(d 20261025) $(d 20261026))
expect "the Sunday before still there" "$(d 20261025) 10:00" "$out"
out=$("$CLI" "$D" list $(d 20261101) $(d 20261201))
absent "from then on gone" "ישיבת צוות" "$out"

echo "== a change made elsewhere comes in"
put work/review.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:review@test
DTSTAMP:20261002T000000Z
DTSTART:$(d 20261013)T130000Z
DTEND:$(d 20261013)T140000Z
SUMMARY:Design review (moved)
END:VEVENT
END:VCALENDAR
ICS
out=$("$CLI" "$D" sync)
expect "one calendar changed" "1 calendar changed" "$out"
out=$("$CLI" "$D" list $(d 20261013) $(d 20261014))
expect "the new time" "$(hm Asia/Jerusalem "$RD 13:00 UTC")-$(hm Asia/Jerusalem "$RD 14:00 UTC") [Work] Design review (moved)" "$out"

echo "== deleting an event"
"$CLI" "$D" delete "Lunch with Avi" || bad "delete"
code=$("${CURL[@]}" -o /dev/null -w "%{http_code}" "$BASE/alice/work/" )
srv=$("${CURL[@]}" "$BASE/alice/work/")
absent "gone from the server" "Lunch with Avi" "$srv"

echo "== the accessory's background refresh, in steps"
# the server changes behind the Atari's back: one event new, one changed,
# one gone; a copy of the folder takes a whole refresh to compare
D2="$W/bg"; rm -rf "$D2"; cp -r "$D" "$D2"
"${CURL[@]}" -X PUT -H "Content-Type: text/calendar" --data-binary @- -o /dev/null "$BASE/alice/work/bg-new.ics" <<ICS
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:bg-new@test
DTSTAMP:20261002T000000Z
DTSTART;TZID=Asia/Jerusalem:$(d 20261016)T080000
DTEND;TZID=Asia/Jerusalem:$(d 20261016)T083000
SUMMARY:Came in the background
BEGIN:VALARM
ACTION:DISPLAY
TRIGGER:-PT5M
END:VALARM
END:VEVENT
END:VCALENDAR
ICS
"${CURL[@]}" -X PUT -H "Content-Type: text/calendar" --data-binary @- -o /dev/null "$BASE/alice/work/review.ics" <<ICS
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:review@test
DTSTAMP:20261002T000000Z
DTSTART:$(d 20261013)T150000Z
DTEND:$(d 20261013)T160000Z
SUMMARY:Design review (again)
END:VEVENT
END:VCALENDAR
ICS
href=""
for h in $("${CURL[@]}" -X PROPFIND -H "Depth: 1" "$BASE/alice/work/" | grep -o '/alice/work/[^<]*\.ics'); do
  if [[ "$("${CURL[@]}" "$BASE$h")" == *"SUMMARY:Dentist"* ]]; then href=$h; break; fi
done
"${CURL[@]}" -X DELETE -o /dev/null "$BASE$href"
R1=$(d 20261012); R2=$(d 20261017)
# stopped half way (the window opened), at each point: what is in memory
# is what the cache reads back, and the next refresh finishes it
total=$("$CLI" "$D2" bgsync | sed -n 's/.* in \([0-9]*\) steps/\1/p'); rm -rf "$D2"; cp -r "$D" "$D2"
stops_bad=0
for k in $(seq 1 $((total - 1))); do
  D3="$W/bgstop"; rm -rf "$D3"; cp -r "$D2" "$D3"
  mem=$("$CLI" "$D3" bgsync $R1 $R2 $k | sed 1d)
  if [ "$mem" != "$("$CLI" "$D3" list $R1 $R2)" ]; then stops_bad=$((stops_bad + 1)); echo "    stopped after $k steps: the cache reads back differently"; fi
  "$CLI" "$D3" bgsync >/dev/null
  after=$("$CLI" "$D3" list $R1 $R2)
  if [[ "$after" != *"Came in the background"* || "$after" != *"Design review (again)"* || "$after" == *"Dentist"* ]]; then
    stops_bad=$((stops_bad + 1)); echo "    stopped after $k steps: not finished by the next"
  fi
done
if [ "$stops_bad" = 0 ]; then ok "stopped at each of $total steps: consistent, finished by the next"; else bad "$stops_bad stops went wrong"; fi
bg=$("$CLI" "$D" bgsync $R1 $R2) || bad "bgsync"
expect "one calendar changed, in steps" "1 calendar changed in" "$bg"
expect "the new event" "$(d 20261016) 08:00-08:30 [Work] Came in the background" "$bg"
expect "the changed one" "Design review (again)" "$bg"
absent "the old copy gone" "Design review (moved)" "$bg"
absent "the deleted one gone" "Dentist" "$bg"
fresh=$("$CLI" "$D" list $R1 $R2)
if [ "$(sed 1d <<<"$bg")" = "$fresh" ]; then ok "read back from the cache the same"; else bad "the cache reads back differently:
$(diff <(sed 1d <<<"$bg") <(echo "$fresh"))"; fi
"$CLI" "$D2" sync >/dev/null
whole=$("$CLI" "$D2" list $R1 $R2)
if [ "$whole" = "$fresh" ]; then ok "the same as a whole refresh"; else bad "differs from a whole refresh:
$(diff <(echo "$whole") <(echo "$fresh"))"; fi
out=$("$CLI" "$D" bgsync)
expect "the next one quiet" "0 calendars changed" "$out"
# many changed: the superseded copies go, the file written anew in steps
put_many() {
  for i in $(seq 1 36); do
    "${CURL[@]}" -X PUT -H "Content-Type: text/calendar" --data-binary @- -o /dev/null "$BASE/alice/work/many-$i.ics" <<ICS
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:many-$i@test
DTSTAMP:20261002T000000Z
DTSTART:$(d 20261125)T$(printf %02d $((i % 20)))0000Z
DURATION:PT15M
SUMMARY:$1 $i
END:VEVENT
END:VCALENDAR
ICS
  done
}
put_many "Many"
"$CLI" "$D" bgsync >/dev/null || bad "bgsync many"
put_many "Changed"
bg=$("$CLI" "$D" bgsync $(d 20261125) $(d 20261126)) || bad "bgsync changed"
n=$(grep -c "Changed" <<<"$bg" || true)
if [ "$n" = 36 ]; then ok "36 changed in the background"; else bad "$n of 36 changed"; fi
dat=$(grep -rl "many-1@test" "$D" --include="*.DAT")
n=$(grep -c "^R .*many-" "$dat" || true)
if [ "$n" = 36 ]; then ok "the cache written anew, one copy each"; else bad "$n copies in the cache"; fi
fresh=$("$CLI" "$D" list $(d 20261125) $(d 20261126))
if [ "$(sed 1d <<<"$bg")" = "$fresh" ]; then ok "and reads back the same"; else bad "reads back differently"; fi
for i in $(seq 1 36); do "${CURL[@]}" -X DELETE -o /dev/null "$BASE/alice/work/many-$i.ics"; done
"$CLI" "$D" bgsync >/dev/null || bad "bgsync deleted"
absent "the many gone again" "Changed" "$("$CLI" "$D" list $(d 20261125) $(d 20261126))"

echo "== time zone support: the same events seen from New York"
"$CLI" "$D" zone Asia/Jerusalem America/New_York
out=$("$CLI" "$D" list $(d 20261004) $(d 20261005))
N=$(d 20261004); ND="TZ=\"Asia/Jerusalem\" ${N:0:4}-${N:4:2}-${N:6:2}"
expect "10:00 Jerusalem in New York time" "$N $(hm America/New_York "$ND 10:00")-$(hm America/New_York "$ND 11:00")" "$out"
"$CLI" "$D" zone Asia/Jerusalem

echo "== a subscription"
out=$("$CLI" "$D" subscribe "http://127.0.0.1:$FEEDPORT/holidays.ics" 2>&1) || bad "subscribe: $out"
expect "named after the feed" "Holidays in Israel" "$out"
out=$("$CLI" "$D" list $(d 20261026) $(d 20261027))
expect "its events shown" "$(d 20261026) all-day   [Holidays in Israel] Sukkot test day" "$out"
out=$("$CLI" "$D" calendars)
expect "read-only" "Holidays in Israel (Subscription, 1 events, read-only)" "$out"

echo "== an On My Atari calendar, and importing into it"
"$CLI" "$D" local "Atari"
n=$("$CLI" "$D" calendars | grep -c "")
out=$("$CLI" "$D" import $((n - 1)) "$W/feed/holidays.ics")
expect "imported" "2 imported" "$out"
out=$("$CLI" "$D" list $(d 20261026) $(d 20261027))
expect "shown from the local calendar" "[Atari] Sukkot test day" "$out"

echo "== invitations (.ics): one event for the New Event dialog, several imported"
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:invite-77@example.org\r\nDTSTAMP:20261020T100000Z\r\nDTSTART:%sT070000Z\r\nDTEND:%sT080000Z\r\nSUMMARY:Board meeting\r\nLOCATION:Room 4\r\nDESCRIPTION:Quarterly numbers\r\nORGANIZER;CN=Alice:mailto:alice@example.org\r\nATTENDEE;CN=Bob;PARTSTAT=NEEDS-ACTION:mailto:bob@example.org\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT30M\r\nEND:VALARM\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' $(d 20261105) $(d 20261105) > "$W/invite.ics"
out=$("$CLI" "$D" read-ics "$W/invite.ics")
expect "one event, an invitation" "events 1" "$out"
expect "its METHOD" "method REQUEST" "$out"
expect "its UID" "uid invite-77@example.org" "$out"
expect "its title" "summary Board meeting" "$out"
expect "its start" "start $(d 20261105)T070000" "$out"
expect "in UTC, as sent" "zone UTC" "$out"
expect "its place" "location Room 4" "$out"
expect "its notes" "notes Quarterly numbers" "$out"
expect "its organiser" "organizer Alice <alice@example.org>" "$out"
expect "its invitees" "attendees Bob <bob@example.org>" "$out"
expect "its reminder" "alarm 30" "$out"
out=$("$CLI" "$D" accept-ics "$WORK" "$W/invite.ics") || bad "accept-ics: $out"
expect "saved as a new event" "saved invite-77@example.org" "$out"
raw=$("${CURL[@]}" "$BASE/alice/work/invite-77_example_org.ics")
expect "on the server with the invitation's UID" "UID:invite-77@example.org" "$raw"
expect "the organiser kept (Alice's invitation, not ours)" "ORGANIZER;CN=Alice:mailto:alice@example.org" "$raw"
expect "the invitee kept" "mailto:bob@example.org" "$raw"
absent "no METHOD in the calendar's copy" "METHOD:" "$raw"
out=$("$CLI" "$D" list $(d 20261105) $(d 20261106))
expect "shown on its day" "Board meeting" "$out"
sed -i 's/SUMMARY:Board meeting/SUMMARY:Board meeting (moved)/; s/T070000Z/T090000Z/; s/T080000Z/T100000Z/' "$W/invite.ics"
out=$("$CLI" "$D" accept-ics "$WORK" "$W/invite.ics") || bad "accept-ics again: $out"
expect "the update matched by UID" "updated invite-77@example.org" "$out"
out=$("$CLI" "$D" list $(d 20261105) $(d 20261106))
expect "the update in its place" "Board meeting (moved)" "$out"
[ "$(grep -c "Board meeting" <<<"$out")" = 1 ] && ok "one event, not two" || bad "the update made a second event: $out"
S=$(d 20261110); X=$(d 20261117)
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:series-9@example.org\r\nDTSTAMP:20261020T100000Z\r\nDTSTART:%sT080000Z\r\nDTEND:%sT090000Z\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\nSUMMARY:Weekly sync\r\nORGANIZER:mailto:alice@example.org\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nUID:series-9@example.org\r\nDTSTAMP:20261020T100000Z\r\nRECURRENCE-ID:%sT080000Z\r\nDTSTART:%sT100000Z\r\nDTEND:%sT110000Z\r\nSUMMARY:Weekly sync (later)\r\nORGANIZER:mailto:alice@example.org\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' $S $S $X $X $X > "$W/series.ics"
out=$("$CLI" "$D" read-ics "$W/series.ics")
expect "a series with a changed occurrence: one invitation" "events 1" "$out"
expect "its master for the dialog" "summary Weekly sync" "$out"
out=$("$CLI" "$D" accept-ics "$WORK" "$W/series.ics") || bad "accept-ics series: $out"
raw=$("${CURL[@]}" "$BASE/alice/work/series-9_example_org.ics")
expect "the changed occurrence kept with it" "RECURRENCE-ID" "$raw"
expect "... its own title" "Weekly sync (later)" "$raw"
cp "$W/feed/holidays.ics" "$W/two.ics"
out=$("$CLI" "$D" read-ics "$W/two.ics")
expect "several events: counted, none for the dialog" "events 2" "$out"
absent "several events: no single one" "uid " "$out"

if [ -n "${FALCON:-}" ]; then
  expect "the log shows TLS" "secure: TLS 1.2" "$(cat "$CAL_LOG")"
  # Radicale closes after every answer: the next connection resumes
  [ -z "${GZIP:-}" ] && expect "a later connection resumed the TLS session" ", resumed" "$(cat "$CAL_LOG")"
fi
if [ -n "${GZIP:-}" ]; then
  expect "answers came compressed and were unpacked" "unpacked in" "$(cat "$CAL_LOG")"
  if grep "bytes" "$CAL_LOG" | grep -v "new connection" | grep -q " in "; then ok "connections kept open between requests"
  else bad "every request opened a new connection"; fi
fi
if grep -qi "authorization: basic" "$CAL_LOG" 2>/dev/null; then bad "the log shows the password"; else ok "no password in the log"; fi

echo
if [ "$fails" = 0 ]; then echo "integration: all passed"; else echo "integration: $fails failed"; exit 1; fi
