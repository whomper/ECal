#!/bin/bash
# Calendar's core against a real CalDAV server (Radicale) and a published
# feed, with cal-cli (the same calstore/CalDAV/HTTP/TLS code as the Atari):
#
#   tests/integration.sh             plain HTTP, straight to the server
#   GATEWAY=1 tests/integration.sh   through the Pi gateway (calgate.py)
#   FALCON=1 tests/integration.sh    Falcon mode: TLS in Calendar itself,
#                                    the server's certificate checked
#                                    against a test CA
#
# Needs: pip install radicale; FALCON=1 also needs openssl.
set -euo pipefail
cd "$(dirname "$0")/.."
# the Atari clock runs in its home zone: so does this one
export TZ=Asia/Jerusalem
CLI=$PWD/build/cal-cli
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
  openssl req -x509 -newkey rsa:2048 -nodes -days 30 -subj "/CN=Calendar Test CA" \
    -keyout "$W/ca.key" -out "$W/ca.pem" -addext "basicConstraints=critical,CA:true" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
  openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$W/srv.key" -out "$W/srv.csr" 2>/dev/null
  printf "subjectAltName=DNS:localhost\nextendedKeyUsage=serverAuth\n" > "$W/ext"
  openssl x509 -req -in "$W/srv.csr" -CA "$W/ca.pem" -CAkey "$W/ca.key" -CAcreateserial \
    -days 30 -extfile "$W/ext" -out "$W/srv.pem" 2>/dev/null
  SSL=(--server-ssl --server-certificate "$W/srv.pem" --server-key "$W/srv.key")
  export CACERT=$W/ca.pem
fi
python3 -m radicale --server-hosts "127.0.0.1:$PORT" --storage-filesystem-folder "$W/store" \
  --auth-type htpasswd --auth-htpasswd-filename "$W/users" --auth-htpasswd-encryption plain \
  "${SSL[@]}" > "$W/radicale.log" 2>&1 &
PIDS+=($!)
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
# a weekly meeting in Hebrew, made by "an iPhone", with an attendee Calendar doesn't edit
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
DTSTART;TZID=Asia/Jerusalem:20261004T100000
DTEND;TZID=Asia/Jerusalem:20261004T110000
RRULE:FREQ=WEEKLY;BYDAY=SU
SUMMARY:ישיבת צוות
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
DTSTART:20261013T120000Z
DTEND:20261013T130000Z
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
DTSTART;VALUE=DATE:20261026
DTEND;VALUE=DATE:20261027
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
  python3 gateway/calgate.py --port "$GWPORT" --bind 127.0.0.1 --allow-local > "$W/gw.log" 2>&1 &
  PIDS+=($!)
  sleep 0.5
  "$CLI" "$D" gateway 127.0.0.1 "$GWPORT"
  echo "(through the gateway on port $GWPORT)"
fi
if [ -n "${FALCON:-}" ]; then
  "$CLI" "$D" falcon 1
  echo "(Falcon mode: TLS in Calendar)"
fi
"$CLI" "$D" zone Asia/Jerusalem
export CAL_LOG=$W/CALENDAR.LOG

echo "== account and first sync"
"$CLI" "$D" caldav Radicale "$BASE/" alice wonderland || bad "caldav account"
out=$("$CLI" "$D" calendars)
expect "the Hebrew calendar found" "בית" "$out"
expect "the work calendar found" "Work" "$out"
absent "the task list left out" "Tasks" "$out"
out=$("$CLI" "$D" list 20261001 20261101)
expect "weekly meeting in Hebrew, 10:00 Jerusalem" "20261004 10:00-11:00 [" "$out"
expect "it repeats" "20261025 10:00-11:00" "$out"
expect "a UTC event shown in Israel time" "20261013 15:00-16:00 [Work] Design review" "$out"

echo "== nothing changed: nothing fetched"
out=$("$CLI" "$D" sync)
expect "second sync is quiet" "0 calendars changed" "$out"

echo "== new event from the Atari"
WORK=$("$CLI" "$D" calendars | awk '/ Work /{print $1}')
"$CLI" "$D" add "$WORK" "Lunch with Avi" 20261014T1230 45 || bad "add"
srv=$("${CURL[@]}" "$BASE/alice/work/")
expect "on the server" "SUMMARY:Lunch with Avi" "$srv"
expect "with its zone" "DTSTART;TZID=Asia/Jerusalem:20261014T123000" "$srv"
expect "and its reminder" "TRIGGER:-PT10M" "$srv"
out=$("$CLI" "$D" list 20261014 20261015)
expect "in the local copy" "12:30-13:15 [Work] Lunch with Avi" "$out"

