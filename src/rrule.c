/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * rrule.c - repeating events: RFC 5545 recurrence rules in full, with
 * RDATE, EXDATE, EXRULE, and RFC 7529's RSCALE=HEBREW (what an iPhone
 * writes for "repeat every year on the Hebrew date").
 *
 * The expansion follows RFC 5545 section 3.3.10: the rule's frequency
 * steps through periods (years, months, weeks, days, hours, minutes,
 * seconds) INTERVAL apart; in each period the BYxxx parts expand or limit
 * the set of candidates, BYSETPOS picks from it, and COUNT and UNTIL end
 * the series. DTSTART is always the first occurrence. Everything happens
 * in the wall time of the event's own zone, so a weekly meeting stays at
 * 10:00 when the clocks change.
 *
 * Times are minutes in the rest of ECal; seconds (BYSECOND,
 * FREQ=SECONDLY) take part in the rule and then fold into their minute.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "ics.h"
#include "util.h"

static const char *const wd2[7] = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };

void rr_byday(short v, int *ord, int *wd)
{
	*wd = ((v % 8) + 8) % 8;
	*ord = (v - *wd) / 8;
}

static int wday_code(const char *s)
{
	int i;
	for (i = 0; i < 7; i++)
		if (toupper((unsigned char)s[0]) == wd2[i][0] && toupper((unsigned char)s[1]) == wd2[i][1])
			return i;
	return -1;
}

/* ---------------- reading and writing rules ---------------- */

static void set_bit(unsigned long *m, int n)
{
	m[n / 32] |= 1UL << (n % 32);
}

static int has_bit(const unsigned long *m, int n)
{
	return (m[n / 32] >> (n % 32)) & 1;
}

static int any_bits(const unsigned long *m, int words)
{
	int i;
	for (i = 0; i < words; i++)
		if (m[i])
			return 1;
	return 0;
}

/* the numbers of a comma list, each within [lo, hi] and not 0 unless allowed */
static int num_list(const char *p, long lo, long hi, int zero_ok, long *out, int max)
{
	int n = 0;
	while (*p && n < max) {
		char *end;
		long v = strtol(p, &end, 10);
		if (end != p && v >= lo && v <= hi && (v || zero_ok))
			out[n++] = v;
		p = end;
		while (*p && *p != ',')
			p++;
		if (*p == ',')
			p++;
	}
	return n;
}

int rrule_parse(const char *s, RRULE *r)
{
	char key[16], val[300];
	long nums[RR_MAXBY * 2];
	int i, n, bad = 0;
	memset(r, 0, sizeof(*r));
	r->interval = 1;
	r->until = CT_NONE;
	r->wkst = 1;
	while (*s) {
		int k = 0, v = 0;
		while (*s == ';' || *s == ' ')
			s++;
		if (!*s)
			break;
		while (*s && *s != '=' && *s != ';' && k < 15)
			key[k++] = (char)toupper((unsigned char)*s++);
		key[k] = 0;
		if (*s == '=')
			s++;
		while (*s && *s != ';' && v < 299)
			val[v++] = *s++;
		val[v] = 0;
		while (*s && *s != ';')
			s++;
		if (!strcmp(key, "FREQ")) {
			static const char *const fq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY",
							  "HOURLY", "MINUTELY", "SECONDLY" };
			for (i = 1; i < 8; i++)
				if (!strcasecmp(val, fq[i]))
					r->freq = (unsigned char)i;
		} else if (!strcmp(key, "INTERVAL")) {
			long iv = atol(val);
			r->interval = (short)(iv < 1 ? 1 : iv > 32000 ? 32000 : iv);
		} else if (!strcmp(key, "COUNT")) {
			r->count = atol(val);
			if (r->count < 0)
				r->count = 0;
		} else if (!strcmp(key, "UNTIL")) {
			r->until_kind = (unsigned char)ct_parse(val, &r->until);
			if (!r->until_kind)
				r->until = CT_NONE;
		} else if (!strcmp(key, "WKST")) {
			int w = wday_code(val);
			if (w >= 0)
				r->wkst = (unsigned char)w;
		} else if (!strcmp(key, "RSCALE")) {
			if (!strcasecmp(val, "HEBREW"))
				r->rscale = RS_HEBREW;
			else if (strcasecmp(val, "GREGORIAN"))
				bad = 1;	/* Chinese, Islamic...: not known here */
		} else if (!strcmp(key, "SKIP")) {
			r->skip = (unsigned char)(!strcasecmp(val, "BACKWARD") ? SKIP_BACKWARD :
						  !strcasecmp(val, "FORWARD") ? SKIP_FORWARD : SKIP_OMIT);
		} else if (!strcmp(key, "BYDAY")) {
			char *p = val;
			while (*p && r->nbyday < RR_MAXBY) {
				int sign = 1, ord = 0, w;
				if (*p == '+' || *p == '-')
					sign = *p++ == '-' ? -1 : 1;
				while (*p >= '0' && *p <= '9') {
					if (ord < 1000)		/* "2151222225MO": too big, not an overflow */
						ord = ord * 10 + (*p - '0');
					p++;
				}
				w = wday_code(p);
				if (w >= 0 && ord <= 53)
					r->byday[r->nbyday++] = (short)(sign * ord * 8 + w);
				while (*p && *p != ',')
					p++;
				if (*p == ',')
					p++;
			}
		} else if (!strcmp(key, "BYMONTHDAY")) {
			n = num_list(val, -31, 31, 0, nums, RR_MAXBY);
			for (i = 0; i < n && r->nbymd < RR_MAXBY; i++)	/* the key may come twice */
				r->bymd[r->nbymd++] = (signed char)nums[i];
		} else if (!strcmp(key, "BYYEARDAY")) {
			n = num_list(val, -366, 366, 0, nums, RR_MAXBY);
			for (i = 0; i < n && r->nbyyd < RR_MAXBY; i++)	/* the key may come twice */
				r->byyd[r->nbyyd++] = (short)nums[i];
		} else if (!strcmp(key, "BYWEEKNO")) {
			n = num_list(val, -53, 53, 0, nums, RR_MAXBY);
			for (i = 0; i < n && r->nbywn < RR_MAXBY; i++)	/* the key may come twice */
				r->bywn[r->nbywn++] = (signed char)nums[i];
		} else if (!strcmp(key, "BYSETPOS")) {
			n = num_list(val, -366, 366, 0, nums, RR_MAXBY);
			for (i = 0; i < n && r->nbysetpos < RR_MAXBY; i++)	/* the key may come twice */
				r->bysetpos[r->nbysetpos++] = (short)nums[i];
		} else if (!strcmp(key, "BYMONTH")) {
			char *p = val;
			while (*p) {
				int m = atoi(p);
				while (*p >= '0' && *p <= '9')
					p++;
				if (m >= 1 && m <= 13) {
					if (*p == 'L' || *p == 'l')
						r->bymonth_leap |= (unsigned short)(1 << (m - 1));
					else
						r->bymonth |= (unsigned short)(1 << (m - 1));
				}
				while (*p && *p != ',')
					p++;
				if (*p == ',')
					p++;
			}
		} else if (!strcmp(key, "BYHOUR")) {
			n = num_list(val, 0, 23, 1, nums, 24);
			for (i = 0; i < n; i++)
				r->byhour |= 1UL << nums[i];
		} else if (!strcmp(key, "BYMINUTE")) {
			n = num_list(val, 0, 59, 1, nums, 60);
			for (i = 0; i < n; i++)
				set_bit(r->byminute, (int)nums[i]);
		} else if (!strcmp(key, "BYSECOND")) {
			n = num_list(val, 0, 60, 1, nums, 61);
			for (i = 0; i < n; i++)
				set_bit(r->bysecond, (int)nums[i]);
		}
	}
	return r->freq != F_NONE && !bad;
}

