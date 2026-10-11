/*
 * unit.c - tests for the calendar core: dates, the Hebrew calendar,
 * time zones, iCalendar in and out, repeating events, XML and URLs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ctime.h"
#include "tz.h"
#include "ics.h"
#include "xml.h"
#include "http.h"
#include "charset.h"
#include "util.h"

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; \
	printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long _a = (long)(a), _b = (long)(b); checks++; if (_a != _b) { fails++; \
	printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); checks++; \
	if (!_a || strcmp(_a, _b)) { fails++; \
	printf("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, _a ? _a : "(null)", _b); } } while (0)

static void test_dates(void)
{
	int y, m, d;
	DT t;
	CHECK_EQ(days_from_civil(1970, 1, 1), 0);
	CHECK_EQ(days_from_civil(2000, 3, 1), 11017);
	civil_from_days(days_from_civil(2026, 10, 10), &y, &m, &d);
	CHECK(y == 2026 && m == 10 && d == 10);
	civil_from_days(days_from_civil(1900, 2, 28) + 1, &y, &m, &d);
	CHECK(y == 1900 && m == 3 && d == 1);
	CHECK_EQ(wday_of(days_from_civil(2026, 10, 10)), 6);	/* a Saturday */
	CHECK_EQ(wday_of(days_from_civil(1969, 12, 31)), 3);
	CHECK_EQ(days_in_month(2024, 2), 29);
	CHECK_EQ(days_in_month(2100, 2), 28);
	ct_split(ct_make(2026, 10, 10, 14, 35), &t);
	CHECK(t.year == 2026 && t.mon == 10 && t.day == 10 && t.hour == 14 && t.min == 35 && t.wday == 6);
	CHECK_EQ(ct_days(-1), -1);
	CHECK_EQ(days_add_months(days_from_civil(2026, 1, 31), 1), days_from_civil(2026, 2, 28));
	{
		CTIME x;
		char s[24];
		CHECK_EQ(ct_parse("20261010", &x), 1);
		CHECK_EQ(ct_parse("20261010T093000", &x), 2);
		CHECK_EQ(x, ct_make(2026, 10, 10, 9, 30));
		CHECK_EQ(ct_parse("20261010T093000Z", &x), 3);
		CHECK_EQ(ct_parse("2026-10-10", &x), 0);
		ct_format_ics(x, 3, s, sizeof(s));
		CHECK_STR(s, "20261010T093000Z");
		fmt_time(14, 5, 0, s, sizeof(s));
		CHECK_STR(s, "2:05pm");
		fmt_time(0, 0, 0, s, sizeof(s));
		CHECK_STR(s, "12am");
		fmt_time(9, 0, 1, s, sizeof(s));
		CHECK_STR(s, "9:00");
	}
}

static void test_hebrew(void)
{
	HDATE h;
	char s[64];
	hebrew_from_days(days_from_civil(2026, 9, 12), &h);	/* Rosh Hashana 5787 */
	CHECK(h.year == 5787 && h.mon == 7 && h.day == 1);
	hebrew_from_days(days_from_civil(2026, 10, 10), &h);
	CHECK(h.year == 5787 && h.mon == 7 && h.day == 29);
	hebrew_from_days(days_from_civil(2025, 4, 13), &h);	/* 15 Nisan 5785 */
	CHECK(h.year == 5785 && h.mon == 1 && h.day == 15);
	hebrew_from_days(days_from_civil(2026, 3, 3), &h);	/* Purim 5786 */
	CHECK(h.year == 5786 && h.mon == 12 && h.day == 14);
	hebrew_from_days(days_from_civil(2024, 3, 24), &h);	/* Purim 5784, a leap year: Adar II */
	CHECK(h.year == 5784 && h.mon == 13 && h.day == 14);
	CHECK(hebrew_leap(5784) && !hebrew_leap(5785));
	CHECK_EQ(days_from_hebrew(5787, 7, 10), days_from_civil(2026, 9, 21));	/* Yom Kippur */
	/* round trip over three years */
	{
		long d, bad = 0;
		for (d = days_from_civil(2024, 1, 1); d < days_from_civil(2027, 1, 1); d++) {
			hebrew_from_days(d, &h);
			if (days_from_hebrew(h.year, h.mon, h.day) != d)
				bad++;
		}
		CHECK_EQ(bad, 0);
	}
	hebrew_numeral(15, s, sizeof(s));
	CHECK(!strcmp(s, "\xCA\"\xC7"));		/* tet " vav */
	hebrew_numeral(12, s, sizeof(s));
	CHECK(!strcmp(s, "\xCB\"\xC3"));		/* yod " bet */
	hebrew_numeral(1, s, sizeof(s));
	CHECK(!strcmp(s, "\xC2'"));			/* alef ' */
	hebrew_numeral(5787, s, sizeof(s));
	CHECK(!strcmp(s, "\xD7\xD6\xD2\"\xC8"));	/* tav shin pe " zayin */
	hebrew_from_days(days_from_civil(2026, 10, 10), &h);
	hebrew_format(&h, 1, s, sizeof(s));
	CHECK(!strcmp(s, "\xCC\"\xCA \xD7\xD6\xD5\xCB \xD7\xD6\xD2\"\xC8"));	/* kaf"tet tishri tashpaz */
}

