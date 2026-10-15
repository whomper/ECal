/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * editor.c - the event dialog: title, place, all-day, start and end,
 * time zone, repeat, alert, calendar, notes. For a repeating event it
 * asks, as the Mac does, whether a change is for this occurrence only
 * or for all future ones.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "util.h"
#include "clip.h"

static short last_cal = -1;	/* new events go where the last one went */

/* ---- dates and times as typed: 12.10.2026, 12/10/26, 12.10; 9:30, 930, 9pm ---- */

static void fmt_date(long day, char *out, int size)
{
	int y, m, d;
	civil_from_days(day, &y, &m, &d);
	snprintf(out, size, "%02d.%02d.%04d", d, m, y);
}

static int parse_date(const char *s, long *day)
{
	int v[3] = { 0, 0, 0 }, n = 0, y, m, d;
	DT now;
	ct_split(now_view(), &now);
	while (*s && n < 3) {
		while (*s == ' ')
			s++;
		if (*s < '0' || *s > '9')
			break;
		while (*s >= '0' && *s <= '9')
			v[n] = v[n] * 10 + (*s++ - '0');
		n++;
		if (*s == '.' || *s == '/' || *s == '-')
			s++;
	}
	if (n < 2)
		return 0;
	d = v[0];
	m = v[1];
	y = n == 3 ? v[2] : now.year;
	if (y < 100)
		y += 2000;
	if (m < 1 || m > 12 || d < 1 || d > days_in_month(y, m) || y > 2999)
		return 0;
	*day = days_from_civil(y, m, d);
	return 1;
}

static void fmt_hm(short minutes, char *out, int size)
{
	fmt_time(minutes / 60, minutes % 60, opt.h24, out, size);
}

static int parse_time(const char *s, short *minutes)
{
	int h = 0, m = 0, digits = 0;
	const char *p = s;
	while (*p == ' ')
		p++;
	while (*p >= '0' && *p <= '9') {
		h = h * 10 + (*p++ - '0');
		digits++;
	}
	if (*p == ':' || *p == '.') {
		p++;
		while (*p >= '0' && *p <= '9')
			m = m * 10 + (*p++ - '0');
	} else if (digits >= 3) {	/* 930, 1430 */
		m = h % 100;
		h /= 100;
	}
	while (*p == ' ')
		p++;
	if ((*p | 32) == 'p' && h < 12)
		h += 12;
	else if ((*p | 32) == 'a' && h == 12)
		h = 0;
	if (!digits || h > 23 || m > 59)
		return 0;
	*minutes = (short)(h * 60 + m);
	return 1;
}

/* ---- the choices ---- */

static const char *const alert_items[] = {
	"  None", "  At time of event", "  5 minutes before", "  10 minutes before",
	"  15 minutes before", "  30 minutes before", "  1 hour before", "  2 hours before",
	"  1 day before", "  2 days before", "  1 week before"
};
static const short alert_min[] = { -1, 0, 5, 10, 15, 30, 60, 120, 1440, 2880, 10080 };
static const char *const allday_alert_items[] = {
	"  None", "  On the day (9:00)", "  1 day before (9:00)", "  2 days before (9:00)", "  1 week before"
};
static const short allday_alert_min[] = { -1, -540, 900, 2340, 10080 };

enum { R_NONE, R_DAY, R_WEEKDAYS, R_WEEK, R_2WEEKS, R_MONTH, R_YEAR, R_HEBYEAR, R_CUSTOM, R_N };
static const char *repeat_items[R_N];
static const char *const end_items[] = { "  Never", "  On date", "  After" };

static int no_by_parts(const RRULE *r)
{
	return !r->nbymd && !r->bymonth && !r->bymonth_leap && !r->nbysetpos && !r->nbyyd && !r->nbywn &&
	       !r->byhour && !r->byminute[0] && !r->byminute[1] && !r->bysecond[0] && !r->bysecond[1];
}

/* which item of the Repeat menu a rule is */
static int rule_kind(const RRULE *r, short wkst)
{
	(void)wkst;
	if (!r)
		return R_NONE;
	if (!no_by_parts(r))
		return R_CUSTOM;
	if (r->rscale == RS_HEBREW)
		return r->freq == F_YEARLY && r->interval == 1 && !r->nbyday ? R_HEBYEAR : R_CUSTOM;
	if (r->freq == F_DAILY && r->interval == 1 && !r->nbyday)
		return R_DAY;
	if (r->freq == F_WEEKLY && r->nbyday == 5 && r->interval == 1) {
		/* Sunday to Thursday, or Monday to Friday */
		int i, mask = 0, ord, wd;
		for (i = 0; i < 5; i++) {
			rr_byday(r->byday[i], &ord, &wd);
			mask |= 1 << wd;
		}
		if (mask == 0x1f || mask == 0x3e)
			return R_WEEKDAYS;
	}
	if (r->freq == F_WEEKLY && r->nbyday <= 1 && r->interval <= 2 && (!r->nbyday || (r->byday[0] >= 0 && r->byday[0] < 8)))
		return r->interval == 2 ? R_2WEEKS : R_WEEK;
	if (r->freq == F_MONTHLY && r->interval == 1 && !r->nbyday)
		return R_MONTH;
	if (r->freq == F_YEARLY && r->interval == 1 && !r->nbyday)
		return R_YEAR;
	return R_CUSTOM;
}

static void make_rule(RRULE *r, int kind, long startday)
{
	int i;
	memset(r, 0, sizeof(*r));
	r->interval = 1;
	r->until = CT_NONE;
	r->wkst = (unsigned char)(opt.weekstart == 0 ? 0 : 1);
	switch (kind) {
	case R_DAY: r->freq = F_DAILY; break;
	case R_WEEKDAYS:
		r->freq = F_WEEKLY;
		for (i = 0; i < 5; i++)
			r->byday[i] = (short)(opt.weekstart == 0 ? i : i + 1);	/* Sun-Thu in Israel */
		r->nbyday = 5;
		break;
	case R_2WEEKS: r->interval = 2; /* fall through */
	case R_WEEK:
		r->freq = F_WEEKLY;
		r->byday[0] = (short)wday_of(startday);
		r->nbyday = 1;
		break;
	case R_MONTH: r->freq = F_MONTHLY; break;
	case R_YEAR: r->freq = F_YEARLY; break;
	case R_HEBYEAR: r->freq = F_YEARLY; r->rscale = RS_HEBREW; break;
	}
}