echo "== a Hebrew title, renaming a series"
"$CLI" "$D" rename "ישיבת צוות" "ישיבת צוות שבועית" || bad "rename"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "renamed on the server" "ישיבת צוות שבועית" "$srv"
expect "the attendee kept" "ATTENDEE;CN=Dana:mailto:dana@example.com" "$srv"
expect "the iPhone's property kept" "X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC" "$srv"

echo "== moving one occurrence"
"$CLI" "$D" move-one "ישיבת צוות שבועית" 20261018T1000 20261018T1400 || bad "move-one"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "an exception on the server" "RECURRENCE-ID;TZID=Asia/Jerusalem:20261018T100000" "$srv"
out=$("$CLI" "$D" list 20261018 20261019)
expect "shown at the new time" "20261018 14:00-15:00" "$out"
absent "not at the old time" "20261018 10:00" "$out"

echo "== deleting one occurrence, then the rest of the series"
"$CLI" "$D" delete "ישיבת צוות שבועית" this 20261011T1000 || bad "delete this"
srv=$("${CURL[@]}" "$BASE/alice/home/weekly.ics")
expect "an EXDATE on the server" "EXDATE;TZID=Asia/Jerusalem:20261011T100000" "$srv"
out=$("$CLI" "$D" list 20261001 20261101)
absent "11 October gone" "20261011 10:00" "$out"
expect "4 October still there" "20261004 10:00" "$out"
"$CLI" "$D" delete "ישיבת צוות שבועית" future 20261101T1000 || bad "delete future"
out=$("$CLI" "$D" list 20261025 20261201)
expect "25 October still there" "20261025 10:00" "$out"
absent "November gone" "202611" "$out"

echo "== a change made elsewhere comes in"
put work/review.ics <<ICS >/dev/null
BEGIN:VCALENDAR
VERSION:2.0
PRODID:test
BEGIN:VEVENT
UID:review@test
DTSTAMP:20261002T000000Z
DTSTART:20261013T130000Z
DTEND:20261013T140000Z
SUMMARY:Design review (moved)
END:VEVENT
END:VCALENDAR
ICS
out=$("$CLI" "$D" sync)
expect "one calendar changed" "1 calendar changed" "$out"
out=$("$CLI" "$D" list 20261013 20261014)
expect "the new time" "16:00-17:00 [Work] Design review (moved)" "$out"

echo "== deleting an event"
"$CLI" "$D" delete "Lunch with Avi" || bad "delete"
code=$("${CURL[@]}" -o /dev/null -w "%{http_code}" "$BASE/alice/work/" )
srv=$("${CURL[@]}" "$BASE/alice/work/")
absent "gone from the server" "Lunch with Avi" "$srv"

echo "== time zone support: the same events seen from New York"
"$CLI" "$D" zone Asia/Jerusalem America/New_York
out=$("$CLI" "$D" list 20261004 20261005)
expect "10:00 Jerusalem is 3:00 in New York" "20261004 03:00-04:00" "$out"
"$CLI" "$D" zone Asia/Jerusalem

echo "== a subscription"
out=$("$CLI" "$D" subscribe "http://127.0.0.1:$FEEDPORT/holidays.ics" 2>&1) || bad "subscribe: $out"
expect "named after the feed" "Holidays in Israel" "$out"
out=$("$CLI" "$D" list 20261026 20261027)
expect "its events shown" "20261026 all-day   [Holidays in Israel] Sukkot test day" "$out"
out=$("$CLI" "$D" calendars)
expect "read-only" "Holidays in Israel (Subscription, 1 events, read-only)" "$out"

echo "== an On My Atari calendar, and importing into it"
"$CLI" "$D" local "Atari"
n=$("$CLI" "$D" calendars | grep -c "")
out=$("$CLI" "$D" import $((n - 1)) "$W/feed/holidays.ics")
expect "imported" "2 imported" "$out"
out=$("$CLI" "$D" list 20261026 20261027)
expect "shown from the local calendar" "[Atari] Sukkot test day" "$out"

if [ -n "${FALCON:-}" ]; then
  expect "the log shows TLS" "secure: TLS 1.2" "$(cat "$CAL_LOG")"
fi
if grep -qi "authorization: basic" "$CAL_LOG" 2>/dev/null; then bad "the log shows the password"; else ok "no password in the log"; fi

echo
if [ "$fails" = 0 ]; then echo "integration: all passed"; else echo "integration: $fails failed"; exit 1; fi