static void test_tz(void)
{
	const TZONE *j = tz_find("Asia/Jerusalem"), *ny = tz_find("America/New_York"),
		    *ld = tz_find("Europe/London"), *sy = tz_find("Australia/Sydney");
	char s[24];
	CHECK(j && ny && ld && sy);
	CHECK(tz_find("Israel Standard Time") == j);
	CHECK(tz_find("asia/jerusalem") == j);
	CHECK(tz_find("Nowhere/Special") == 0);
	/* Israel 2026: summer time from Friday 27 March to Sunday 25 October */
	CHECK_EQ(tz_offset(j, ct_make(2026, 3, 26, 23, 59)), 120);
	CHECK_EQ(tz_offset(j, ct_make(2026, 3, 27, 0, 0)), 180);	/* 2:00 local */
	CHECK_EQ(tz_offset(j, ct_make(2026, 10, 24, 22, 59)), 180);
	CHECK_EQ(tz_offset(j, ct_make(2026, 10, 24, 23, 0)), 120);	/* 2:00 summer time */
	CHECK_EQ(tz_offset(j, ct_make(2026, 12, 1, 12, 0)), 120);
	/* 2:30 on 27 March 2026 doesn't happen in Jerusalem: counts as 3:30 */
	CHECK_EQ(tz_to_local(j, tz_to_utc(j, ct_make(2026, 3, 27, 2, 30))), ct_make(2026, 3, 27, 3, 30));
	/* 1:30 on 25 October happens twice: the first one (summer time) */
	CHECK_EQ(tz_to_utc(j, ct_make(2026, 10, 25, 1, 30)), ct_make(2026, 10, 24, 22, 30));
	/* New York: 8 March to 1 November 2026 */
	CHECK_EQ(tz_offset(ny, ct_make(2026, 3, 8, 6, 59)), -300);
	CHECK_EQ(tz_offset(ny, ct_make(2026, 3, 8, 7, 0)), -240);
	CHECK_EQ(tz_offset(ny, ct_make(2026, 11, 1, 5, 59)), -240);
	CHECK_EQ(tz_offset(ny, ct_make(2026, 11, 1, 6, 0)), -300);
	/* London: 29 March and 25 October, 01:00 UTC */
	CHECK_EQ(tz_offset(ld, ct_make(2026, 3, 29, 0, 59)), 0);
	CHECK_EQ(tz_offset(ld, ct_make(2026, 3, 29, 1, 0)), 60);
	/* Sydney, southern hemisphere: summer over the new year */
	CHECK_EQ(tz_offset(sy, ct_make(2026, 1, 15, 0, 0)), 660);
	CHECK_EQ(tz_offset(sy, ct_make(2026, 4, 4, 15, 59)), 660);
	CHECK_EQ(tz_offset(sy, ct_make(2026, 4, 4, 16, 0)), 600);
	CHECK_EQ(tz_offset(sy, ct_make(2026, 10, 3, 16, 0)), 660);
	CHECK_EQ(tz_offset(tz_find("Etc/GMT-2"), 0), 120);
	CHECK_EQ(tz_offset(tz_find("UTC+05:30"), 0), 330);
	tz_label(j, ct_make(2026, 7, 1, 0, 0), s, sizeof(s));
	CHECK_STR(s, "GMT+3");
	tz_label(tz_find("Asia/Kolkata"), 0, s, sizeof(s));
	CHECK_STR(s, "GMT+5:30");
	CHECK_STR(tz_city(tz_find("America/Los_Angeles")), "Los Angeles");
	{
		char buf[1200];
		tz_vtimezone(j, buf, sizeof(buf));
		CHECK(strstr(buf, "TZID:Asia/Jerusalem") != 0);
		CHECK(strstr(buf, "BYDAY=FR;BYMONTHDAY=23,24,25,26,27,28,29") != 0);
		CHECK(strstr(buf, "BYMONTH=10;BYDAY=-1SU") != 0);
	}
}