/* ---- Custom...: any rule ---- */

static const char *const freq_items[] = {
	"  minutes", "  hours", "  days", "  weeks", "  months", "  years"
};
static const unsigned char freq_of[] = { F_MINUTELY, F_HOURLY, F_DAILY, F_WEEKLY, F_MONTHLY, F_YEARLY };
static const char *const ord_items[] = {
	"  first", "  second", "  third", "  fourth", "  fifth", "  last", "  second to last"
};
static const signed char ord_of[] = { 1, 2, 3, 4, 5, -1, -2 };
static const char *const onday_items[] = {
	"  Sunday", "  Monday", "  Tuesday", "  Wednesday", "  Thursday", "  Friday", "  Saturday",
	"  day", "  weekday", "  weekend day"
};

enum {
	C_LFREQ, C_IVAL, C_FREQ, C_LDAYS, C_D0, C_D1, C_D2, C_D3, C_D4, C_D5, C_D6,
	C_ONTHE, C_ORD, C_ONDAY, C_LMD, C_MD,
	C_LMON, C_M0, C_M1, C_M2, C_M3, C_M4, C_M5, C_M6, C_M7, C_M8, C_M9, C_M10, C_M11,
	C_LHOUR, C_HOURS, C_LMIN, C_MINS, C_LRULE, C_RULE, C_NOTE, C_CANCEL, C_OK, CN
};

typedef struct {
	FIELD f[CN];
	char ival[6], md[60], hours[60], mins[60], rule[200];
	char shown[200];	/* the rule text as last generated from the fields */
} CUST;

static void list_text(char *out, int size, const long *v, int n)
{
	int i, o = 0;
	out[0] = 0;
	for (i = 0; i < n && o < size - 6; i++)
		o += snprintf(out + o, size - o, "%s%ld", i ? "," : "", v[i]);
}

/* the rule the fields describe (without its end) */
static void cust_rule(CUST *c, RRULE *r, long startday)
{
	char tmp[60];
	long v[61];
	int i, n, k;
	memset(r, 0, sizeof(*r));
	r->until = CT_NONE;
	r->wkst = (unsigned char)(opt.weekstart == 0 ? 0 : 1);
	r->freq = freq_of[c->f[C_FREQ].val];
	r->interval = (short)(atoi(c->ival) > 0 ? atoi(c->ival) : 1);
	if (r->freq == F_WEEKLY) {
		for (i = 0; i < 7; i++)
			if (c->f[C_D0 + i].val)
				r->byday[r->nbyday++] = (short)i;
		if (!r->nbyday)
			r->byday[r->nbyday++] = (short)wday_of(startday);
	}
	if (r->freq == F_MONTHLY || r->freq == F_YEARLY) {
		if (c->f[C_ONTHE].val) {
			int ord = ord_of[c->f[C_ORD].val], kind = c->f[C_ONDAY].val;
			if (kind < 7) {
				r->byday[r->nbyday++] = (short)(ord * 8 + kind);
			} else if (kind == 7) {
				r->bymd[r->nbymd++] = (signed char)ord;
			} else {
				/* a weekday or a weekend day: the days, then which of them */
				static const int wk_il[] = { 0, 1, 2, 3, 4 }, wk[] = { 1, 2, 3, 4, 5 };
				static const int we_il[] = { 5, 6 }, we[] = { 0, 6 };
				const int *d = kind == 8 ? (opt.weekstart == 0 ? wk_il : wk) : (opt.weekstart == 0 ? we_il : we);
				int nd = kind == 8 ? 5 : 2;
				for (k = 0; k < nd; k++)
					r->byday[r->nbyday++] = (short)d[k];
				r->bysetpos[r->nbysetpos++] = (short)ord;
			}
		} else if (c->md[0]) {
			snprintf(tmp, sizeof(tmp), "BYMONTHDAY=%s", c->md);
			{
				RRULE t;
				char rr[80];
				snprintf(rr, sizeof(rr), "FREQ=MONTHLY;%s", tmp);
				if (rrule_parse(rr, &t)) {
					memcpy(r->bymd, t.bymd, sizeof(r->bymd));
					r->nbymd = t.nbymd;
				}
			}
		}
	}
	if (r->freq == F_YEARLY)
		for (i = 0; i < 12; i++)
			if (c->f[C_M0 + i].val)
				r->bymonth |= (unsigned short)(1 << i);
	if (r->freq != F_MINUTELY && r->freq != F_HOURLY && c->hours[0]) {
		char rr[100];
		RRULE t;
		snprintf(rr, sizeof(rr), "FREQ=DAILY;BYHOUR=%s", c->hours);
		if (rrule_parse(rr, &t))
			r->byhour = t.byhour;
	}
	if (r->freq != F_MINUTELY && c->mins[0]) {
		char rr[100];
		RRULE t;
		snprintf(rr, sizeof(rr), "FREQ=DAILY;BYMINUTE=%s", c->mins);
		if (rrule_parse(rr, &t))
			memcpy(r->byminute, t.byminute, sizeof(r->byminute));
	}
	(void)v;
	(void)n;
}

static long cust_day;

static void cust_enable(CUST *c)
{
	int f = c->f[C_FREQ].val, fq = freq_of[f], i;
	short wk = fq == F_WEEKLY ? 0 : FF_DISABLED, mo = fq == F_MONTHLY || fq == F_YEARLY ? 0 : FF_DISABLED;
	short yr = fq == F_YEARLY ? 0 : FF_DISABLED, tm = fq == F_MINUTELY || fq == F_HOURLY ? FF_DISABLED : 0;
	for (i = 0; i < 7; i++)
		c->f[C_D0 + i].flags = wk;
	c->f[C_ONTHE].flags = mo;
	c->f[C_ORD].flags = mo | (c->f[C_ONTHE].val ? 0 : FF_DISABLED);
	c->f[C_ONDAY].flags = mo | (c->f[C_ONTHE].val ? 0 : FF_DISABLED);
	c->f[C_MD].flags = FF_LTR | mo | (c->f[C_ONTHE].val ? FF_DISABLED : 0);
	for (i = 0; i < 12; i++)
		c->f[C_M0 + i].flags = yr;
	c->f[C_HOURS].flags = FF_LTR | tm;
	c->f[C_MINS].flags = FF_LTR | (fq == F_MINUTELY ? FF_DISABLED : 0);
}