static void add_list(SBUF *b, const char *name, const long *v, int n)
{
	int i;
	if (!n)
		return;
	sb_printf(b, ";%s=", name);
	for (i = 0; i < n; i++)
		sb_printf(b, "%s%ld", i ? "," : "", v[i]);
}

static void add_mask(SBUF *b, const char *name, const unsigned long *m, int bits)
{
	long v[61];
	int i, n = 0;
	for (i = 0; i < bits; i++)
		if (has_bit(m, i))
			v[n++] = i;
	add_list(b, name, v, n);
}

void rrule_format(const RRULE *r, const TZONE *tz, int allday, char *out, int size)
{
	static const char *const fq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY", "HOURLY", "MINUTELY", "SECONDLY" };
	SBUF b;
	long v[RR_MAXBY];
	int i;
	sb_init(&b);
	if (r->rscale == RS_HEBREW)
		sb_adds(&b, "RSCALE=HEBREW;");
	sb_printf(&b, "FREQ=%s", fq[r->freq < 8 ? r->freq : 0]);
	if (r->interval > 1)
		sb_printf(&b, ";INTERVAL=%d", r->interval);
	if (r->count)
		sb_printf(&b, ";COUNT=%ld", r->count);
	else if (r->until != CT_NONE) {
		char t[24];
		if (allday)
			ct_format_ics(r->until, 1, t, sizeof(t));
		else if (tz)
			ct_format_ics(tz_to_utc(tz, r->until), 3, t, sizeof(t));
		else
			ct_format_ics(r->until, 2, t, sizeof(t));
		sb_printf(&b, ";UNTIL=%s", t);
	}
	if (r->bymonth || r->bymonth_leap) {
		int first = 1;
		sb_adds(&b, ";BYMONTH=");
		for (i = 0; i < 13; i++) {
			if (r->bymonth & (1 << i)) {
				sb_printf(&b, "%s%d", first ? "" : ",", i + 1);
				first = 0;
			}
			if (r->bymonth_leap & (1 << i)) {
				sb_printf(&b, "%s%dL", first ? "" : ",", i + 1);
				first = 0;
			}
		}
	}
	for (i = 0; i < r->nbywn; i++)
		v[i] = r->bywn[i];
	add_list(&b, "BYWEEKNO", v, r->nbywn);
	for (i = 0; i < r->nbyyd; i++)
		v[i] = r->byyd[i];
	add_list(&b, "BYYEARDAY", v, r->nbyyd);
	for (i = 0; i < r->nbymd; i++)
		v[i] = r->bymd[i];
	add_list(&b, "BYMONTHDAY", v, r->nbymd);
	if (r->nbyday) {
		sb_adds(&b, ";BYDAY=");
		for (i = 0; i < r->nbyday; i++) {
			int ord, w;
			rr_byday(r->byday[i], &ord, &w);
			if (ord)
				sb_printf(&b, "%s%d%s", i ? "," : "", ord, wd2[w]);
			else
				sb_printf(&b, "%s%s", i ? "," : "", wd2[w]);
		}
	}
	add_mask(&b, "BYHOUR", &r->byhour, 24);
	add_mask(&b, "BYMINUTE", r->byminute, 60);
	add_mask(&b, "BYSECOND", r->bysecond, 61);
	for (i = 0; i < r->nbysetpos; i++)
		v[i] = r->bysetpos[i];
	add_list(&b, "BYSETPOS", v, r->nbysetpos);
	if (r->wkst != 1)
		sb_printf(&b, ";WKST=%s", wd2[r->wkst % 7]);
	if (r->rscale == RS_HEBREW && r->skip)
		sb_printf(&b, ";SKIP=%s", r->skip == SKIP_BACKWARD ? "BACKWARD" : "FORWARD");
	str_copy(out, b.s ? b.s : "", size);
	sb_free(&b);
}

