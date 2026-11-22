/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * corecheck: whole calendars through the core, for a digest that must be
 * the same on the build machine and as ColdFire code under qemu-m68k
 * -cpu cfv4e (make core-cf, the FireBee edition): bigcal.py's calendars
 * (about 1050 events, series, exceptions, alerts, a VTIMEZONE) and the
 * fuzz seeds, each parsed (every event's fields), then every event
 * expanded over two years in five view zones, and each event's start
 * shown in each zone's wall time (ev_wall).
 *
 *   corecheck        prints one line a calendar and a zone, and the sum
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ics.h"
#include "tz.h"
#include "ctime.h"
#include "corecal.h"	/* (made by the Makefile: CORECAL[], CORECAL_N, each text and its name) */

static unsigned long h_str(unsigned long h, const char *s)
{
	if (!s)
		return h * 33 + 1;
	while (*s)
		h = h * 33 + (unsigned char)*s++;
	return h * 33;
}

static unsigned long h_num(unsigned long h, long v)
{
	return h * 1000003UL + (unsigned long)v;
}

#define MAXEV 2000
static EVENT *evs[MAXEV];
static int nev;

static int take(EVENT *e, void *ud)
{
	(void)ud;
	if (nev < MAXEV)
		evs[nev++] = e;
	else
		ev_free(e);
	return 1;
}

static unsigned long occ_sum;
static long occ_n;

static void add(EVENT *e, CTIME s, CTIME en, void *ud)
{
	(void)e;
	(void)ud;
	occ_sum = h_num(h_num(occ_sum, (long)s), (long)en);
	occ_n++;
}

static unsigned long fields(const EVENT *e)
{
	unsigned long h = 5381;
	int i;
	h = h_str(h, e->uid);
	h = h_str(h, e->summary);
	h = h_str(h, e->location);
	h = h_str(h, e->notes);
	h = h_num(h, (long)e->start);
	h = h_num(h, (long)e->end);
	h = h_num(h, (long)e->recurid);
	h = h_num(h, e->allday * 4 + e->floating * 2 + e->status);
	h = h_num(h, e->alarm);
	h = h_num(h, e->seq);
	h = h_num(h, e->rr ? e->rr->freq * 1000 + e->rr->interval : -1);
	h = h_num(h, e->nexdate);
	for (i = 0; i < e->nexdate; i++)
		h = h_num(h, (long)e->exdate[i]);
	h = h_num(h, e->nrdate);
	for (i = 0; i < e->nrdate; i++)
		h = h_num(h, (long)e->rdate[i]);
	return h;
}

int main(void)
{
	static const char *const zones[] = { "Asia/Jerusalem", "Europe/London", "America/New_York",
					     "Australia/Sydney", "UTC" };
	const CTIME from = ct_make(2026, 1, 1, 0, 0), to = ct_make(2028, 1, 1, 0, 0);
	unsigned long total = 0;
	int c, z, i;
	for (c = 0; c < CORECAL_N; c++) {
		unsigned long f = 0;
		nev = 0;
		ics_parse(CORECAL[c].text, CORECAL[c].len, take, 0, CT_NONE, CT_NONE);
		for (i = 0; i < nev; i++)
			f = h_num(f, (long)fields(evs[i]));
		printf("%s: %d events, fields %08lx\n", CORECAL[c].name, nev, f & 0xffffffffUL);
		total = h_num(total, (long)f);
		for (z = 0; z < (int)(sizeof(zones) / sizeof(zones[0])); z++) {
			const TZONE *view = z == 4 ? tz_utc() : tz_find(zones[z]);
			unsigned long w = 0;
			occ_sum = 0;
			occ_n = 0;
			for (i = 0; i < nev; i++) {
				ics_expand(evs[i], from, to, view, add, 0, 0);
				w = h_num(w, (long)ev_wall(evs[i], evs[i]->start, view));
			}
			printf("  %s%s: %ld occurrences, %08lx, wall %08lx\n", zones[z], view ? "" : " (not known)",
			       occ_n, occ_sum & 0xffffffffUL, w & 0xffffffffUL);
			total = h_num(h_num(total, (long)occ_sum), (long)w);
		}
		for (i = 0; i < nev; i++)
			ev_free(evs[i]);
	}
	printf("corecheck: %d calendars, digest %08lx\n", CORECAL_N, total & 0xffffffffUL);
	return 0;
}