static void cust_show_rule(CUST *c)
{
	RRULE r;
	cust_rule(c, &r, cust_day);
	rrule_format(&r, 0, 0, c->rule, sizeof(c->rule));
	str_copy(c->shown, c->rule, sizeof(c->shown));
	c->f[C_RULE].cur = (short)strlen(c->rule);
	c->f[C_RULE].first = 0;
}

static void cust_changed(DLG *d, short i)
{
	CUST *c = d->ud;
	short k;
	(void)i;
	cust_enable(c);
	cust_show_rule(c);
	for (k = 0; k < CN; k++)
		dlg_redraw_field(d, k);
}

static void cput(FIELD *f, short type, short flags, short x, short y, short w, const char *label)
{
	memset(f, 0, sizeof(*f));
	f->type = type;
	f->flags = flags;
	f->x = x;
	f->y = y;
	f->w = w;
	f->h = 1;
	f->label = label;
}

/* edit rule r in the Custom dialog; 1 if OK. The end (count, until)
   stays as it was: the event dialog sets it */
static int custom_rule(RRULE *r, long startday)
{
	static const char *const dn[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
	static CUST c;
	DLG d;
	RRULE in = *r;
	short i, row = 1, res;
	memset(&c, 0, sizeof(c));
	memset(&d, 0, sizeof(d));
	cust_day = startday;
	cput(&c.f[C_LFREQ], FT_LABEL, 0, 0, row, 0, "Every");
	cput(&c.f[C_IVAL], FT_TEXT, FF_LTR, 7, row, 4, 0);
	c.f[C_IVAL].buf = c.ival;
	c.f[C_IVAL].max = 5;
	snprintf(c.ival, sizeof(c.ival), "%d", r->interval > 0 ? r->interval : 1);
	cput(&c.f[C_FREQ], FT_POPUP, 0, 13, row, 14, 0);
	c.f[C_FREQ].items = freq_items;
	c.f[C_FREQ].nitems = 6;
	c.f[C_FREQ].val = 2;
	for (i = 0; i < 6; i++)
		if (freq_of[i] == r->freq)
			c.f[C_FREQ].val = i;
	row += 2;
	cput(&c.f[C_LDAYS], FT_LABEL, 0, 0, row, 0, "On:");
	for (i = 0; i < 7; i++) {
		int k, ord, wd;
		cput(&c.f[C_D0 + i], FT_CHECK, 0, 7 + i * 6, row, 5, dn[i]);
		for (k = 0; k < r->nbyday; k++) {
			rr_byday(r->byday[k], &ord, &wd);
			if (!ord && wd == i && r->freq == F_WEEKLY)
				c.f[C_D0 + i].val = 1;
		}
	}
	row += 2;
	cput(&c.f[C_ONTHE], FT_CHECK, 0, 0, row, 7, "the");
	cput(&c.f[C_ORD], FT_POPUP, 0, 7, row, 17, 0);
	c.f[C_ORD].items = ord_items;
	c.f[C_ORD].nitems = 7;
	cput(&c.f[C_ONDAY], FT_POPUP, 0, 26, row, 15, 0);
	c.f[C_ONDAY].items = onday_items;
	c.f[C_ONDAY].nitems = 10;
	/* "on the" from the rule: one ordinal weekday, a weekday/weekend set
	   with one BYSETPOS, or one BYMONTHDAY of -1/-2 */
	{
		int ord, wd, k;
		if (r->nbyday == 1 && !r->nbysetpos) {
			rr_byday(r->byday[0], &ord, &wd);
			for (k = 0; k < 7; k++)
				if (ord_of[k] == ord && ord) {
					c.f[C_ONTHE].val = 1;
					c.f[C_ORD].val = k;
					c.f[C_ONDAY].val = (short)wd;
				}
		} else if (r->nbysetpos == 1 && (r->nbyday == 5 || r->nbyday == 2)) {
			for (k = 0; k < 7; k++)
				if (ord_of[k] == r->bysetpos[0]) {
					c.f[C_ONTHE].val = 1;
					c.f[C_ORD].val = k;
					c.f[C_ONDAY].val = r->nbyday == 5 ? 8 : 9;
				}
		} else if (r->nbymd == 1 && r->bymd[0] < 0) {
			for (k = 0; k < 7; k++)
				if (ord_of[k] == r->bymd[0]) {
					c.f[C_ONTHE].val = 1;
					c.f[C_ORD].val = k;
					c.f[C_ONDAY].val = 7;
				}
		}
	}
	row++;
	cput(&c.f[C_LMD], FT_LABEL, 0, 0, row, 0, "or on days of the month:");
	cput(&c.f[C_MD], FT_TEXT, FF_LTR, 26, row, 30, 0);
	c.f[C_MD].buf = c.md;
	c.f[C_MD].max = sizeof(c.md) - 1;
	if (!c.f[C_ONTHE].val) {
		long v[RR_MAXBY];
		for (i = 0; i < r->nbymd; i++)
			v[i] = r->bymd[i];
		list_text(c.md, sizeof(c.md), v, r->nbymd);
	}
	row += 2;
	cput(&c.f[C_LMON], FT_LABEL, 0, 0, row, 0, "In:");
	for (i = 0; i < 12; i++) {
		cput(&c.f[C_M0 + i], FT_CHECK, 0, 7 + (i % 6) * 8, row + i / 6, 7, month_short[i]);
		c.f[C_M0 + i].val = (r->bymonth >> i) & 1;
	}
	row += 3;
	cput(&c.f[C_LHOUR], FT_LABEL, 0, 0, row, 0, "At hours:");
	cput(&c.f[C_HOURS], FT_TEXT, FF_LTR, 11, row, 18, 0);
	c.f[C_HOURS].buf = c.hours;
	c.f[C_HOURS].max = sizeof(c.hours) - 1;
	cput(&c.f[C_LMIN], FT_LABEL, 0, 31, row, 0, "minutes:");
	cput(&c.f[C_MINS], FT_TEXT, FF_LTR, 40, row, 16, 0);
	c.f[C_MINS].buf = c.mins;
	c.f[C_MINS].max = sizeof(c.mins) - 1;
	{
		long v[61];
		int n = 0;
		for (i = 0; i < 24; i++)
			if ((r->byhour >> i) & 1)
				v[n++] = i;
		list_text(c.hours, sizeof(c.hours), v, n);
		n = 0;
		for (i = 0; i < 60; i++)
			if ((r->byminute[i / 32] >> (i % 32)) & 1)
				v[n++] = i;
		list_text(c.mins, sizeof(c.mins), v, n);
	}
	row += 2;
	cput(&c.f[C_LRULE], FT_LABEL, 0, 0, row, 0, "Rule:");
	cput(&c.f[C_RULE], FT_TEXT, FF_LTR, 7, row, 49, 0);
	c.f[C_RULE].buf = c.rule;
	c.f[C_RULE].max = sizeof(c.rule) - 1;
	cput(&c.f[C_NOTE], FT_LABEL, FF_LTR, 7, row + 1, 0, "(iCalendar RRULE: type any rule here)");
	row += 3;
	cput(&c.f[C_CANCEL], FT_BUTTON, FF_CANCEL, 34, row, 10, "Cancel");
	cput(&c.f[C_OK], FT_BUTTON, FF_DEFAULT, 46, row, 10, "OK");
	cust_enable(&c);
	/* the rule as it is, without its end */
	{
		RRULE t = in;
		t.count = 0;
		t.until = CT_NONE;
		rrule_format(&t, 0, 0, c.rule, sizeof(c.rule));
		cust_rule(&c, r, startday);
		rrule_format(r, 0, 0, c.shown, sizeof(c.shown));
		*r = in;
		/* fields that can't show the rule: the text is what counts */
		if (strcmp(c.rule, c.shown))
			c.shown[0] = 1;
	}
	d.title = "Custom repeat";
	d.w = 57;
	d.h = row + 1;
	d.f = c.f;
	d.n = CN;
	d.focus = C_IVAL;
	d.changed = cust_changed;
	d.ud = &c;
	for (;;) {
		RRULE nr;
		res = dlg_run(&d);
		if (res != C_OK)
			return 0;
		if (strcmp(c.rule, c.shown)) {
			/* the text was typed (or the fields can't express the rule) */
			if (!rrule_parse(c.rule, &nr)) {
				alert(1, "[1][That isn't a rule ECal understands. It starts with FREQ= (e.g. FREQ=MONTHLY;BYDAY=-1FR).][ OK ]");
				continue;
			}
		} else {
			cust_rule(&c, &nr, startday);
		}
		nr.count = in.count;
		nr.until = in.until;
		nr.until_kind = in.until_kind;
		*r = nr;
		return 1;
	}
}

/* ---- the dialog ---- */

enum {
	L_TITLE, F_TITLE, L_LOC, F_LOC, F_ALLDAY, L_CAL, F_CAL,
	L_START, F_SDATE, F_STIME, L_TZ, F_TZ, L_END, F_EDATE, F_ETIME,
	L_REP, F_REP, F_REPEND, F_REPVAL, L_REPDESC, L_ALERT, F_ALERT, L_NOTES, F_NOTES, L_URL,
	B_DELETE, B_CANCEL, B_OK, NF
};

typedef struct {
	FIELD f[NF];
	short calidx[MAXCAL];		/* popup item -> calendar */
	const char *calnames[MAXCAL];
	char calbuf[MAXCAL][40];
	short ncal;
	const char *tzitems[5];
	char tzlab[4][44];
	const TZONE *tzz[4];
	const TZONE *zone;		/* chosen; NULL: floating */
	short readonly;
	RRULE rule;			/* the repeat rule being edited */
	short has_rule;
	short last_rep;			/* the Repeat item before a change (Custom... cancelled) */
	short rep_touched;		/* the Repeat menu was used */
	char repdesc[90];
} ED;

static void describe_rule(ED *x)
{
	if (!x->has_rule) {
		x->repdesc[0] = 0;
		return;
	}
	{
		RRULE t = x->rule;
		t.count = 0;
		t.until = CT_NONE;
		rrule_describe(&t, x->repdesc, sizeof(x->repdesc));
	}
}

static void set_allday(DLG *d, ED *x)
{
	int ad = x->f[F_ALLDAY].val;
	short fl = ad ? FF_DISABLED : 0;
	x->f[F_STIME].flags = (x->f[F_STIME].flags & ~FF_DISABLED) | fl | (x->readonly ? FF_DISABLED : 0);
	x->f[F_ETIME].flags = (x->f[F_ETIME].flags & ~FF_DISABLED) | fl | (x->readonly ? FF_DISABLED : 0);
	x->f[F_TZ].flags = (x->f[F_TZ].flags & ~FF_DISABLED) | fl | (x->readonly ? FF_DISABLED : 0);
	x->f[F_ALERT].items = ad ? allday_alert_items : alert_items;
	x->f[F_ALERT].nitems = ad ? 5 : 11;
	if (x->f[F_ALERT].val >= x->f[F_ALERT].nitems)
		x->f[F_ALERT].val = 0;
	(void)d;
}

static void set_repend(ED *x)
{
	int rep = x->f[F_REP].val != R_NONE && x->has_rule, e = x->f[F_REPEND].val;
	x->f[F_REPEND].flags = rep && !x->readonly ? 0 : FF_DISABLED;
	x->f[F_REPVAL].flags = (rep && e && !x->readonly ? 0 : FF_DISABLED) | FF_LTR;
	x->f[F_REPVAL].w = e == 2 ? 4 : 10;
}

static char repval[16], repval0[16];	/* the end of the repeat, and as first shown */
static short repend0;

static void changed(DLG *d, short i)
{
	ED *x = d->ud;
	if (i == F_ALLDAY) {
		set_allday(d, x);
		dlg_redraw_field(d, F_STIME);
		dlg_redraw_field(d, F_ETIME);
		dlg_redraw_field(d, F_TZ);
		dlg_redraw_field(d, F_ALERT);
	} else if (i == F_REP) {
		long day;
		int k = x->f[F_REP].val;
		if (!parse_date(x->f[F_SDATE].buf, &day))
			day = ct_days(now_view());
		if (k == R_NONE) {
			x->has_rule = 0;
		} else if (k == R_CUSTOM) {
			RRULE r = x->rule;
			if (!x->has_rule)
				make_rule(&r, R_WEEK, day);
			if (custom_rule(&r, day)) {
				x->rule = r;
				x->has_rule = 1;
			}
			x->f[F_REP].val = x->has_rule ? (short)rule_kind(&x->rule, opt.weekstart) : x->last_rep;
		} else {
			make_rule(&x->rule, k, day);
			x->has_rule = 1;
		}
		x->last_rep = x->f[F_REP].val;
		x->rep_touched = 1;
		describe_rule(x);
		set_repend(x);
		dlg_redraw(d);		/* Custom... drew over it */
	} else if (i == F_REPEND) {
		if (i == F_REPEND && x->f[F_REPEND].val == 2 && !strchr(repval, '.') && !repval[0])
			str_copy(repval, "10", sizeof(repval));
		if (i == F_REPEND && x->f[F_REPEND].val == 2 && strchr(repval, '.'))
			str_copy(repval, "10", sizeof(repval));
		if (i == F_REPEND && x->f[F_REPEND].val == 1 && !strchr(repval, '.')) {
			long day;
			if (parse_date(x->f[F_SDATE].buf, &day))
				fmt_date(days_add_months(day, 3), repval, sizeof(repval));
		}
		x->f[F_REPVAL].cur = (short)strlen(repval);
		set_repend(x);
		dlg_redraw_field(d, F_REPEND);
		dlg_redraw_field(d, F_REPVAL);
	} else if (i == F_TZ) {
		short k = x->f[F_TZ].val;
		if (k == 4) {			/* Other... */
			const char *name = zone_pick(x->zone ? x->zone->name : opt.home_tz);
			if (name) {
				x->zone = tz_find(name);
				str_copy(x->tzlab[0], tz_city(x->zone), sizeof(x->tzlab[0]));
				x->tzz[0] = x->zone;
			}
			x->f[F_TZ].val = 0;
		} else {
			x->zone = x->tzz[k];
		}
		dlg_redraw_field(d, F_TZ);
	}
}

static void put(FIELD *f, short type, short flags, short x, short y, short w, const char *label)
{
	memset(f, 0, sizeof(*f));
	f->type = type;
	f->flags = flags;
	f->x = x;
	f->y = y;
	f->w = w;
	f->h = 1;
	f->label = label;
}

/* as on the Mac, a change to a repeating event asks; "all future" from
   the first occurrence is all of them */
static int ask_how(int first)
{
	short b = alert(1, "[2][This is a repeating event.|Change only this occurrence,|or this and all future ones?][Only this|All future|Cancel]");
	if (b == 1)
		return SAVE_THIS;
	if (b == 2)
		return first ? SAVE_ALL : SAVE_FUTURE;
	return -1;
}

/* e: the event (NULL for a new one, built from day/minutes); occ_start:
   the occurrence clicked, in the view's wall time */
static int run_editor(EVENT *e, CTIME occ_start, CTIME orig, int is_new)
{
	static char title[200], loc[120], sdate[16], stime[12], edate[16], etime[12], notes[1200];
	ED x;
	DLG d;
	EVENT *m = e->res >= 0 ? ev_master(e) : 0;
	const TZONE *vz = view_zone(), *shown_z;
	/* act: where this occurrence is now; occ_rep: the start the series
	   gave it, which "only this one" is about (they differ for an
	   occurrence that was changed before) */
	CTIME act = ev_from_wall(e, occ_start, vz), dur = e->end - e->start, ws, we;
	CTIME occ_rep = orig != CT_NONE ? orig : e->recurid != CT_NONE ? e->recurid : act;
	short i, r, row;
	int repeating = m && m->rr;
	char urlline[90];
	memset(&x, 0, sizeof(x));
	memset(&d, 0, sizeof(d));
	x.readonly = !is_new && !cal_writable(&cals[e->cal]);
	str_copy(title, e->summary ? e->summary : "", sizeof(title));
	str_copy(loc, e->location ? e->location : "", sizeof(loc));
	str_copy(notes, e->notes ? e->notes : "", sizeof(notes));
	/* times as the event's own zone shows them (time zone support), else the view's */
	x.zone = e->floating ? 0 : e->tz;
	shown_z = (opt.tzsupport && x.zone) ? x.zone : vz;
	if (e->allday) {
		ws = occ_start;
		we = occ_start + dur - MIN_PER_DAY;
	} else if (e->floating || !e->tz) {
		ws = occ_start;
		we = occ_start + dur;
	} else {
		ws = tz_to_local(shown_z, act);
		we = tz_to_local(shown_z, act + dur);
	}
	fmt_date(ct_days(ws), sdate, sizeof(sdate));
	fmt_date(ct_days(we), edate, sizeof(edate));
	fmt_hm((short)(ws - ct_day_start(ws)), stime, sizeof(stime));
	fmt_hm((short)(we - ct_day_start(we)), etime, sizeof(etime));

	row = 1;
	put(&x.f[L_TITLE], FT_LABEL, 0, 0, row, 0, "Title:");
	put(&x.f[F_TITLE], FT_TEXT, 0, 11, row, 47, 0);
	x.f[F_TITLE].buf = title;
	x.f[F_TITLE].max = sizeof(title) - 1;
	row++;
	put(&x.f[L_LOC], FT_LABEL, 0, 0, row, 0, "Location:");
	put(&x.f[F_LOC], FT_TEXT, 0, 11, row, 47, 0);
	x.f[F_LOC].buf = loc;
	x.f[F_LOC].max = sizeof(loc) - 1;
	row += 2;
	put(&x.f[F_ALLDAY], FT_CHECK, 0, 11, row, 10, "all-day");
	x.f[F_ALLDAY].val = e->allday;
	put(&x.f[L_CAL], FT_LABEL, 0, 25, row, 0, "Calendar:");
	put(&x.f[F_CAL], FT_POPUP, 0, 35, row, 23, 0);
	for (i = 0; i < ncals; i++)
		if (cal_writable(&cals[i]) || i == e->cal) {
			if (!cal_writable(&cals[i]) && !x.readonly)
				continue;
			snprintf(x.calbuf[x.ncal], sizeof(x.calbuf[0]), "  %s", cals[i].name);
			x.calnames[x.ncal] = x.calbuf[x.ncal];
			x.calidx[x.ncal] = i;
			if (i == e->cal)
				x.f[F_CAL].val = x.ncal;
			x.ncal++;
		}
	x.f[F_CAL].items = x.calnames;
	x.f[F_CAL].nitems = x.ncal;
	row++;
	put(&x.f[L_START], FT_LABEL, 0, 0, row, 0, "Starts:");
	put(&x.f[F_SDATE], FT_TEXT, FF_LTR, 11, row, 10, 0);
	x.f[F_SDATE].buf = sdate;
	x.f[F_SDATE].max = 12;
	put(&x.f[F_STIME], FT_TEXT, FF_LTR, 23, row, 7, 0);
	x.f[F_STIME].buf = stime;
	x.f[F_STIME].max = 9;
	/* time zone: the event's, home, the view's, floating, other */
	put(&x.f[L_TZ], FT_LABEL, opt.tzsupport ? 0 : FF_DISABLED, 32, row, 0, opt.tzsupport ? "Zone:" : "");
	put(&x.f[F_TZ], FT_POPUP, opt.tzsupport ? 0 : FF_DISABLED, 38, row, 20, 0);
	x.tzz[0] = x.zone ? x.zone : vz;
	x.tzz[1] = home_zone();
	x.tzz[2] = tz_utc();
	x.tzz[3] = 0;
	str_copy(x.tzlab[0], tz_city(x.tzz[0]), sizeof(x.tzlab[0]));
	snprintf(x.tzlab[1], sizeof(x.tzlab[1]), "  %s (home)", tz_city(x.tzz[1]));
	str_copy(x.tzlab[2], "  UTC", sizeof(x.tzlab[2]));
	str_copy(x.tzlab[3], "  Floating (any zone)", sizeof(x.tzlab[3]));
	for (i = 0; i < 4; i++)
		x.tzitems[i] = x.tzlab[i];
	x.tzitems[4] = "  Other...";
	x.f[F_TZ].items = x.tzitems;
	x.f[F_TZ].nitems = 5;
	x.f[F_TZ].val = x.zone ? 0 : 3;
	if (!opt.tzsupport)
		x.f[F_TZ].type = FT_LABEL, x.f[F_TZ].label = "";
	row++;
	put(&x.f[L_END], FT_LABEL, 0, 0, row, 0, "Ends:");
	put(&x.f[F_EDATE], FT_TEXT, FF_LTR, 11, row, 10, 0);
	x.f[F_EDATE].buf = edate;
	x.f[F_EDATE].max = 12;
	put(&x.f[F_ETIME], FT_TEXT, FF_LTR, 23, row, 7, 0);
	x.f[F_ETIME].buf = etime;
	x.f[F_ETIME].max = 9;
	row += 2;
	/* repeat */
	{
		const RRULE *cur = m ? m->rr : e->rr;
		int kind;
		x.has_rule = cur != 0;
		if (cur)
			x.rule = *cur;
		kind = rule_kind(cur, opt.weekstart);
		repeat_items[R_NONE] = "  None";
		repeat_items[R_DAY] = "  Every day";
		repeat_items[R_WEEKDAYS] = opt.weekstart == 0 ? "  Every weekday (Sun-Thu)" : "  Every weekday (Mon-Fri)";
		repeat_items[R_WEEK] = "  Every week";
		repeat_items[R_2WEEKS] = "  Every 2 weeks";
		repeat_items[R_MONTH] = "  Every month";
		repeat_items[R_YEAR] = "  Every year";
		repeat_items[R_HEBYEAR] = "  Every year (Hebrew date)";
		repeat_items[R_CUSTOM] = "  Custom...";
		put(&x.f[L_REP], FT_LABEL, 0, 0, row, 0, "Repeat:");
		put(&x.f[F_REP], FT_POPUP, 0, 11, row, 26, 0);
		x.f[F_REP].items = repeat_items;
		x.f[F_REP].nitems = R_N;
		x.f[F_REP].val = (short)kind;
		x.last_rep = (short)kind;
		put(&x.f[F_REPEND], FT_POPUP, 0, 39, row, 9, 0);
		x.f[F_REPEND].items = end_items;
		x.f[F_REPEND].nitems = 3;
		repval[0] = 0;
		{
			const RRULE *rr = m ? m->rr : e->rr;
			if (rr && rr->count) {
				x.f[F_REPEND].val = 2;
				snprintf(repval, sizeof(repval), "%ld", rr->count);
			} else if (rr && rr->until != CT_NONE) {
				x.f[F_REPEND].val = 1;
				fmt_date(ct_days(rr->until), repval, sizeof(repval));
			}
		}
		put(&x.f[F_REPVAL], FT_TEXT, FF_LTR, 49, row, 10, 0);
		x.f[F_REPVAL].buf = repval;
		x.f[F_REPVAL].max = 12;
		str_copy(repval0, repval, sizeof(repval0));
		repend0 = x.f[F_REPEND].val;
	}
	row++;
	/* what the rule means, in words ("Every month on the last Friday") */
	describe_rule(&x);
	put(&x.f[L_REPDESC], FT_LABEL, FF_LTR, 11, row, 47, x.repdesc);
	row++;
	put(&x.f[L_ALERT], FT_LABEL, 0, 0, row, 0, "Alert:");
	put(&x.f[F_ALERT], FT_POPUP, 0, 11, row, 26, 0);
	{
		const short *mins = e->allday ? allday_alert_min : alert_min;
		short n = e->allday ? 5 : 11;
		x.f[F_ALERT].val = 0;
		for (i = 0; i < n; i++)
			if (mins[i] == e->alarm)
				x.f[F_ALERT].val = i;
	}
	row += 2;
	put(&x.f[L_NOTES], FT_LABEL, 0, 0, row, 0, "Notes:");
	put(&x.f[F_NOTES], FT_MULTI, 0, 11, row, 47, 0);
	x.f[F_NOTES].h = 4;
	x.f[F_NOTES].buf = notes;
	x.f[F_NOTES].max = sizeof(notes) - 1;
	row += 4;
	urlline[0] = 0;
	if (e->url)
		snprintf(urlline, sizeof(urlline), "%.80s", e->url);
	put(&x.f[L_URL], FT_LABEL, FF_LTR, 11, row, 47, urlline);
	row += 1;
	put(&x.f[B_DELETE], FT_BUTTON, (is_new || x.readonly) ? FF_DISABLED : 0, 0, row, 10, "Delete");
	put(&x.f[B_CANCEL], FT_BUTTON, FF_CANCEL, 36, row, 10, x.readonly ? "Close" : "Cancel");
	put(&x.f[B_OK], FT_BUTTON, x.readonly ? FF_DISABLED : FF_DEFAULT, 48, row, 10, is_new ? "Add" : "OK");
	if (x.readonly)
		for (i = 0; i < NF; i++)
			if (i != B_CANCEL && x.f[i].type != FT_LABEL)
				x.f[i].flags |= FF_DISABLED;
	set_allday(&d, &x);
	set_repend(&x);
	d.title = is_new ? "New Event" : x.readonly ? "Event (read-only calendar)" : "Event";
	d.w = 59;
	d.h = row + 1;
	d.f = x.f;
	d.n = NF;
	d.focus = F_TITLE;
	d.changed = changed;
	d.ud = &x;

	for (;;) {
		long sday, eday;
		short smin = 0, emin = 0;
		EVENT *n;
		int how, ok, ci;
		r = dlg_run(&d);
		if (r == B_DELETE) {
			int h = SAVE_ALL;
			if (repeating) {
				short b = alert(1, "[2][Delete only this occurrence,|or this and all future ones?][Only this|All future|Cancel]");
				if (b == 3)
					continue;
				h = b == 1 ? SAVE_THIS : SAVE_FUTURE;
			} else if (alert(2, "[2][Delete \"%s\"?][Delete|Cancel]", e->summary ? e->summary : "New Event") != 1) {
				continue;
			}
			busy(1);
			ok = ev_delete(e, h, occ_rep);
			busy(0);
			if (!ok) {
				alert(1, "[1][Couldn't delete the event:|%s][ OK ]", cal_err);
				continue;
			}
			views_select_none();
			return 1;
		}
		if (r != B_OK)
			return 0;
		/* check what was typed */
		if (!parse_date(sdate, &sday) || !parse_date(edate, &eday)) {
			alert(1, "[1][Please type dates as|day.month.year: 12.10.2026][ OK ]");
			continue;
		}
		if (!x.f[F_ALLDAY].val && (!parse_time(stime, &smin) || !parse_time(etime, &emin))) {
			alert(1, "[1][Please type times as|hours:minutes, e.g. 9:30][ OK ]");
			continue;
		}
		n = ev_dup(e);
		if (!n)
			return 0;
		ev_set(&n->summary, title[0] ? title : "New Event");
		ev_set(&n->location, loc);
		ev_set(&n->notes, notes);
		n->allday = (unsigned char)x.f[F_ALLDAY].val;
		if (n->allday) {
			n->tz = 0;
			n->floating = 0;
			n->start = sday * MIN_PER_DAY;
			n->end = (eday + 1) * MIN_PER_DAY;
			n->alarm = allday_alert_min[x.f[F_ALERT].val];
		} else {
			CTIME a = sday * MIN_PER_DAY + smin, b = eday * MIN_PER_DAY + emin;
			const TZONE *z = opt.tzsupport ? x.zone : (e->allday || is_new || !e->tz || e->floating ? vz : e->tz);
			if (opt.tzsupport) {
				/* the times were typed in the chosen zone */
				n->tz = z;
				n->floating = !z;
				n->start = z ? tz_to_utc(z, a) : a;
				n->end = z ? tz_to_utc(z, b) : b;
			} else {
				/* typed in the view's zone; the event keeps its own */
				n->tz = z;
				n->floating = 0;
				n->start = tz_to_utc(vz, a);
				n->end = tz_to_utc(vz, b);
			}
			n->alarm = alert_min[x.f[F_ALERT].val];
		}
		if (n->end < n->start) {
			ev_free(n);
			alert(1, "[1][The event ends before it starts.][ OK ]");
			continue;
		}
		/* repeat: the rule being edited, with the end from the dialog */
		{
			int kind = x.f[F_REP].val;
			free(n->rr);
			n->rr = 0;
			if (x.has_rule && kind != R_NONE && (n->rr = malloc(sizeof(RRULE)))) {
				long until;
				int series_start = !repeating || (occ_rep == m->start && e == m);
				*n->rr = x.rule;
				/* the end as typed, unless it was left as it was shown */
				if (x.f[F_REPEND].val != repend0 || strcmp(repval, repval0) || !repeating) {
					n->rr->count = 0;
					n->rr->until = CT_NONE;
					if (x.f[F_REPEND].val == 2)
						n->rr->count = atol(repval) > 0 ? atol(repval) : 1;
					else if (x.f[F_REPEND].val == 1 && parse_date(repval, &until))
						n->rr->until = n->allday ? until * MIN_PER_DAY : until * MIN_PER_DAY + MIN_PER_DAY - 1;
				}
				/* "every week" follows the series to its new weekday, but
				   moving one occurrence doesn't change the rule */
				if ((kind == R_WEEK || kind == R_2WEEKS) && n->rr->nbyday == 1 &&
				    (series_start || x.rep_touched))
					n->rr->byday[0] = (short)wday_of(sday);
			}
		}
		ci = x.ncal ? x.calidx[x.f[F_CAL].val] : -1;
		if (ci < 0) {
			ev_free(n);
			alert(1, "[1][There is no calendar to put the event in: add an account or an On My Atari calendar first.][ OK ]");
			return 0;
		}
		how = SAVE_ALL;
		if (is_new) {
			n->res = -1;
		} else if (repeating) {
			int rule_changed = !n->rr || memcmp(n->rr, m->rr, sizeof(RRULE));
			int first = occ_rep == m->start && e == m;
			how = rule_changed ? (first ? SAVE_ALL : SAVE_FUTURE) : ask_how(first);
			if (how < 0) {
				ev_free(n);
				continue;
			}
			if (how == SAVE_ALL && e->recurid == CT_NONE) {
				/* the whole series moves by as much as this occurrence did */
				CTIME shift = n->start - act, len = n->end - n->start;
				n->start = m->start + shift;
				n->end = n->start + len;
			}
			if (how == SAVE_ALL && e->recurid != CT_NONE) {
				/* edit the series from an exception: take the master's times */
				EVENT *mm = ev_dup(m);
				if (mm) {
					ev_set(&mm->summary, n->summary);
					ev_set(&mm->location, n->location);
					ev_set(&mm->notes, n->notes);
					mm->alarm = n->alarm;
					ev_free(n);
					n = mm;
				}
			}
		}
		busy(1);
		ok = ev_save(n, ci, how, occ_rep);
		busy(0);
		ev_free(n);
		if (!ok) {
			alert(1, "[1][Couldn't save the event:|%s][ OK ]", cal_err);
			continue;
		}
		last_cal = (short)ci;
		views_select_none();
		return 1;
	}
}

/* a drag: the occurrence at occ_start (view's wall time) to new_start,
   its length kept; a repeating one asks, as the editor does */
int editor_move(EVENT *e, CTIME occ_start, CTIME orig, CTIME new_start)
{
	const TZONE *vz = view_zone();
	EVENT *m = e->res >= 0 ? ev_master(e) : 0, *n;
	CTIME act = ev_from_wall(e, occ_start, vz), to = ev_from_wall(e, new_start, vz), len = e->end - e->start;
	CTIME occ_rep = orig != CT_NONE ? orig : e->recurid != CT_NONE ? e->recurid : act;
	int how = SAVE_ALL, ok, first = m && occ_rep == m->start && e == m;
	if (!cal_writable(&cals[e->cal]) || to == act)
		return 0;
	if (m && m->rr) {
		how = ask_how(first);
		if (how < 0)
			return 0;
	}
	n = ev_dup(how == SAVE_ALL && m && m->rr && e->recurid == CT_NONE ? m : e);
	if (!n)
		return 0;
	if (how == SAVE_ALL && m && m->rr) {
		/* the whole series moves by as much as this occurrence did */
		n->start = m->start + (to - act);
		n->end = n->start + (m->end - m->start);
	} else {
		n->start = to;
		n->end = to + len;
	}
	/* a weekly series follows its occurrence to the new weekday */
	if (how != SAVE_THIS && n->rr && n->rr->freq == F_WEEKLY && n->rr->nbyday == 1 && n->rr->byday[0] < 8 &&
	    ct_days(new_start) != ct_days(occ_start))
		n->rr->byday[0] = (short)wday_of(ct_days(new_start));
	busy(1);
	ok = ev_save(n, e->cal, how, occ_rep);
	busy(0);
	ev_free(n);
	if (!ok)
		alert(1, "[1][Couldn't move the event:|%s][ OK ]", cal_err);
	return ok;
}

int editor_open(EVENT *e, CTIME occ_start, CTIME orig)
{
	return run_editor(e, occ_start, orig, 0);
}

static const char *new_title;		/* text dropped: the new event's title */

/* text dropped on the window: a new event with it as its title (its first
   line) */
void editor_new_titled(long day, const char *text)
{
	char t[200];
	str_copy(t, text, sizeof(t));
	clip_line(t);
	new_title = t;
	editor_new(day, -1, 0);
	new_title = 0;
}

void editor_new(long day, short minute, short allday)
{
	EVENT *e = ev_new();
	CTIME start;
	int i;
	if (!e)
		return;
	/* which calendar: the last one used, else the first that takes events */
	e->cal = -1;
	if (last_cal >= 0 && last_cal < ncals && cal_writable(&cals[last_cal]) && cals[last_cal].visible)
		e->cal = last_cal;
	for (i = 0; i < ncals && e->cal < 0; i++)
		if (cal_writable(&cals[i]) && cals[i].visible)
			e->cal = (short)i;
	for (i = 0; i < ncals && e->cal < 0; i++)
		if (cal_writable(&cals[i]))
			e->cal = (short)i;
	if (new_title && *new_title)
		ev_set(&e->summary, new_title);
	if (e->cal < 0) {
		ev_free(e);
		alert(1, "[1][There is no calendar you can add events to yet.|Add an iCloud or other account, or an On My Atari calendar, from the \xF0 menu.][ OK ]");
		return;
	}
	if (minute < 0) {
		/* the next full hour today, 9:00 on other days */
		CTIME now = now_view();
		minute = day == ct_days(now) ? (short)(((now - ct_day_start(now)) / 60 + 1) * 60) : 9 * 60;
		if (minute > 23 * 60)
			minute = 23 * 60;
	}
	e->allday = (unsigned char)allday;
	if (allday) {
		e->start = day * MIN_PER_DAY;
		e->end = e->start + MIN_PER_DAY;
		start = e->start;
		e->alarm = opt.alarm_default >= 0 ? -540 : -1;
	} else {
		start = day * MIN_PER_DAY + minute;
		e->tz = view_zone();
		e->start = tz_to_utc(e->tz, start);
		e->end = e->start + (opt.duration > 0 ? opt.duration : 60);
		e->alarm = opt.alarm_default;
	}
	if (run_editor(e, start, CT_NONE, 1))
		after_change();
	ev_free(e);
}
