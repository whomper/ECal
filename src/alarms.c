/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alarms.c - the reminders of the next days (see alarms.h)
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "alarms.h"
#include "plat.h"
#include "util.h"

ALARM *alarm_q;
int alarm_n;
CTIME alarm_built;

static ALARM *nq;		/* the one being built */
static int nn, nmax, qmax;
static int dirty = 1, building;

void alarms_changed(void)
{
	dirty = 1;
}

int alarms_pending(void)
{
	return dirty || building;
}

void alarms_begin_build(void)
{
	nn = 0;
	building = 1;
	dirty = 0;
}

static void clean(char *out, int size, const char *s)
{
	char *p;
	str_copy(out, s ? s : "", size);
	for (p = out; *p; p++)
		if (*p == '\t' || *p == '\r' || *p == '\n')
			*p = ' ';
}

void alarms_add(CTIME fire, CTIME start, int allday, const char *title, const char *place)
{
	ALARM *a;
	if (nn >= nmax) {
		int m = nmax ? nmax * 2 : 32;
		ALARM *n = realloc(nq, m * sizeof(ALARM));
		if (!n)
			return;
		nq = n;
		nmax = m;
	}
	a = &nq[nn++];
	a->fire = fire;
	a->start = start;
	a->allday = (unsigned char)(allday != 0);
	clean(a->title, sizeof(a->title), title);
	clean(a->place, sizeof(a->place), place);
}

static int by_fire(const void *x, const void *y)
{
	const ALARM *a = x, *b = y;
	if (a->fire != b->fire)
		return a->fire < b->fire ? -1 : 1;
	return a->start < b->start ? -1 : a->start > b->start;
}

void alarms_end_build(CTIME built)
{
	ALARM *t;
	int m;
	if (nn > 1)
		qsort(nq, nn, sizeof(ALARM), by_fire);
	/* the new one in; the old one's memory is the next build's */
	t = alarm_q;
	alarm_q = nq;
	nq = t;
	m = qmax;
	qmax = nmax;
	nmax = m;
	alarm_n = nn;
	nn = 0;
	alarm_built = built;
	building = 0;
}

void alarms_due(CTIME after, CTIME upto, void (*fn)(const ALARM *a, void *ud), void *ud)
{
	int lo = 0, hi = alarm_n;
	/* the first after "after" */
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (alarm_q[mid].fire <= after)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (; lo < alarm_n && alarm_q[lo].fire <= upto; lo++)
		fn(&alarm_q[lo], ud);
}

/* ---- ALARMS.IDX ---- */

static unsigned long stamp(const char *const *files, int n)
{
	unsigned long h = 5381;
	int i;
	for (i = 0; i < n; i++) {
		h = (h * 33) ^ pf_stamp(files[i]);
		h = (h * 33) ^ (unsigned long)pf_size(files[i]);
	}
	return h;
}

int alarms_save(const char *path, const char *const *files, int nfiles, int on, int h24)
{
	SBUF b;
	int i, ok;
	sb_init(&b);
	sb_printf(&b, "ECAL ALARMS 1\r\nbuilt %ld\r\non %d\r\nh24 %d\r\nstamp %lu\r\n",
		  (long)alarm_built, on, h24, stamp(files, nfiles));
	for (i = 0; i < nfiles; i++)
		sb_printf(&b, "F %s\r\n", files[i]);
	for (i = 0; i < alarm_n; i++) {
		ALARM *a = &alarm_q[i];
		sb_printf(&b, "A %ld\t%ld\t%d\t%s\t%s\r\n", (long)a->fire, (long)a->start, a->allday, a->title, a->place);
	}
	ok = b.s && pf_save(path, b.s, b.len) == 0;
	sb_free(&b);
	return ok;
}

int alarms_load(const char *path, CTIME now, int *on, int *h24)
{
	long len;
	char *t = pf_load(path, &len), *line, *next;
	const char *files[64];
	int nf = 0, ok = 0;
	unsigned long st = 0;
	CTIME built = 0;
	if (!t)
		return 0;
	if (strncmp(t, "ECAL ALARMS 1", 13))
		goto out;
	alarms_begin_build();
	for (line = t; line && *line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		line[strcspn(line, "\r")] = 0;
		if (!strncmp(line, "built ", 6))
			built = atol(line + 6);
		else if (!strncmp(line, "on ", 3))
			*on = atoi(line + 3);
		else if (!strncmp(line, "h24 ", 4))
			*h24 = atoi(line + 4);
		else if (!strncmp(line, "stamp ", 6))
			st = strtoul(line + 6, 0, 10);
		else if (line[0] == 'F' && line[1] == ' ' && nf < 64)
			files[nf++] = line + 2;
		else if (line[0] == 'A' && line[1] == ' ') {
			char *f[5];
			int k;
			f[0] = line + 2;
			for (k = 1; k < 5; k++) {
				f[k] = strchr(f[k - 1], '\t');
				if (!f[k])
					break;
				*f[k]++ = 0;
			}
			if (k == 5)
				alarms_add(atol(f[0]), atol(f[1]), atoi(f[2]), f[3], f[4]);
		}
	}
	/* made from the files as they are now, and reaching past today */
	ok = built && nf && stamp(files, nf) == st && now < built + (ALARM_DAYS - 1) * MIN_PER_DAY;
	if (ok)
		alarms_end_build(built);
	else
		building = 0;
	dirty = 1;		/* built again once the calendars are read */
out:
	free(t);
	return ok;
}