static const char *ordinal(int n, char *buf)
{
	static const char *const names[] = { "", "first", "second", "third", "fourth", "fifth" };
	if (n == -1)
		return "last";
	if (n == -2)
		return "second to last";
	if (n > 0 && n <= 5)
		return names[n];
	if (n < 0) {
		sprintf(buf, "%d. from last", -n);
		return buf;
	}
	sprintf(buf, "%d.", n);
	return buf;
}

void rrule_describe(const RRULE *r, char *out, int size)
{
	static const char *const unit[] = { "", "day", "week", "month", "year", "hour", "minute", "second" };
	/* RFC 7529's numbering of the Hebrew months */
	static const char *const hmon[13] = { "Tishri", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar",
					      "Nisan", "Iyar", "Sivan", "Tammuz", "Av", "Elul", "" };
	SBUF b;
	char tmp[24];
	int i;
	sb_init(&b);
	if (r->interval > 1)
		sb_printf(&b, "Every %d %ss", r->interval, unit[r->freq < 8 ? r->freq : 0]);
	else
		sb_printf(&b, "Every %s", unit[r->freq < 8 ? r->freq : 0]);
	if (r->rscale == RS_HEBREW)
		sb_adds(&b, " of the Hebrew calendar");
	if (r->nbyday) {
		sb_adds(&b, " on ");
		if (r->nbyday == 5 && !r->nbysetpos) {
			int mask = 0;
			for (i = 0; i < 5; i++)
				mask |= 1 << ((r->byday[i] % 8 + 8) % 8);
			if (mask == 0x3e)
				sb_adds(&b, "weekdays (Mon-Fri)");
			else if (mask == 0x1f)
				sb_adds(&b, "weekdays (Sun-Thu)");
			else
				mask = 0;
			if (mask)
				goto days_done;
		}
		for (i = 0; i < r->nbyday; i++) {
			int ord, w;
			rr_byday(r->byday[i], &ord, &w);
			if (i)
				sb_adds(&b, i == r->nbyday - 1 ? " and " : ", ");
			if (ord)
				sb_printf(&b, "the %s ", ordinal(ord, tmp));
			sb_adds(&b, wday_names[w]);
		}
days_done:;
	}
	if (r->nbymd) {
		sb_adds(&b, r->nbyday ? ", if on day " : " on day ");
		for (i = 0; i < r->nbymd; i++) {
			if (r->bymd[i] < 0)
				sb_printf(&b, "%s%s", i ? ", " : "", r->bymd[i] == -1 ? "last" : ordinal(r->bymd[i], tmp));
			else
				sb_printf(&b, "%s%d", i ? ", " : "", r->bymd[i]);
		}
	}
	if (r->nbyyd) {
		sb_adds(&b, " on day ");
		for (i = 0; i < r->nbyyd; i++)
			sb_printf(&b, "%s%d", i ? ", " : "", r->byyd[i]);
		sb_adds(&b, " of the year");
	}
	if (r->nbywn) {
		sb_adds(&b, " in week ");
		for (i = 0; i < r->nbywn; i++)
			sb_printf(&b, "%s%d", i ? ", " : "", r->bywn[i]);
	}
	if (r->bymonth || r->bymonth_leap) {
		int first = 1;
		sb_adds(&b, " in ");
		for (i = 0; i < 13; i++) {
			if (r->bymonth & (1 << i)) {
				sb_printf(&b, "%s%s", first ? "" : ", ",
					  r->rscale == RS_HEBREW ? hmon[i] : month_names[i % 12]);
				first = 0;
			}
			if (r->bymonth_leap & (1 << i)) {
				sb_printf(&b, "%sAdar I", first ? "" : ", ");
				first = 0;
			}
		}
	}
	if (r->byhour) {
		int first = 1;
		sb_adds(&b, " at ");
		for (i = 0; i < 24; i++)
			if (r->byhour & (1UL << i)) {
				sb_printf(&b, "%s%d", first ? "" : ",", i);
				first = 0;
			}
		sb_adds(&b, " h");
	}
	if (any_bits(r->byminute, 2)) {
		int first = 1;
		sb_adds(&b, ", minute ");
		for (i = 0; i < 60; i++)
			if (has_bit(r->byminute, i)) {
				sb_printf(&b, "%s%d", first ? "" : ",", i);
				first = 0;
			}
	}
	if (r->nbysetpos) {
		sb_adds(&b, ", only the ");
		for (i = 0; i < r->nbysetpos; i++)
			sb_printf(&b, "%s%s", i ? " and " : "", ordinal(r->bysetpos[i], tmp));
	}
	if (r->count)
		sb_printf(&b, ", %ld time%s", r->count, r->count == 1 ? "" : "s");
	else if (r->until != CT_NONE) {
		DT d;
		ct_split(r->until, &d);
		sb_printf(&b, ", until %d %s %d", d.day, month_short[d.mon - 1], d.year);
	}
	str_copy(out, b.s ? b.s : "", size);
	sb_free(&b);
}

/* ---------------- the expansion ---------------- */

typedef int (*WALK_FN)(CTIME c, void *ud);	/* 1: stop */

/* the matching of one day against the day-level BYxxx parts */
static int f_month(const RRULE *r, int m)
{
	return !r->bymonth || ((r->bymonth >> (m - 1)) & 1);
}