/* ---- iCalendar ---- */

typedef struct {
	EVENT *ev[32];
	int n;
} EVS;

static int keep(EVENT *e, void *ud)
{
	EVS *s = ud;
	if (s->n < 32)
		s->ev[s->n++] = e;
	else
		ev_free(e);
	return 1;
}

static void free_evs(EVS *s)
{
	int i;
	for (i = 0; i < s->n; i++)
		ev_free(s->ev[i]);
	s->n = 0;
}

typedef struct {
	CTIME s[400], e[400];
	int n;
	EVS *all;
} OCCS;

static void got_occ(EVENT *e, CTIME s, CTIME en, void *ud)
{
	OCCS *o = ud;
	if (o->n < 400) {
		o->s[o->n] = s;
		o->e[o->n] = en;
		o->n++;
	}
}

static int skip_exc(EVENT *m, CTIME orig, void *ud)
{
	OCCS *o = ud;
	int i;
	if (!o->all)
		return 0;
	for (i = 0; i < o->all->n; i++) {
		EVENT *x = o->all->ev[i];
		if (x->recurid == orig && !strcmp(x->uid, m->uid))
			return 1;
	}
	return 0;
}

static int expand_rule(const char *dtstart, const char *rule, CTIME from, CTIME to, OCCS *o)
{
	char text[800];
	EVS s;
	memset(&s, 0, sizeof(s));
	snprintf(text, sizeof(text),
		 "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:r1\r\nDTSTART;TZID=Asia/Jerusalem:%s\r\n"
		 "DURATION:PT1H\r\nRRULE:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n", dtstart, rule);
	ics_parse(text, (long)strlen(text), keep, &s, CT_NONE, CT_NONE);
	o->n = 0;
	o->all = 0;
	if (s.n == 1)
		ics_expand(s.ev[0], from, to, tz_find("Asia/Jerusalem"), got_occ, 0, o);
	free_evs(&s);
	return o->n;
}

