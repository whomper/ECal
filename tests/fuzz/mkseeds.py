#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Writes tests/fuzz/seeds/: the starting inputs for make fuzz, answers
as calendar servers give them. For fuzz_dav the first byte picks the
request (see fuzz_dav.c); the rest is what the server says.

tests/fuzz/found/ holds the inputs that once broke a parser, as the fuzzer
saved them; make test runs them all again (common/tests/fuzz/replay.c)."""
import gzip, os, zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def put(target, name, data):
    d = os.path.join(HERE, "seeds", target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def http(status, body, headers=(), chunked=False, gz=False):
    b = body.encode() if isinstance(body, str) else body
    h = ["HTTP/1.1 %s" % status, "Content-Type: application/xml; charset=utf-8"]
    h += list(headers)
    if gz:
        b = gzip.compress(b)
        h.append("Content-Encoding: gzip")
    if chunked:
        h.append("Transfer-Encoding: chunked")
        parts = [b[i:i + 40] for i in range(0, len(b), 40)]
        b = b"".join(b"%x\r\n" % len(p) + p + b"\r\n" for p in parts) + b"0\r\n\r\n"
    else:
        h.append("Content-Length: %d" % len(b))
    return ("\r\n".join(h) + "\r\n\r\n").encode() + b


def ms(*responses):
    return ('<?xml version="1.0" encoding="utf-8"?>\n<d:multistatus xmlns:d="DAV:" '
            'xmlns:c="urn:ietf:params:xml:ns:caldav" xmlns:cs="http://calendarserver.org/ns/" '
            'xmlns:ic="http://apple.com/ns/ical/">' + "".join(responses) + "</d:multistatus>")


def resp(href, props, status="HTTP/1.1 200 OK"):
    return "<d:response><d:href>%s</d:href><d:propstat><d:prop>%s</d:prop><d:status>%s</d:status></d:propstat></d:response>" % (href, props, status)


EVENT = ("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Example//EN\r\n"
         "BEGIN:VTIMEZONE\r\nTZID:Asia/Jerusalem\r\nBEGIN:STANDARD\r\nDTSTART:19701025T020000\r\n"
         "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nTZOFFSETFROM:+0300\r\nTZOFFSETTO:+0200\r\nEND:STANDARD\r\n"
         "BEGIN:DAYLIGHT\r\nDTSTART:19700327T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1FR\r\n"
         "TZOFFSETFROM:+0200\r\nTZOFFSETTO:+0300\r\nEND:DAYLIGHT\r\nEND:VTIMEZONE\r\n"
         "BEGIN:VEVENT\r\nUID:a1@example.org\r\nDTSTAMP:20261001T080000Z\r\n"
         "DTSTART;TZID=Asia/Jerusalem:20261005T100000\r\nDURATION:PT1H30M\r\n"
         "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;UNTIL=20271231T000000Z\r\n"
         "EXDATE;TZID=Asia/Jerusalem:20261019T100000\r\nSUMMARY:Team meeting\\, weekly\r\n"
         "DESCRIPTION:Line one\\nline two \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d\r\n LOCATION:Room 4\r\n"
         "BEGIN:VALARM\r\nTRIGGER:-PT15M\r\nACTION:DISPLAY\r\nEND:VALARM\r\nEND:VEVENT\r\n"
         "BEGIN:VEVENT\r\nUID:a1@example.org\r\nRECURRENCE-ID;TZID=Asia/Jerusalem:20261102T100000\r\n"
         "DTSTART;TZID=Asia/Jerusalem:20261102T120000\r\nDTEND;TZID=Asia/Jerusalem:20261102T130000\r\n"
         "SUMMARY:Moved\r\nEND:VEVENT\r\n"
         "BEGIN:VEVENT\r\nUID:b2@example.org\r\nDTSTART;VALUE=DATE:20261010\r\nDTEND;VALUE=DATE:20261011\r\n"
         "RRULE:FREQ=YEARLY;RSCALE=HEBREW;BYMONTH=1;BYMONTHDAY=1\r\nSUMMARY:Rosh Hashana\r\n"
         "ATTENDEE;CN=Dana;PARTSTAT=ACCEPTED:mailto:dana@example.org\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")


def main():
    home = "/calendars/me/"
    principal = http("207 Multi-Status", ms(resp("/", "<d:current-user-principal><d:href>/principals/me/</d:href></d:current-user-principal>")))
    homeset = http("207 Multi-Status", ms(resp("/principals/me/", "<c:calendar-home-set><d:href>%s</d:href></c:calendar-home-set>" % home)))
    cals = http("207 Multi-Status", ms(
        resp(home, "<d:resourcetype><d:collection/></d:resourcetype>"),
        resp(home + "home/", "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype><d:displayname>Home &amp; work</d:displayname>"
             "<ic:calendar-color>#FF2968FF</ic:calendar-color><cs:getctag>\"ctag-1\"</cs:getctag>"
             "<c:supported-calendar-component-set><c:comp name=\"VEVENT\"/></c:supported-calendar-component-set>"
             "<d:current-user-privilege-set><d:privilege><d:write/></d:privilege></d:current-user-privilege-set>")), chunked=True)
    put("dav", "discover", b"\x00" + principal + homeset + cals)
    put("dav", "discover-gateway", b"\x80" + principal + homeset + cals)
    put("dav", "discover-401-digest", b"\x00" + http("401 Unauthorized", "no", ['WWW-Authenticate: Digest realm="cal", nonce="abc123", qop="auth", algorithm=MD5']) + principal + homeset + cals)
    put("dav", "discover-redirect", b"\x00" + http("301 Moved", "", ["Location: http://cal.example.org/dav/"]) + principal + homeset + cals)
    put("dav", "ctag", b"\x01" + http("207 Multi-Status", ms(resp("/calendars/me/home/", "<cs:getctag>ctag-2</cs:getctag>")), gz=True))
    items = "".join(resp("/calendars/me/home/%d.ics" % i, "<d:getetag>\"e%d\"</d:getetag>" % i) for i in range(5))
    put("dav", "list", b"\x02" + http("207 Multi-Status", ms(items)))
    put("dav", "list-chunked-gzip", b"\x02" + http("207 Multi-Status", ms(items), chunked=True, gz=True))
    data = ms(resp("/calendars/me/home/a.ics", "<d:getetag>\"e1\"</d:getetag><c:calendar-data>%s</c:calendar-data>" %
                   EVENT.replace("&", "&amp;").replace("<", "&lt;")))
    put("dav", "multiget", b"\x03" + http("207 Multi-Status", data))
    put("dav", "put", b"\x04" + http("201 Created", "", ['ETag: "e9"']))
    put("dav", "put-412", b"\x0c" + http("412 Precondition Failed", "changed"))
    put("dav", "delete", b"\x05" + http("204 No Content", ""))
    put("dav", "get-ics", b"\x06" + http("200 OK", EVENT, ['Content-Type: text/calendar', 'ETag: "f1"', "Last-Modified: Thu, 15 Oct 2026 08:00:00 GMT"]))
    put("dav", "get-ics-sink", b"\x07" + http("200 OK", EVENT, chunked=True))
    put("dav", "get-304", b"\x06" + http("304 Not Modified", ""))
    put("ics", "event", EVENT)
    put("ics", "lf-only", EVENT.replace("\r\n", "\n"))
    for i, r in enumerate(["FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE;UNTIL=20271231T000000Z",
                           "FREQ=MONTHLY;BYDAY=-1FR;COUNT=10", "FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=29",
                           "FREQ=YEARLY;RSCALE=HEBREW;BYMONTH=5L;BYMONTHDAY=14;SKIP=FORWARD",
                           "FREQ=DAILY;BYHOUR=9,17;BYSETPOS=1,-1;WKST=SU",
                           "FREQ=MONTHLY;BYMONTHDAY=1,15,-1;BYWEEKNO=20;BYYEARDAY=-1"]):
        put("rrule", "r%d" % i, r)
    put("xml", "multistatus", data)
    put("xml", "entities", '<a x="1" y=\'2\'>&lt;&amp;&#1488;&#x5d0;&quot;<![CDATA[x<y]]><!-- c --><b/></a>')
    text = (EVENT * 3).encode()
    put("inflate", "gzip", gzip.compress(text))
    put("inflate", "zlib", zlib.compress(text))
    raw = zlib.compressobj(9, zlib.DEFLATED, -15)
    put("inflate", "raw", raw.compress(text) + raw.flush())


if __name__ == "__main__":
    main()
