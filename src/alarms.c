/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alarms.c - the reminders of the next days (see alarms.h)
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "alarms.h"
#include "plat.h"
#include "util.h"

ALARM *alarm_q;
int alarm_n;
CTIME alarm_built;

static ALARM *nq;		/* the one being built */
static short *ord;		/* its entries in fire order, kept as they come */
static int nn, nmax, qmax, omax, ord_ok;
static int dirty = 1, building;

void alarms_changed(void)
{
	dirty = 1;
}

int alarms_pending(void)
{
	return dirty || building;
}

int alarms_stale(void)
{
	return dirty;
}

void alarms_begin_build(void)
{
	nn = 0;
	ord_ok = 1;
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

/* a before b: by fire time, then start */
static int before(const ALARM *a, const ALARM *b)
{
	if (a->fire != b->fire)
		return a->fire < b->fire;
	return a->start < b->start;
}

void alarms_add(CTIME fire, CTIME start, int allday, const char *title, const char *place)
{
	ALARM *a;
	int lo, hi, i;
	if (nn >= nmax) {
		int m = nmax ? nmax * 2 : 32;
		ALARM *n = realloc(nq, m * sizeof(ALARM));
		if (!n)
			return;
		nq = n;
		nmax = m;
	}
	if (ord_ok && nn >= omax) {
		short *o = realloc(ord, nmax * sizeof(short));
		if (o) {
			ord = o;
			omax = nmax;
		} else {
			ord_ok = 0;	/* sorted at the end instead */
		}
	}
	a = &nq[nn];
	a->fire = fire;
	a->start = start;
	a->allday = (unsigned char)(allday != 0);
	clean(a->title, sizeof(a->title), title);
	clean(a->place, sizeof(a->place), place);
	if (ord_ok) {
		/* its place in the order now, a step's worth at a time, not
		   all of them sorted at the end: on an ST that took 60 ms for
		   43 (the library's qsort, byte by byte through a pointer) */
		lo = 0;
		hi = nn;
		while (lo < hi) {
			int mid = (lo + hi) / 2;
			if (before(a, &nq[ord[mid]]))
				hi = mid;
			else
				lo = mid + 1;	/* after the equal ones: as they came */
		}
		for (i = nn; i > lo; i--)
			ord[i] = ord[i - 1];
		ord[lo] = (short)nn;
	}
	nn++;
}

/* sorted by an index of them: the library's qsort swaps byte by byte,
   and an ALARM is 124 bytes (tens of milliseconds on a 68000) */
static const ALARM *sort_base;

static int by_fire(const void *x, const void *y)
{
	const ALARM *a = &sort_base[*(const short *)x], *b = &sort_base[*(const short *)y];
	if (a->fire != b->fire)
		return a->fire < b->fire ? -1 : 1;
	return a->start < b->start ? -1 : a->start > b->start;
}

void alarms_end_build(CTIME built)
{
	short *ix = ord_ok ? ord : malloc((nn + 1) * sizeof(short));
	int i;
	if (qmax < nn) {
		ALARM *t = realloc(alarm_q, nn * sizeof(ALARM));
		if (t) {
			alarm_q = t;
			qmax = nn;
		}
	}
	if (ix && qmax >= nn) {
		/* in order into the old queue's memory, one copy each */
		if (!ord_ok) {
			for (i = 0; i < nn; i++)
				ix[i] = (short)i;
			sort_base = nq;
			if (nn > 1)
				qsort(ix, nn, sizeof(short), by_fire);
		}
		for (i = 0; i < nn; i++)
			alarm_q[i] = nq[ix[i]];
		alarm_n = nn;
	} else {
		alarm_n = 0;		/* no memory: no reminders until the next build */
	}
	if (ix != ord)
		free(ix);
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

#define MAXF 40
static char vfiles[MAXF][160];	/* the loaded file's: to check, a step each */
static int vn, vi;
static unsigned long vstamp, vh;
static int vdone;		/* files the last finished check looked at */
int alarms_verified;
static unsigned long vdone_h;	/* and their stamp then */

/* a folder's calendar files (.DAT, .ICS): their names, sizes and times
   summed, so the listing's order doesn't count */
static int stamp_listed(const char *name, long size, unsigned long stamp, void *ud)
{
	unsigned long *h = ud, x = 5381;
	const char *dot = strrchr(name, '.');
	if (!dot || (strcasecmp(dot, ".DAT") && strcasecmp(dot, ".ICS")))
		return 0;
	for (; *name; name++)
		x = (x * 33) ^ (unsigned char)toupper((unsigned char)*name);
	x = (x * 33) ^ stamp;
	*h += (x * 33) ^ (unsigned long)size;
	return 0;
}

static unsigned long last_stamp, v0_stamp;
static long last_size, v0_size;

unsigned long alarms_stamp_info(long *size)
{
	*size = last_size;
	return last_stamp;
}

unsigned long alarms_check_first(long *size)
{
	*size = v0_size;
	return v0_stamp;
}

unsigned long alarms_stamp_add(unsigned long h, const char *file)
{
	long size;
	size_t n = strlen(file);
	if (n && (file[n - 1] == '\\' || file[n - 1] == '/')) {
		unsigned long sum = 0;
		pf_list(file, stamp_listed, &sum);	/* one look for the lot */
		return (h * 33) ^ sum;
	}
	last_stamp = pf_info(file, &size);	/* one directory look for both */
	last_size = size;
	h = (h * 33) ^ last_stamp;
	return (h * 33) ^ (unsigned long)size;
}

static int same_files(const char *const *files, int n)
{
	int i;
	if (n != vdone && n != vn)
		return 0;
	for (i = 0; i < n; i++)
		if (strcmp(files[i], vfiles[i]))
			return 0;
	return 1;
}

int alarms_check_stamp(const char *const *files, int n, unsigned long *h)
{
	if (!vdone || n != vdone || !same_files(files, n))
		return 0;
	*h = vdone_h;
	vdone = 0;	/* once: later it is out of date */
	return 1;
}

void alarms_verify_with(const char *const *files, int n, unsigned long h)
{
	if (vi > 0 || !vn || n != vn || !same_files(files, n))
		return;		/* begun, or none, or of other files */
	vh = h;
	vdone = n;
	vdone_h = h;
	if (vh != vstamp) {
		alarm_n = 0;
		alarm_built = 0;
	} else {
		alarms_verified = 1;
	}
	vn = vi = 0;
}

/* written in steps too: the text eight reminders a step, then the file */
static SBUF sv;
static int sv_i = -1;
static char sv_path[160];

void alarms_save_begin(const char *path, const char *const *files, int nfiles, unsigned long stamp, int on, int h24)
{
	int i;
	sb_free(&sv);
	sb_init(&sv);
	str_copy(sv_path, path, sizeof(sv_path));
	sb_printf(&sv, "ECAL ALARMS 1\r\nbuilt %ld\r\non %d\r\nh24 %d\r\nstamp %lu\r\n",
		  (long)alarm_built, on, h24, stamp);
	for (i = 0; i < nfiles; i++)
		sb_printf(&sv, "F %s\r\n", files[i]);
	sv_i = 0;
}

int alarms_save_step(void)
{
	int k, ok = 0, h;
	if (sv_i < 0)
		return 0;
	for (k = 0; k < 8 && sv_i < alarm_n; k++, sv_i++) {
		ALARM *a = &alarm_q[sv_i];
		sb_printf(&sv, "A %ld\t%ld\t%d\t%s\t%s\r\n", (long)a->fire, (long)a->start, a->allday, a->title, a->place);
	}
	if (sv_i < alarm_n)
		return 1;
	/* straight over the old one: only a copy, and one that is cut short
	   or out of date is noticed (its stamp) and made again */
	if (sv.s && (h = pf_open(sv_path, PF_WRITE)) >= 0) {
		ok = pf_write(h, sv.s, sv.len) == sv.len;
		pf_close(h);
	}
	sb_free(&sv);
	sv_i = -1;
	return ok ? 0 : -1;
}

int alarms_save(const char *path, const char *const *files, int nfiles, unsigned long stamp, int on, int h24)
{
	int r;
	alarms_save_begin(path, files, nfiles, stamp, on, h24);
	while ((r = alarms_save_step()) > 0)
		;
	return r == 0;
}

int alarms_load(const char *path, CTIME now, int *on, int *h24)
{
	long len;
	char *t = pf_load(path, &len), *line, *next;
	int ok = 0;
	unsigned long st = 0;
	CTIME built = 0;
	vn = vi = vdone = 0;
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
		else if (line[0] == 'F' && line[1] == ' ' && vn < MAXF)
			str_copy(vfiles[vn++], line + 2, sizeof(vfiles[0]));
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
	/* reaching past today: in use at once, its files checked after */
	ok = built && vn && now < built + (ALARM_DAYS - 1) * MIN_PER_DAY;
	if (ok) {
		alarms_end_build(built);
		vstamp = st;
		vh = ALARM_STAMP0;
	} else {
		building = 0;
		vn = 0;
	}
	dirty = 1;		/* built again once the calendars are read */
out:
	free(t);
	return ok;
}

int alarms_verify_step(void)
{
	if (vi >= vn)
		return 0;
	vh = alarms_stamp_add(vh, vfiles[vi++]);
	if (vi == 1)
		v0_stamp = alarms_stamp_info(&v0_size);
	if (vi < vn)
		return 1;
	vdone = vn;
	vdone_h = vh;
	if (vh != vstamp) {
		/* a calendar changed since it was made: no reminders from it */
		alarm_n = 0;
		alarm_built = 0;
	} else {
		alarms_verified = 1;
	}
	vn = vi = 0;
	return 0;
}

/* what the queue says, to tell whether ALARMS.IDX needs writing again */
unsigned long alarms_hash(void)
{
	unsigned long h = 5381;
	int i;
	for (i = 0; i < alarm_n; i++) {
		const ALARM *a = &alarm_q[i];
		const char *s;
		h = (h * 33) ^ (unsigned long)a->fire;
		h = (h * 33) ^ (unsigned long)a->start;
		h = (h * 33) ^ a->allday;
		for (s = a->title; *s; s++)
			h = (h * 33) ^ (unsigned char)*s;
		for (s = a->place; *s; s++)
			h = (h * 33) ^ (unsigned char)*s;
	}
	return h;
}