static const char sample[] =
	"BEGIN:VCALENDAR\r\n"
	"VERSION:2.0\r\n"
	"PRODID:-//Apple Inc.//macOS 15.0//EN\r\n"
	"X-WR-CALNAME:\xd7\x91\xd7\x99\xd7\xaa\r\n"		/* "bayit" */
	"BEGIN:VTIMEZONE\r\n"
	"TZID:Asia/Jerusalem\r\n"
	"BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0200\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=FR;BYMONTHDAY=23,24,25,26,27,28,29\r\n"
	"DTSTART:20130329T020000\r\nTZNAME:IDT\r\nTZOFFSETTO:+0300\r\nEND:DAYLIGHT\r\n"
	"BEGIN:STANDARD\r\nTZOFFSETFROM:+0300\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\n"
	"DTSTART:20131027T020000\r\nTZNAME:IST\r\nTZOFFSETTO:+0200\r\nEND:STANDARD\r\n"
	"END:VTIMEZONE\r\n"
	"BEGIN:VEVENT\r\n"
	"UID:weekly-1@example.com\r\n"
	"DTSTAMP:20261001T080000Z\r\n"
	"SUMMARY:\xd7\xa4\xd7\x92\xd7\x99\xd7\xa9\xd7\x94 \xd7\xa9\xd7\x91\xd7\x95\xd7\xa2\xd7\x99\xd7\xaa\r\n"
	"LOCATION:Tel Aviv\\, office\r\n"
	"DESCRIPTION:Line one\\nLine two with a long text that goes on and on so that it has to be folded at seventy-f\r\n"
	" ive octets\r\n"
	"DTSTART;TZID=Asia/Jerusalem:20261004T100000\r\n"
	"DTEND;TZID=Asia/Jerusalem:20261004T110000\r\n"
	"RRULE:FREQ=WEEKLY;BYDAY=SU,WE;UNTIL=20261231T215959Z\r\n"
	"EXDATE;TZID=Asia/Jerusalem:20261007T100000\r\n"
	"ATTENDEE;CN=\"Dana\";PARTSTAT=ACCEPTED:mailto:dana@example.com\r\n"
	"X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC\r\n"
	"BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
	"END:VEVENT\r\n"
	"BEGIN:VEVENT\r\n"
	"UID:weekly-1@example.com\r\n"
	"RECURRENCE-ID;TZID=Asia/Jerusalem:20261011T100000\r\n"
	"DTSTART;TZID=Asia/Jerusalem:20261011T140000\r\n"
	"DTEND;TZID=Asia/Jerusalem:20261011T150000\r\n"
	"SUMMARY:Moved this week\r\n"
	"END:VEVENT\r\n"
	"BEGIN:VEVENT\r\n"
	"UID:allday-1\r\n"
	"DTSTART;VALUE=DATE:20261016\r\n"
	"DTEND;VALUE=DATE:20261019\r\n"
	"SUMMARY:Trip\r\n"
	"END:VEVENT\r\n"
	"BEGIN:VTODO\r\nUID:todo\r\nSUMMARY:not an event\r\nBEGIN:VALARM\r\nTRIGGER:-PT1M\r\nEND:VALARM\r\nEND:VTODO\r\n"
	"BEGIN:VEVENT\r\n"
	"UID:utc-1\r\n"
	"DTSTART:20261012T070000Z\r\n"
	"DTEND:20261012T073000Z\r\n"
	"SUMMARY:Call\r\n"
	"END:VEVENT\r\n"
	"END:VCALENDAR\r\n";

static void test_ics_parse(void)
{
	EVS s;
	OCCS o;
	const TZONE *j = tz_find("Asia/Jerusalem");
	EVENT *w;
	memset(&s, 0, sizeof(s));
	CHECK_EQ(ics_parse(sample, (long)strlen(sample), keep, &s, CT_NONE, CT_NONE), 4);
	CHECK_EQ(s.n, 4);
	CHECK(!strcmp(ics_calname, "\xC3\xCB\xD7"));		/* bet yod tav */
	w = s.ev[0];
	CHECK_STR(w->uid, "weekly-1@example.com");
	CHECK(!strcmp(w->summary, "\xD2\xC4\xCB\xD6\xC6 \xD6\xC3\xC7\xD1\xCB\xD7"));
	CHECK_STR(w->location, "Tel Aviv, office");
	CHECK(w->notes && strstr(w->notes, "Line one\nLine two") && strstr(w->notes, "seventy-five octets"));
	CHECK(w->tz == j);
	CHECK_EQ(w->start, ct_make(2026, 10, 4, 7, 0));	/* 10:00 summer time = 07:00 UTC */
	CHECK_EQ(w->end - w->start, 60);
	CHECK(w->rr && w->rr->freq == F_WEEKLY && w->rr->nbyday == 2);
	CHECK_EQ(w->rr->until, ct_make(2026, 12, 31, 23, 59));	/* in Jerusalem's winter time */
	CHECK_EQ(w->nexdate, 1);
	CHECK_EQ(w->exdate[0], ct_make(2026, 10, 7, 7, 0));
	CHECK_EQ(w->alarm, 15);
	CHECK_EQ(s.ev[1]->recurid, ct_make(2026, 10, 11, 7, 0));
	CHECK(s.ev[2]->allday && s.ev[2]->end - s.ev[2]->start == 3 * MIN_PER_DAY);
	CHECK(s.ev[3]->tz == tz_utc());
	/* the series in October, as Jerusalem sees it */
	memset(&o, 0, sizeof(o));
	o.all = &s;
	ics_expand(w, ct_make(2026, 10, 1, 0, 0), ct_make(2026, 11, 1, 0, 0), j, got_occ, skip_exc, &o);
	/* Sun 4, (Wed 7 excluded), Sun 11 replaced, Wed 14, Sun 18, Wed 21, Sun 25, Wed 28 */
	CHECK_EQ(o.n, 6);
	CHECK_EQ(o.s[0], ct_make(2026, 10, 4, 10, 0));
	CHECK_EQ(o.s[1], ct_make(2026, 10, 14, 10, 0));
	/* after the clocks change on 25 October it stays at 10:00 */
	CHECK_EQ(o.s[4], ct_make(2026, 10, 25, 10, 0));
	CHECK_EQ(o.s[5], ct_make(2026, 10, 28, 10, 0));
	/* seen from New York the same meetings are at 4:00 (3:00 after Israel's change) */
	o.n = 0;
	ics_expand(w, ct_make(2026, 10, 25, 0, 0), ct_make(2026, 10, 26, 0, 0), tz_find("America/New_York"),
		   got_occ, skip_exc, &o);
	CHECK_EQ(o.n, 1);
	CHECK_EQ(o.s[0], ct_make(2026, 10, 25, 4, 0));
	/* until the end of the year: last one on Wednesday 30 December */
	o.n = 0;
	ics_expand(w, ct_make(2026, 12, 1, 0, 0), ct_make(2027, 2, 1, 0, 0), j, got_occ, skip_exc, &o);
	CHECK_EQ(o.n, 9);
	CHECK_EQ(o.s[o.n - 1], ct_make(2026, 12, 30, 10, 0));
	/* all-day events keep their dates in any zone */
	o.n = 0;
	ics_expand(s.ev[2], ct_make(2026, 10, 17, 0, 0), ct_make(2026, 10, 18, 0, 0),
		   tz_find("Pacific/Honolulu"), got_occ, 0, &o);
	CHECK_EQ(o.n, 1);
	CHECK_EQ(o.s[0], ct_make(2026, 10, 16, 0, 0));
	free_evs(&s);
	/* a window drops events that happen once, outside it */
	CHECK_EQ(ics_parse(sample, (long)strlen(sample), keep, &s, ct_make(2027, 1, 1, 0, 0), ct_make(2027, 3, 1, 0, 0)), 2);
	free_evs(&s);
}

