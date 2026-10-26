/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rrjump: a series' occurrences are the same whether the walk jumps to
 * the range asked for (rr_walk's hint, what a view does) or walks to it
 * from DTSTART. For each rule (RFC 5545's examples, and bigcal's), every
 * window of a sweep across the years is expanded both ways and compared;
 * the sums printed make a digest, the same on the build machine and as
 * 68000 code under qemu-m68k (make rrjump-m68k), where the division
 * shortcuts (div16) run.
 *
 *   rrjump        exit 1 on any difference
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ics.h"
#include "tz.h"
#include "ctime.h"

typedef struct {
	CTIME s[4000];
	int n;
	CTIME from, to;		/* kept: only those in [from, to) (the walk from DTSTART) */
} SET;

static void add(EVENT *e, CTIME s, CTIME en, void *ud)
{
	SET *k = ud;
	(void)e;
	if (k->from != CT_NONE && !(en > k->from || (en == s && s >= k->from)))
		return;
	if (k->to != CT_NONE && s >= k->to)
		return;
	if (k->n < 4000)
		k->s[k->n++] = s;
}

static EVENT *got;

static int take(EVENT *e, void *ud)
{
	(void)ud;
	if (!got)
		got = e;
	else
		ev_free(e);
	return 1;
}

/* DTSTART (a TZID one, a date, or UTC) and the rule */
static const char *const rules[][2] = {
	/* RFC 5545 3.8.5.3's examples */
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=DAILY;COUNT=10" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=DAILY;UNTIL=19971224T000000Z" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=DAILY;INTERVAL=2" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=DAILY;INTERVAL=10;COUNT=5" },
	{ "DTSTART;TZID=America/New_York:19980101T090000", "FREQ=DAILY;UNTIL=20000131T140000Z;BYMONTH=1" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=WEEKLY;COUNT=10" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=WEEKLY;UNTIL=19971224T000000Z" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=WEEKLY;INTERVAL=2;WKST=SU" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=WEEKLY;UNTIL=19971007T000000Z;WKST=SU;BYDAY=TU,TH" },
	{ "DTSTART;TZID=America/New_York:19970901T090000", "FREQ=WEEKLY;INTERVAL=2;UNTIL=19971224T000000Z;WKST=SU;BYDAY=MO,WE,FR" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=8;WKST=SU;BYDAY=TU,TH" },
	{ "DTSTART;TZID=America/New_York:19970905T090000", "FREQ=MONTHLY;COUNT=10;BYDAY=1FR" },
	{ "DTSTART;TZID=America/New_York:19970905T090000", "FREQ=MONTHLY;UNTIL=19971224T000000Z;BYDAY=1FR" },
	{ "DTSTART;TZID=America/New_York:19970907T090000", "FREQ=MONTHLY;INTERVAL=2;COUNT=10;BYDAY=1SU,-1SU" },
	{ "DTSTART;TZID=America/New_York:19970922T090000", "FREQ=MONTHLY;COUNT=6;BYDAY=-2MO" },
	{ "DTSTART;TZID=America/New_York:19970928T090000", "FREQ=MONTHLY;BYMONTHDAY=-3" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=MONTHLY;COUNT=10;BYMONTHDAY=2,15" },
	{ "DTSTART;TZID=America/New_York:19970930T090000", "FREQ=MONTHLY;COUNT=10;BYMONTHDAY=1,-1" },
	{ "DTSTART;TZID=America/New_York:19970910T090000", "FREQ=MONTHLY;INTERVAL=18;COUNT=10;BYMONTHDAY=10,11,12,13,14,15" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=MONTHLY;INTERVAL=2;BYDAY=TU" },
	{ "DTSTART;TZID=America/New_York:19970610T090000", "FREQ=YEARLY;COUNT=10;BYMONTH=6,7" },
	{ "DTSTART;TZID=America/New_York:19970310T090000", "FREQ=YEARLY;INTERVAL=2;COUNT=10;BYMONTH=1,2,3" },
	{ "DTSTART;TZID=America/New_York:19970101T090000", "FREQ=YEARLY;INTERVAL=3;COUNT=10;BYYEARDAY=1,100,200" },
	{ "DTSTART;TZID=America/New_York:19970519T090000", "FREQ=YEARLY;BYDAY=20MO" },
	{ "DTSTART;TZID=America/New_York:19970512T090000", "FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO" },
	{ "DTSTART;TZID=America/New_York:19970313T090000", "FREQ=YEARLY;BYMONTH=3;BYDAY=TH" },
	{ "DTSTART;TZID=America/New_York:19970605T090000", "FREQ=YEARLY;BYDAY=TH;BYMONTH=6,7,8" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13" },
	{ "DTSTART;TZID=America/New_York:19970913T090000", "FREQ=MONTHLY;BYDAY=SA;BYMONTHDAY=7,8,9,10,11,12,13" },
	{ "DTSTART;TZID=America/New_York:19961105T090000", "FREQ=YEARLY;INTERVAL=4;BYMONTH=11;BYDAY=TU;BYMONTHDAY=2,3,4,5,6,7,8" },
	{ "DTSTART;TZID=America/New_York:19970904T090000", "FREQ=MONTHLY;COUNT=3;BYDAY=TU,WE,TH;BYSETPOS=3" },
	{ "DTSTART;TZID=America/New_York:19970929T090000", "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-2" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=HOURLY;INTERVAL=3;UNTIL=19970902T170000Z" },
	{ "DTSTART;TZID=America/New_York:19970902T090000", "FREQ=MINUTELY;INTERVAL=15;COUNT=6" },
	{ "DTSTART;TZID=America/New_York:19970805T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=MO" },
	{ "DTSTART;TZID=America/New_York:19970805T090000", "FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=SU" },
	{ "DTSTART;TZID=America/New_York:20070115T090000", "FREQ=MONTHLY;BYMONTHDAY=15,30;COUNT=5" },
	/* bigcal's, and calendars' everyday ones */
	{ "DTSTART;TZID=Asia/Jerusalem:20260628T091500", "FREQ=DAILY;BYDAY=SU,MO,TU,WE,TH" },
	{ "DTSTART;TZID=Asia/Jerusalem:20251230T100000", "FREQ=WEEKLY;BYDAY=SU" },
	{ "DTSTART;TZID=Asia/Jerusalem:20250107T180000", "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU" },
	{ "DTSTART;TZID=Asia/Jerusalem:20250106T070000", "FREQ=WEEKLY;BYDAY=MO,TH" },
	{ "DTSTART;TZID=Asia/Jerusalem:20250131T160000", "FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1" },
	{ "DTSTART;VALUE=DATE:19600314", "FREQ=YEARLY" },
	{ "DTSTART;VALUE=DATE:19720229", "FREQ=YEARLY" },
	{ "DTSTART:20250301T080000Z", "FREQ=DAILY;INTERVAL=3" },
	{ "DTSTART;TZID=Europe/London:20240331T003000", "FREQ=WEEKLY;BYDAY=SU,WE;WKST=MO" },
	{ "DTSTART;TZID=Asia/Jerusalem:20260101T120000", "FREQ=WEEKLY;BYDAY=FR;BYMONTH=3,4,10" },
};

int main(void)
{
	static SET a, b;
	const TZONE *view = tz_find("Asia/Jerusalem");
	unsigned long digest = 0;
	int i, fails = 0, windows = 0;
	for (i = 0; i < (int)(sizeof(rules) / sizeof(rules[0])); i++) {
		char ics[600];
		int y, m;
		unsigned long sum = 0;
		long total = 0;
		snprintf(ics, sizeof(ics),
			 "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:r%d\r\n%s\r\nDURATION:PT1H\r\nRRULE:%s\r\n"
			 "END:VEVENT\r\nEND:VCALENDAR\r\n", i, rules[i][0], rules[i][1]);
		got = 0;
		ics_parse(ics, (long)strlen(ics), take, 0, CT_NONE, CT_NONE);
		if (!got || !got->rr) {
			printf("rule %d: not read\n", i);
			fails++;
			continue;
		}
		/* windows of a week, a month and a year, across 1960-2040 */
		for (y = 1960; y <= 2040; y += (y < 1995 || y > 2030) ? 7 : 1)
			for (m = 1; m <= 12; m += 5) {
				static const int days[] = { 7, 31, 366 };
				int k;
				for (k = 0; k < 3; k++) {
					CTIME from = ct_make(y, m, 3, 0, 0), to = from + (CTIME)days[k] * MIN_PER_DAY;
					int j;
					a.n = b.n = 0;
					a.from = a.to = CT_NONE;
					ics_expand(got, from, to, view, add, 0, &a);	/* jumping */
					b.from = from;
					b.to = to;
					ics_expand(got, got->start - 2 * MIN_PER_DAY, to, view, add, 0, &b);	/* walked */
					windows++;
					if (a.n != b.n || memcmp(a.s, b.s, a.n * sizeof(CTIME))) {
						if (fails < 20)
							printf("rule %d (%s) %04d-%02d-03 +%d days: %d with the jump, %d walked\n",
							       i, rules[i][1], y, m, days[k], a.n, b.n);
						fails++;
					}
					for (j = 0; j < a.n; j++)
						sum = sum * 31 + (unsigned long)a.s[j];
					total += a.n;
				}
			}
		digest = digest * 131 + sum + (unsigned long)total;
		ev_free(got);
	}
	printf("rrjump: %d rules, %d windows, digest %08lx: %s\n", (int)(sizeof(rules) / sizeof(rules[0])), windows,
	       digest & 0xffffffffUL, fails ? "FAILED" : "all the same");
	return fails ? 1 : 0;
}