static int f_yday(const RRULE *r, int yd, int ylen)
{
	int i;
	if (!r->nbyyd)
		return 1;
	for (i = 0; i < r->nbyyd; i++)
		if (r->byyd[i] == yd || ylen + r->byyd[i] + 1 == yd)
			return 1;
	return 0;
}

static int f_mday(const RRULE *r, int d, int dim)
{
	int i;
	if (!r->nbymd)
		return 1;
	for (i = 0; i < r->nbymd; i++)
		if (r->bymd[i] == d || dim + r->bymd[i] + 1 == d)
			return 1;
	return 0;
}

/* ctx: 0 plain weekdays, 1 ordinals within the month, 2 within the year */
static int f_wday(const RRULE *r, int wd, int ctx, int d, int dim, int yd, int ylen)
{
	int i;
	if (!r->nbyday)
		return 1;
	for (i = 0; i < r->nbyday; i++) {
		int ord, w;
		rr_byday(r->byday[i], &ord, &w);
		if (w != wd)
			continue;
		if (!ord || !ctx)
			return 1;
		if (ctx == 1 && (ord > 0 ? (d - 1) / 7 + 1 == ord : -((dim - d) / 7 + 1) == ord))
			return 1;
		if (ctx == 2 && (ord > 0 ? (yd - 1) / 7 + 1 == ord : -((ylen - yd) / 7 + 1) == ord))
			return 1;
	}
	return 0;
}

/* all the day-level parts, for frequencies of a day and shorter */
static int day_ok(const RRULE *r, long day)
{
	int y, m, d, ylen;
	long jan1;
	civil_from_days(day, &y, &m, &d);
	if (!f_month(r, m))
		return 0;
	jan1 = days_from_civil(y, 1, 1);
	ylen = is_leap(y) ? 366 : 365;
	return f_yday(r, (int)(day - jan1) + 1, ylen) && f_mday(r, d, days_in_month(y, m)) &&
	       f_wday(r, wday_of(day), 0, d, 0, 0, 0);
}

static long week1(int y, int wkst)
{
	long jan1 = days_from_civil(y, 1, 1);
	int off = (wday_of(jan1) - wkst + 7) % 7;
	long start = jan1 - off;
	if (7 - off < 4)		/* week 1 has at least four days of the year */
		start += 7;
	return start;
}

#define DMAX 400
#define CMAX 1500

static int add_day(long *days, int n, long d)
{
	if (n < DMAX)
		days[n++] = d;
	return n;
}

static int sort_unique(long *v, int n)
{
	int i, j, k;
	for (i = 1; i < n; i++) {
		long t = v[i];
		for (j = i; j > 0 && v[j - 1] > t; j--)
			v[j] = v[j - 1];
		v[j] = t;
	}
	for (i = k = 0; i < n; i++)
		if (!k || v[k - 1] != v[i])
			v[k++] = v[i];
	return k;
}

/* the candidate days of period p of a yearly rule */
static int yearly_days(const RRULE *r, int y, int m0, int d0, int wd0, long *days)
{
	int n = 0, m, ctx, ylen = is_leap(y) ? 366 : 365, i;
	long jan1 = days_from_civil(y, 1, 1), d, k;
	int dayparts = r->nbywn || r->nbyyd || r->nbymd || r->nbyday;
	long cand[DMAX];
	int nc = 0;
	if (!dayparts) {
		/* the start's day of the month, in its month or in BYMONTH's */
		for (m = 1; m <= 12; m++)
			if (r->bymonth ? f_month(r, m) : m == m0)
				if (d0 <= days_in_month(y, m))
					n = add_day(days, n, days_from_civil(y, m, d0));
		return n;
	}
	/* the universe of days, before the limits */
	if (r->nbywn) {
		long w1 = week1(y, r->wkst);
		int nweeks = (int)((week1(y + 1, r->wkst) - w1) / 7);
		for (i = 0; i < r->nbywn; i++) {
			int w = r->bywn[i] > 0 ? r->bywn[i] : nweeks + r->bywn[i] + 1;
			if (w < 1 || w > nweeks)
				continue;
			for (k = 0; k < 7; k++)
				nc = add_day(cand, nc, w1 + (w - 1) * 7 + k);
		}
	} else if (r->nbyyd) {
		for (i = 0; i < r->nbyyd; i++) {
			int yd = r->byyd[i] > 0 ? r->byyd[i] : ylen + r->byyd[i] + 1;
			if (yd >= 1 && yd <= ylen)
				nc = add_day(cand, nc, jan1 + yd - 1);
		}
	} else if (r->nbymd) {
		for (m = 1; m <= 12; m++) {
			int dim = days_in_month(y, m);
			if (!f_month(r, m))
				continue;
			for (i = 0; i < r->nbymd; i++) {
				int md = r->bymd[i] > 0 ? r->bymd[i] : dim + r->bymd[i] + 1;
				if (md >= 1 && md <= dim)
					nc = add_day(cand, nc, days_from_civil(y, m, md));
			}
		}
	} else {
		for (m = 1; m <= 12; m++)
			if (f_month(r, m))
				for (d = days_from_civil(y, m, 1), k = days_in_month(y, m); k > 0; d++, k--)
					nc = add_day(cand, nc, d);
	}
	nc = sort_unique(cand, nc);
	/* ordinals in BYDAY count within the month when BYMONTH is given,
	   else within the year; with BYWEEKNO they don't apply */
	ctx = r->nbywn ? 0 : r->bymonth ? 1 : 2;
	for (i = 0; i < nc; i++) {
		int yy, mm, dd, wd = wday_of(cand[i]);
		civil_from_days(cand[i], &yy, &mm, &dd);
		if (!r->nbywn && yy != y)
			continue;
		if (!f_month(r, mm) || !f_yday(r, (int)(cand[i] - jan1) + 1, ylen) ||
		    !f_mday(r, dd, days_in_month(yy, mm)) ||
		    !f_wday(r, wd, ctx, dd, days_in_month(yy, mm), (int)(cand[i] - jan1) + 1, ylen))
			continue;
		/* BYWEEKNO alone: the start's weekday in those weeks */
		if (r->nbywn && !r->nbyday && !r->nbyyd && !r->nbymd && wd != wd0)
			continue;
		n = add_day(days, n, cand[i]);
	}
	return n;
}