static void test_rrules(void)
{
	OCCS o;
	CTIME a = ct_make(2026, 1, 1, 0, 0), b = ct_make(2027, 1, 1, 0, 0);
	/* the last Friday of every month */
	CHECK_EQ(expand_rule("20260130T090000", "FREQ=MONTHLY;BYDAY=-1FR", a, b, &o), 12);
	CHECK_EQ(o.s[1], ct_make(2026, 2, 27, 9, 0));
	CHECK_EQ(o.s[11], ct_make(2026, 12, 25, 9, 0));
	/* the last weekday of the month */
	CHECK_EQ(expand_rule("20260130T090000", "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1", a, b, &o), 12);
	CHECK_EQ(o.s[4], ct_make(2026, 5, 29, 9, 0));	/* 31 May 2026 is a Sunday */
	/* every other week on Tuesday and Thursday, 5 times */
	CHECK_EQ(expand_rule("20260106T180000", "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;COUNT=5", a, b, &o), 5);
	CHECK_EQ(o.s[2], ct_make(2026, 1, 20, 18, 0));
	CHECK_EQ(o.s[4], ct_make(2026, 2, 3, 18, 0));
	/* a birthday on 29 February: only in leap years */
	CHECK_EQ(expand_rule("20240229T120000", "FREQ=YEARLY", ct_make(2024, 1, 1, 0, 0), ct_make(2033, 1, 1, 0, 0), &o), 3);
	CHECK_EQ(o.s[1], ct_make(2028, 2, 29, 12, 0));
	/* the 31st: months without one are skipped */
	CHECK_EQ(expand_rule("20260131T080000", "FREQ=MONTHLY", a, b, &o), 7);
	/* daily, far into the future (jumps there instead of walking) */
	CHECK_EQ(expand_rule("20000101T070000", "FREQ=DAILY;INTERVAL=3",
			     ct_make(2026, 10, 1, 0, 0), ct_make(2026, 10, 11, 0, 0), &o), 3);
	CHECK_EQ(o.s[0], ct_make(2026, 10, 2, 7, 0));
	/* yearly on the second Sunday of May (US Mother's Day) */
	CHECK_EQ(expand_rule("20100509T100000", "FREQ=YEARLY;BYMONTH=5;BYDAY=2SU", a, b, &o), 1);
	CHECK_EQ(o.s[0], ct_make(2026, 5, 10, 10, 0));
	/* a start off the rule still counts as the first occurrence */
	CHECK_EQ(expand_rule("20260102T100000", "FREQ=WEEKLY;BYDAY=MO;COUNT=3", a, b, &o), 3);
	CHECK_EQ(o.s[0], ct_make(2026, 1, 2, 10, 0));
	CHECK_EQ(o.s[2], ct_make(2026, 1, 12, 10, 0));
	{
		RRULE r;
		char s[120];
		CHECK(rrule_parse("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE", &r));
		rrule_describe(&r, s, sizeof(s));
		CHECK_STR(s, "Every 2 weeks on Monday and Wednesday");
		rrule_format(&r, 0, 0, s, sizeof(s));
		CHECK_STR(s, "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE");
		CHECK(rrule_parse("FREQ=MONTHLY;BYDAY=-1FR", &r));
		rrule_describe(&r, s, sizeof(s));
		CHECK_STR(s, "Every month on the last Friday");
		CHECK(!rrule_parse("FREQ=HOURLY", &r));
	}
	{
		long m;
		CHECK(ics_duration("-PT15M", &m) && m == -15);
		CHECK(ics_duration("P1DT2H", &m) && m == 26 * 60);
		CHECK(ics_duration("-P1W", &m) && m == -7 * 1440);
	}
}

static void test_ics_build(void)
{
	EVENT *e = ev_new();
	EVS s;
	char *text, *t2, *t3;
	long len, len2, len3;
	memset(&s, 0, sizeof(s));
	ics_now_utc = ct_make(2026, 10, 10, 12, 0);
	e->uid = strdup("new-1@atari");
	e->summary = strdup("\xD2\xC4\xCB\xD6\xC6; with, punctuation\\");	/* Hebrew + specials */
	e->notes = strdup("two\nlines");
	e->tz = tz_find("Asia/Jerusalem");
	e->start = tz_to_utc(e->tz, ct_make(2026, 10, 12, 9, 0));
	e->end = e->start + 90;
	e->alarm = 30;
	e->rr = calloc(1, sizeof(RRULE));
	rrule_parse("FREQ=DAILY;COUNT=3", e->rr);
	text = ics_build(e, 0, 0, &len);
	CHECK(text && strstr(text, "BEGIN:VTIMEZONE") && strstr(text, "DTSTART;TZID=Asia/Jerusalem:20261012T090000"));
	CHECK(strstr(text, "TRIGGER:-PT30M") != 0);
	CHECK(strstr(text, "RRULE:FREQ=DAILY;COUNT=3") != 0);
	{
		/* an all-day event's alert at 9:00 on the day */
		EVENT *a = ev_new();
		EVS t;
		char *x;
		long xl;
		memset(&t, 0, sizeof(t));
		a->uid = strdup("ad");
		a->allday = 1;
		a->start = ct_make(2026, 10, 16, 0, 0);
		a->end = a->start + MIN_PER_DAY;
		a->alarm = -540;
		x = ics_build(a, 0, 0, &xl);
		CHECK(strstr(x, "TRIGGER:PT540M") != 0);
		ics_parse(x, xl, keep, &t, CT_NONE, CT_NONE);
		CHECK(t.n == 1 && t.ev[0]->alarm == -540);
		free_evs(&t);
		free(x);
		ev_free(a);
	}
	CHECK(strstr(text, "SUMMARY:\xd7\xa4\xd7\x92\xd7\x99\xd7\xa9\xd7\x94\\; with\\, punctuation\\\\") != 0);
	CHECK(strstr(text, "DESCRIPTION:two\\nlines") != 0);
	/* read back */
	ics_parse(text, len, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 1);
	if (s.n) {
		CHECK_STR(s.ev[0]->summary, e->summary);
		CHECK_STR(s.ev[0]->notes, "two\nlines");
		CHECK_EQ(s.ev[0]->start, e->start);
		CHECK_EQ(s.ev[0]->end, e->end);
		CHECK_EQ(s.ev[0]->alarm, 30);
		CHECK(s.ev[0]->rr && s.ev[0]->rr->count == 3);
	}
	free_evs(&s);
	free(text);
	ev_free(e);

	/* editing an event from another program keeps what we don't edit */
	ics_parse(sample, (long)strlen(sample), keep, &s, CT_NONE, CT_NONE);
	e = ev_dup(s.ev[0]);
	free(e->summary);
	e->summary = strdup("Weekly meeting");
	e->alarm = -1;
	text = ics_build(e, sample, (long)strlen(sample), &len);
	CHECK(strstr(text, "SUMMARY:Weekly meeting") != 0);
	CHECK(strstr(text, "ATTENDEE;CN=\"Dana\";PARTSTAT=ACCEPTED:mailto:dana@example.com") != 0);
	CHECK(strstr(text, "X-APPLE-TRAVEL-ADVISORY-BEHAVIOR:AUTOMATIC") != 0);
	CHECK(strstr(text, "SUMMARY:Moved this week") != 0);	/* the exception stays */
	CHECK(strstr(text, "TRIGGER:-PT15M") == 0);		/* the alarm was removed */
	CHECK(strstr(text, "SUMMARY:Trip") != 0);
	{
		/* exactly one master, one VTIMEZONE */
		char *p = text;
		int masters = 0, tzs = 0;
		while ((p = strstr(p, "RRULE:FREQ=WEEKLY"))) {
			masters++;
			p++;
		}
		for (p = text; (p = strstr(p, "BEGIN:VTIMEZONE")); p++)
			tzs++;
		CHECK_EQ(masters, 1);
		CHECK_EQ(tzs, 1);
	}
	/* no line longer than 75 octets */
	{
		char *p = text, *nl;
		int longest = 0;
		while ((nl = strstr(p, "\r\n"))) {
			if (nl - p > longest)
				longest = (int)(nl - p);
			p = nl + 2;
		}
		CHECK(longest <= 75);
	}
	free_evs(&s);
	ics_parse(text, len, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 4);
	CHECK(s.n && !strcmp(s.ev[0]->summary, "Weekly meeting") && s.ev[0]->alarm == -1);
	free_evs(&s);
	/* an exception for 18 October */
	free(e->summary);
	e->summary = strdup("Only this once");
	e->recurid = tz_to_utc(e->tz, ct_make(2026, 10, 18, 10, 0));
	e->start = tz_to_utc(e->tz, ct_make(2026, 10, 18, 16, 0));
	e->end = e->start + 60;
	free(e->rr);
	e->rr = 0;
	t2 = ics_build(e, text, len, &len2);
	ics_parse(t2, len2, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 5);
	CHECK(strstr(t2, "RECURRENCE-ID;TZID=Asia/Jerusalem:20261018T100000") != 0);
	free_evs(&s);
	/* written again: replaces the old exception, doesn't add another */
	t3 = ics_build(e, t2, len2, &len3);
	ics_parse(t3, len3, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 5);
	free_evs(&s);
	free(t3);
	/* dropping exceptions */
	t3 = ics_drop_exceptions(t2, len2, tz_to_utc(e->tz, ct_make(2026, 10, 11, 10, 0)), &len3);
	ics_parse(t3, len3, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 4);
	free_evs(&s);
	free(t3);
	t3 = ics_drop_exceptions(t2, len2, CT_NONE, &len3);
	ics_parse(t3, len3, keep, &s, CT_NONE, CT_NONE);
	CHECK_EQ(s.n, 3);
	free_evs(&s);
	free(t3);
	free(t2);
	free(text);
	ev_free(e);
}