static int monthly_days(const RRULE *r, int y, int m, int d0, long *days)
{
	int n = 0, dim = days_in_month(y, m), d;
	long first = days_from_civil(y, m, 1), jan1 = days_from_civil(y, 1, 1);
	int ylen = is_leap(y) ? 366 : 365;
	if (!f_month(r, m))
		return 0;
	if (!r->nbymd && !r->nbyday && !r->nbyyd) {
		if (d0 <= dim)
			days[n++] = days_from_civil(y, m, d0);
		return n;
	}
	for (d = 1; d <= dim; d++) {
		long day = first + d - 1;
		if (f_mday(r, d, dim) && f_yday(r, (int)(day - jan1) + 1, ylen) &&
		    f_wday(r, wday_of(day), 1, d, dim, 0, 0))
			days[n++] = day;
	}
	return n;
}

/* the times of day a day-based rule gives, in seconds */
static int day_times(const RRULE *r, long sod0, long *t)
{
	int h, mi, s, n = 0;
	int h0 = (int)(sod0 / 3600), m0 = (int)(sod0 / 60 % 60), s0 = (int)(sod0 % 60);
	for (h = 0; h < 24; h++) {
		if (r->byhour ? !((r->byhour >> h) & 1) : h != h0)
			continue;
		for (mi = 0; mi < 60; mi++) {
			if (any_bits(r->byminute, 2) ? !has_bit(r->byminute, mi) : mi != m0)
				continue;
			for (s = 0; s <= 60; s++) {
				if (any_bits(r->bysecond, 2) ? !has_bit(r->bysecond, s) : s != s0)
					continue;
				if (n < CMAX)
					t[n++] = h * 3600L + mi * 60L + s;
			}
		}
	}
	return n;
}

/* BYSETPOS: keep the given positions of the period's sorted set */
static int setpos(const RRULE *r, long *c, int n)
{
	long keep[RR_MAXBY];
	int k = 0, i;
	if (!r->nbysetpos || !n)
		return n;
	for (i = 0; i < r->nbysetpos; i++) {
		int at = r->bysetpos[i] > 0 ? r->bysetpos[i] - 1 : n + r->bysetpos[i];
		if (at >= 0 && at < n)
			keep[k++] = c[at];
	}
	k = sort_unique(keep, k);
	memcpy(c, keep, k * sizeof(long));
	return k;
}

typedef struct {
	const RRULE *r;
	CTIME w0;
	WALK_FN fn;
	void *ud;
	long seen;
	int started, stopped;
	CTIME last;		/* the last minute handed out (seconds fold into minutes) */
} WALK;

/* one occurrence at day + seconds; 1 when the walk is over */
static int occur(WALK *w, long day, long sec)
{
	CTIME c = day * MIN_PER_DAY + sec / 60;
	const RRULE *r = w->r;
	if (c < w->w0)
		return 0;
	if (!w->started) {
		w->started = 1;
		if (c != w->w0) {	/* DTSTART is always the first, even off the rule */
			w->seen++;
			w->last = w->w0;
			if (w->fn(w->w0, w->ud))
				return w->stopped = 1;
		}
	}
	if (r->until != CT_NONE && c > r->until)
		return w->stopped = 1;
	if (r->count && w->seen >= r->count)
		return w->stopped = 1;
	w->seen++;
	if (c == w->last)
		return 0;
	w->last = c;
	if (w->fn(c, w->ud))
		return w->stopped = 1;
	return 0;
}

/* the Hebrew calendar (RSCALE=HEBREW): RFC 7529 numbers the months from
   Tishri (1) to Elul (12), with "5L" for Adar I; Calendrical Calculations
   (ctime.c) from Nisan (1), Adar 12, Adar II 13 */
static int heb_cc_month(int rfc, int leapmonth, long hy, int *exists)
{
	int leap = hebrew_leap(hy);
	*exists = 1;
	if (leapmonth) {		/* Adar I */
		if (!leap)
			*exists = 0;
		return 12;
	}
	if (rfc <= 5)
		return rfc + 6;
	if (rfc == 6)
		return leap ? 13 : 12;	/* Adar, or Adar II in a leap year */
	return rfc - 6;
}

static int heb_rfc_month(int cc, long hy, int *leapmonth)
{
	*leapmonth = 0;
	if (cc == 13)			/* Adar II */
		return 6;
	if (cc == 12) {			/* Adar, or Adar I in a leap year */
		if (hebrew_leap(hy)) {
			*leapmonth = 1;
			return 5;
		}
		return 6;
	}
	if (cc >= 7)			/* Tishri .. Shevat */
		return cc - 6;
	return cc + 6;			/* Nisan .. Elul */
}

/* the day for Hebrew year hy, RFC month (and leap flag), day; with SKIP;
   -1 if it is left out */