static void test_learned_zone(void)
{
	static const char cal[] =
		"BEGIN:VCALENDAR\nBEGIN:VTIMEZONE\nTZID:My Office Zone\n"
		"BEGIN:STANDARD\nDTSTART:16011104T020000\nRRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\n"
		"TZOFFSETFROM:-0400\nTZOFFSETTO:-0500\nEND:STANDARD\n"
		"BEGIN:DAYLIGHT\nDTSTART:16010311T020000\nRRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\n"
		"TZOFFSETFROM:-0500\nTZOFFSETTO:-0400\nEND:DAYLIGHT\nEND:VTIMEZONE\n"
		"BEGIN:VEVENT\nUID:x\nDTSTART;TZID=My Office Zone:20260715T090000\nDTEND;TZID=\"My Office Zone\":20260715T100000\n"
		"SUMMARY:Outlook meeting\nEND:VEVENT\nEND:VCALENDAR\n";
	EVS s;
	const TZONE *z;
	memset(&s, 0, sizeof(s));
	ics_parse(cal, (long)strlen(cal), keep, &s, CT_NONE, CT_NONE);
	z = tz_find("My Office Zone");
	CHECK(z != 0);
	if (z) {
		CHECK_EQ(z->std, -300);
		CHECK_EQ(z->dst, 60);
		CHECK_EQ(tz_offset(z, ct_make(2026, 3, 8, 7, 0)), -240);
		CHECK_EQ(tz_offset(z, ct_make(2026, 11, 1, 6, 0)), -300);
	}
	CHECK_EQ(s.n, 1);
	if (s.n) {
		CHECK_EQ(s.ev[0]->start, ct_make(2026, 7, 15, 13, 0));
		CHECK_EQ(s.ev[0]->end, ct_make(2026, 7, 15, 14, 0));
	}
	free_evs(&s);
}