static long heb_day(const RRULE *r, long hy, int rfcm, int leapm, int day)
{
	int exists, cc = heb_cc_month(rfcm, leapm, hy, &exists), len;
	if (!exists) {
		if (r->skip == SKIP_OMIT)
			return -1;
		if (r->skip == SKIP_BACKWARD)
			return days_from_hebrew(hy, 11, hebrew_month_days(hy, 11));	/* the end of Shevat */
		return days_from_hebrew(hy, 12, 1);					/* Adar */
	}
	len = hebrew_month_days(hy, cc);
	if (day > len) {
		if (r->skip == SKIP_OMIT)
			return -1;
		if (r->skip == SKIP_BACKWARD)
			return days_from_hebrew(hy, cc, len);
		return days_from_hebrew(hy, cc, len) + 1;
	}
	return days_from_hebrew(hy, cc, day);
}

static void walk_hebrew(WALK *w, long d0, long sod0, CTIME stop)
{
	const RRULE *r = w->r;
	HDATE h0;
	long p, iter;
	int rfcm0, leap0;
	hebrew_from_days(d0, &h0);
	rfcm0 = heb_rfc_month(h0.mon, h0.year, &leap0);
	for (p = 0, iter = 0; iter < 5000 && !w->stopped; p++, iter++) {
		long cand[32];
		int n = 0, i;
		if (r->freq == F_YEARLY) {
			long hy = h0.year + p * r->interval;
			int m;
			int days_list[RR_MAXBY], nd = 0;
			if (r->nbymd)
				for (i = 0; i < r->nbymd; i++)
					days_list[nd++] = r->bymd[i];
			else
				days_list[nd++] = h0.day;
			for (m = 1; m <= 12; m++) {
				int pick = r->bymonth || r->bymonth_leap ? (r->bymonth >> (m - 1)) & 1 : m == rfcm0 && !leap0;
				int pickl = r->bymonth || r->bymonth_leap ? (r->bymonth_leap >> (m - 1)) & 1 : m == rfcm0 && leap0;
				int k;
				for (k = 0; k < 2; k++) {
					if (!(k ? pickl : pick))
						continue;
					for (i = 0; i < nd && n < 32; i++) {
						long d;
						int dd = days_list[i];
						if (dd < 0) {
							int ex, cc = heb_cc_month(m, k, hy, &ex);
							if (!ex)
								continue;
							dd = hebrew_month_days(hy, cc) + dd + 1;
						}
						d = heb_day(r, hy, m, k, dd);
						if (d >= 0)
							cand[n++] = d;
					}
				}
			}
			if (stop != CT_NONE && days_from_hebrew(hy, 7, 1) * MIN_PER_DAY > stop + 400L * MIN_PER_DAY)
				return;
		} else if (r->freq == F_MONTHLY) {
			/* months in order: step through them from the start's */
			long hy = h0.year;
			int cc = h0.mon;
			long steps = p * r->interval, k;
			for (k = 0; k < steps; k++) {
				int last = hebrew_leap(hy) ? 13 : 12;
				if (cc == 6) {
					cc = 7;
					hy++;
				} else if (cc == last) {
					cc = 1;
				} else {
					cc++;
				}
			}
			{
				int len = hebrew_month_days(hy, cc), dd = r->nbymd ? r->bymd[0] : h0.day;
				if (dd < 0)
					dd = len + dd + 1;
				if (dd >= 1 && dd <= len)
					cand[n++] = days_from_hebrew(hy, cc, dd);
				else if (r->skip == SKIP_BACKWARD)
					cand[n++] = days_from_hebrew(hy, cc, len);
				else if (r->skip == SKIP_FORWARD)
					cand[n++] = days_from_hebrew(hy, cc, len) + 1;
				if (stop != CT_NONE && days_from_hebrew(hy, cc, 1) * MIN_PER_DAY > stop + 40L * MIN_PER_DAY)
					return;
			}
		} else {
			return;
		}
		n = sort_unique(cand, n);
		n = setpos(r, cand, n);
		for (i = 0; i < n; i++)
			if (occur(w, cand[i], sod0))
				return;
	}
}

/* walk the occurrences of rule r from w0 (wall time) in order, calling fn
   for each; hint: a wall time near which the caller's interest starts
   (CT_NONE: from the beginning); stop: none needed after this time */
static void rr_walk(const RRULE *r, CTIME w0, CTIME hint, CTIME stop, WALK_FN fn, void *ud)
{
	WALK w;
	long d0 = ct_days(w0), sod0 = (w0 - d0 * MIN_PER_DAY) * 60, p, pstart = 0, iter, maxiter;
	long ws0, times[CMAX], days[DMAX], cand[CMAX];
	int y0, m0, dd0, wd0 = wday_of(d0), ntimes;
	memset(&w, 0, sizeof(w));
	w.r = r;
	w.w0 = w0;
	w.fn = fn;
	w.ud = ud;
	w.last = CT_NONE;
	civil_from_days(d0, &y0, &m0, &dd0);
	ws0 = d0 - (wd0 - r->wkst + 7) % 7;
	if (r->rscale == RS_HEBREW) {
		walk_hebrew(&w, d0, sod0, stop);
		if (!w.started)
			occur(&w, d0, sod0);
		return;
	}
	ntimes = r->freq <= F_YEARLY ? day_times(r, sod0, times) : 0;
	/* without a COUNT, jump close to where the caller looks */
	if (!r->count && hint != CT_NONE) {
		long td = ct_days(hint) - 2 - d0, iv = r->interval;
		if (td > 0) {
			switch (r->freq) {
			case F_DAILY: pstart = td / iv; break;
			case F_WEEKLY: pstart = (td + d0 - ws0) / (7 * iv); break;
			case F_MONTHLY:
			case F_YEARLY: {
				/* the months (years) between, counted exactly: by 31-day
				   months a rule from a year ago began months early, each
				   with all its days worked out for nothing */
				int ty, tm, tdd;
				civil_from_days(d0 + td, &ty, &tm, &tdd);
				pstart = r->freq == F_MONTHLY ? ((long)(ty - y0) * 12 + (tm - m0)) / iv : (long)(ty - y0) / iv;
				break;
			}
			case F_HOURLY: pstart = (td * 24 - sod0 / 3600) / iv; break;
			case F_MINUTELY: pstart = td < 4000 ? (td * 1440 - sod0 / 60) / iv : 0; break;
			case F_SECONDLY: pstart = td < 20 ? (td * 86400 - sod0) / iv : 0; break;
			}
			if (pstart > 0) {
				pstart--;
				w.started = 1;
			} else {
				pstart = 0;
			}
		}
	}
	/* a COUNT can't jump ahead: daily for decades must still get there */
	maxiter = r->freq <= F_YEARLY ? 40000 : 200000;
	for (p = pstart, iter = 0; iter < maxiter && !w.stopped; p++, iter++) {
		int nd = 0, nc = 0, i, j;
		long base;		/* first day of the period */
		if (r->freq <= F_YEARLY) {
			switch (r->freq) {
			case F_DAILY:
				base = d0 + p * r->interval;
				if (day_ok(r, base))
					days[nd++] = base;
				break;
			case F_WEEKLY:
				base = ws0 + p * 7 * r->interval;
				for (i = 0; i < 7; i++) {
					long d = base + i;
					int yy, mm, ddd;
					if (r->nbyday ? !f_wday(r, wday_of(d), 0, 0, 0, 0, 0) : wday_of(d) != wd0)
						continue;
					civil_from_days(d, &yy, &mm, &ddd);
					if (!f_month(r, mm))
						continue;
					days[nd++] = d;
				}
				break;
			case F_MONTHLY: {
				long mi = (long)y0 * 12 + (m0 - 1) + p * r->interval;
				int y = (int)(mi / 12), m = (int)(mi % 12) + 1;
				base = days_from_civil(y, m, 1);
				nd = monthly_days(r, y, m, dd0, days);
				break;
			}
			default: {
				int y = y0 + (int)(p * r->interval);
				base = days_from_civil(y, 1, 1);
				nd = yearly_days(r, y, m0, dd0, wd0, days);
				break;
			}
			}
			nd = sort_unique(days, nd);
			if (r->nbysetpos) {
				for (i = 0; i < nd; i++)
					for (j = 0; j < ntimes && nc < CMAX; j++)
						cand[nc++] = days[i] * 86400L + times[j];	/* fits for 68 years around 1970 */
				if (nd && (days[0] > 24000 || days[0] < -24000)) {
					/* far from 1970: positions on days only */
					nc = setpos(r, days, nd);
					for (i = 0; i < nc; i++)
						for (j = 0; j < ntimes; j++)
							if (occur(&w, days[i], times[j]))
								return;
				} else {
					nc = setpos(r, cand, nc);
					for (i = 0; i < nc; i++) {
						long d = cand[i] / 86400L, s = cand[i] % 86400L;
						if (s < 0) {
							s += 86400L;
							d--;
						}
						if (occur(&w, d, s))
							return;
					}
				}
			} else {
				for (i = 0; i < nd; i++)
					for (j = 0; j < ntimes; j++)
						if (occur(&w, days[i], times[j]))
							return;
			}
		} else {
			/* hours, minutes, seconds: the period is one of them */
			long per_day = r->freq == F_HOURLY ? 24 : r->freq == F_MINUTELY ? 1440 : 86400L;
			long off0 = r->freq == F_HOURLY ? sod0 / 3600 : r->freq == F_MINUTELY ? sod0 / 60 : sod0;
			long total = off0 + p * r->interval, day = d0 + total / per_day, within = total % per_day;
			int h, mi, s0 = (int)(sod0 % 60), m0s = (int)(sod0 / 60 % 60);
			base = day;
			if (!day_ok(r, day)) {
				/* the whole day is out: on to the first period of the next */
				long next = (day + 1 - d0) * per_day - off0;
				long np = (next + r->interval - 1) / r->interval;
				if (np > p)
					p = np - 1;
				if (stop != CT_NONE && day * MIN_PER_DAY > stop + MIN_PER_DAY)
					return;
				continue;
			}
			if (r->freq == F_HOURLY) {
				h = (int)within;
				if (r->byhour && !((r->byhour >> h) & 1))
					goto next;
				for (mi = 0; mi < 60; mi++) {
					int s;
					if (any_bits(r->byminute, 2) ? !has_bit(r->byminute, mi) : mi != m0s)
						continue;
					for (s = 0; s <= 60; s++)
						if ((any_bits(r->bysecond, 2) ? has_bit(r->bysecond, s) : s == s0) && nc < CMAX)
							cand[nc++] = h * 3600L + mi * 60L + s;
				}
			} else if (r->freq == F_MINUTELY) {
				int s;
				h = (int)(within / 60);
				mi = (int)(within % 60);
				if ((r->byhour && !((r->byhour >> h) & 1)) ||
				    (any_bits(r->byminute, 2) && !has_bit(r->byminute, mi)))
					goto next;
				for (s = 0; s <= 60; s++)
					if ((any_bits(r->bysecond, 2) ? has_bit(r->bysecond, s) : s == s0) && nc < CMAX)
						cand[nc++] = h * 3600L + mi * 60L + s;
			} else {
				h = (int)(within / 3600);
				mi = (int)(within / 60 % 60);
				if ((r->byhour && !((r->byhour >> h) & 1)) ||
				    (any_bits(r->byminute, 2) && !has_bit(r->byminute, mi)) ||
				    (any_bits(r->bysecond, 2) && !has_bit(r->bysecond, (int)(within % 60))))
					goto next;
				cand[nc++] = within;
			}
			nc = setpos(r, cand, nc);
			for (i = 0; i < nc; i++)
				if (occur(&w, day, cand[i]))
					return;
		}
next:
		/* well past what's wanted (an empty period can't stop us above) */
		if (stop != CT_NONE && base * MIN_PER_DAY > stop + 2 * MIN_PER_DAY && base > d0)
			return;
		if (r->until != CT_NONE && base * MIN_PER_DAY > r->until + 31 * MIN_PER_DAY)
			return;
	}
	if (!w.started && !w.stopped)
		occur(&w, d0, sod0);	/* a rule that never matches: DTSTART still happens */
}