static void test_xml(void)
{
	static const char x[] =
		"<?xml version=\"1.0\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n"
		" <D:response><D:href>/cal/a%20b.ics</D:href>\n"
		"  <D:propstat><D:prop><D:getetag>\"abc&amp;1\"</D:getetag>"
		"<C:calendar-data xmlns:C=\"urn:ietf:params:xml:ns:caldav\"><![CDATA[BEGIN:VCALENDAR\r\n<x>]]>&#1488;</C:calendar-data>"
		"<D:resourcetype><D:collection/><cal:calendar xmlns:cal=\"x\"/></D:resourcetype>"
		"</D:prop></D:propstat></D:response><!-- c --></D:multistatus>";
	XMLP p;
	int t, n = 0;
	char seen[400] = "";
	xml_init(&p, x, (long)strlen(x));
	while ((t = xml_next(&p)) != X_EOF) {
		char item[200];
		if (t == X_TEXT)
			snprintf(item, sizeof(item), "[%s]", p.text);
		else
			snprintf(item, sizeof(item), "%s%s ", t == X_END ? "/" : t == X_EMPTY ? "!" : "", p.name);
		if (strlen(seen) + strlen(item) < sizeof(seen))
			strcat(seen, item);
		n++;
	}
	xml_done(&p);
	CHECK_STR(seen, "multistatus response href [/cal/a%20b.ics]/href propstat prop getetag "
		  "[\"abc&1\"]/getetag calendar-data [BEGIN:VCALENDAR\r\n<x>\xd7\x90]/calendar-data "
		  "resourcetype !collection !calendar /resourcetype /prop /propstat /response /multistatus ");
}

static void test_urls(void)
{
	URL u;
	char s[300];
	CHECK(url_parse("webcal://p42-calendars.icloud.com/published/2/abc?x=1", &u));
	CHECK(u.tls && u.port == 443 && !strcmp(u.host, "p42-calendars.icloud.com"));
	CHECK_STR(u.path, "/published/2/abc?x=1");
	CHECK(url_parse("http://192.168.1.10:5232", &u));
	CHECK(!u.tls && u.port == 5232 && !strcmp(u.path, "/"));
	CHECK(!url_parse("ftp://x/y", &u));
	url_resolve("https://caldav.icloud.com/123/principal/", "/123/calendars/", s, sizeof(s));
	CHECK_STR(s, "https://caldav.icloud.com/123/calendars/");
	url_resolve("https://caldav.icloud.com/", "https://p01-caldav.icloud.com:443/1/calendars/", s, sizeof(s));
	CHECK_STR(s, "https://p01-caldav.icloud.com:443/1/calendars/");
	url_resolve("http://h:8080/a/b/c.ics", "d.ics", s, sizeof(s));
	CHECK_STR(s, "http://h:8080/a/b/d.ics");
	http_basic("Aladdin", "open sesame", s, sizeof(s));
	CHECK_STR(s, "QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
}

int main(void)
{
	test_dates();
	test_hebrew();
	test_tz();
	test_ics_parse();
	test_rrules();
	test_ics_build();
	test_learned_zone();
	test_xml();
	test_urls();
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