/* ---------------- occurrences of an event ---------------- */

CTIME ev_wall(const EVENT *e, CTIME t, const TZONE *view)
{
	if (e->allday || e->floating || !e->tz)
		return t;
	return tz_to_local(view, t);
}

CTIME ev_from_wall(const EVENT *e, CTIME wall, const TZONE *view)
{
	if (e->allday || e->floating || !e->tz)
		return wall;
	return tz_to_utc(view, wall);
}

typedef struct {
	EVENT *e;
	const TZONE *ez;	/* zone the series repeats in; NULL: wall times */
	const TZONE *view;
	CTIME from, to, dur;
	OCC_CB add;
	SKIP_CB skip;
	void *ud;
	CTIME *ex;		/* EXRULE's starts (wall), sorted */
	int nex;
} XP;

static int excluded(XP *x, CTIME c, CTIME rep)
{
	EVENT *e = x->e;
	int i, lo, hi;
	for (i = 0; i < e->nexdate; i++)
		if (e->exdate[i] == rep || (e->allday && ct_days(e->exdate[i]) == ct_days(rep)))
			return 1;
	lo = 0;
	hi = x->nex - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (x->ex[mid] == c)
			return 1;
		if (x->ex[mid] < c)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return x->skip && x->skip(e, rep, x->ud);
}

/* one occurrence at wall time c (in the event's zone); 1 when past the range */
static int emit(XP *x, CTIME c, CTIME dur)
{
	CTIME rep, vs, ve;
	/* well before the range: no time-zone maths. Two zones' clocks are
	   at most 26 hours apart, so an occurrence ending more than 27 hours
	   before the range in its own zone's time ends before it in any
	   (a birthday from 1960 is walked year by year, every minute) */
	if (c + (dur > 0 ? dur : 0) + 27 * 60L < x->from)
		return 0;
	rep = x->ez ? tz_to_utc(x->ez, c) : c;
	if (x->ez) {
		vs = tz_to_local(x->view, rep);
		ve = tz_to_local(x->view, rep + dur);
	} else {
		vs = rep;
		ve = rep + dur;
	}
	if (vs >= x->to)
		return 1;
	if (excluded(x, c, rep))
		return 0;
	if (ve > x->from || (ve == vs && vs >= x->from))
		x->add(x->e, vs, ve, x->ud);
	return 0;
}

static int walk_emit(CTIME c, void *ud)
{
	XP *x = ud;
	return emit(x, c, x->dur);
}

typedef struct {
	CTIME *v;
	int n, max;
	CTIME to;
} EXSET;

static int walk_collect(CTIME c, void *ud)
{
	EXSET *s = ud;
	if (c > s->to)
		return 1;
	if (s->n >= s->max) {
		CTIME *nv;
		int nm = s->max ? s->max * 2 : 64;
		if (nm > 4096)
			return 1;
		nv = realloc(s->v, nm * sizeof(CTIME));
		if (!nv)
			return 1;
		s->v = nv;
		s->max = nm;
	}
	s->v[s->n++] = c;
	return 0;
}

static int cmp_ct(const void *a, const void *b)
{
	CTIME x = *(const CTIME *)a, y = *(const CTIME *)b;
	return x < y ? -1 : x > y;
}

void ics_expand(EVENT *e, CTIME from, CTIME to, const TZONE *view,
		OCC_CB add, SKIP_CB skip, void *ud)
{
	XP x;
	CTIME w0;
	int i;
	EXSET exs;
	memset(&x, 0, sizeof(x));
	x.e = e;
	x.ez = (e->allday || e->floating) ? 0 : e->tz;
	x.view = view;
	x.from = from;
	x.to = to;
	x.dur = e->end - e->start;
	x.add = add;
	x.skip = skip;
	x.ud = ud;
	if (e->status == ST_CANCELLED && !e->rr)
		return;
	w0 = x.ez ? tz_to_local(x.ez, e->start) : e->start;
	if (!e->rr && !e->nrdate) {
		emit(&x, w0, x.dur);
		return;
	}
	/* EXRULE: its starts in (about) the range, to leave out */
	memset(&exs, 0, sizeof(exs));
	for (i = 0; i < e->nexrule; i++) {
		exs.to = to + 2 * MIN_PER_DAY;
		rr_walk(&e->exrule[i], w0, from - 2 * MIN_PER_DAY, exs.to, walk_collect, &exs);
	}
	if (exs.n > 1)
		qsort(exs.v, exs.n, sizeof(CTIME), cmp_ct);
	x.ex = exs.v;
	x.nex = exs.n;
	if (e->rr)
		rr_walk(e->rr, w0, from, to, walk_emit, &x);
	else
		emit(&x, w0, x.dur);	/* RDATEs only: DTSTART is one too */
	/* RDATE: more starts, each maybe with its own end */
	for (i = 0; i < e->nrdate; i++) {
		CTIME r = e->rdate[i], c = x.ez ? tz_to_local(x.ez, r) : r;
		CTIME dur = e->rdate_end && e->rdate_end[i] != CT_NONE ? e->rdate_end[i] - r : x.dur;
		if (c != w0)
			emit(&x, c, dur);
	}
	free(exs.v);
}
