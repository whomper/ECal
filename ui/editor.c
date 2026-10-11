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
#include "../src/util.h"

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

enum { R_NONE, R_DAY, R_WEEKDAYS, R_WEEK, R_2WEEKS, R_MONTH, R_YEAR, R_CUSTOM };
static const char *repeat_items[8];
static const char *const end_items[] = { "  Never", "  On date", "  After" };

static int rule_kind(const RRULE *r, short wkst)
{
	if (!r)
		return R_NONE;
	if (r->nbymd || r->bymonth || r->nbysetpos)
		return R_CUSTOM;
	if (r->freq == F_DAILY && r->interval == 1 && !r->nbyday)
		return R_DAY;
	if (r->freq == F_WEEKLY && r->nbyday == 5) {
		/* Sunday to Thursday, or Monday to Friday */
		int i, mask = 0;
		for (i = 0; i < 5; i++)
			mask |= 1 << ((r->byday[i] % 8 + 8) % 8);
		if (mask == 0x1f || mask == 0x3e)
			return R_WEEKDAYS;
	}
	if (r->freq == F_WEEKLY && r->nbyday <= 1 && r->interval <= 2)
		return r->interval == 2 ? R_2WEEKS : R_WEEK;
	if (r->freq == F_MONTHLY && r->interval == 1 && !r->nbyday)
		return R_MONTH;
	if (r->freq == F_YEARLY && r->interval == 1 && !r->nbyday)
		return R_YEAR;
	(void)wkst;
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
	}
}

/* ---- the dialog ---- */

enum {
	L_TITLE, F_TITLE, L_LOC, F_LOC, F_ALLDAY, L_CAL, F_CAL,
	L_START, F_SDATE, F_STIME, L_TZ, F_TZ, L_END, F_EDATE, F_ETIME,
	L_REP, F_REP, F_REPEND, F_REPVAL, L_ALERT, F_ALERT, L_NOTES, F_NOTES, L_URL,
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
} ED;

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
	int rep = x->f[F_REP].val != R_NONE, e = x->f[F_REPEND].val;
	x->f[F_REPEND].flags = rep && !x->readonly ? 0 : FF_DISABLED;
	x->f[F_REPVAL].flags = (rep && e && !x->readonly ? 0 : FF_DISABLED) | FF_LTR;
	x->f[F_REPVAL].w = e == 2 ? 4 : 10;
}

static char repval[16];

static void changed(DLG *d, short i)
{
	ED *x = d->ud;
	if (i == F_ALLDAY) {
		set_allday(d, x);
		dlg_redraw_field(d, F_STIME);
		dlg_redraw_field(d, F_ETIME);
		dlg_redraw_field(d, F_TZ);
		dlg_redraw_field(d, F_ALERT);
	} else if (i == F_REP || i == F_REPEND) {
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

static int ask_how(int first)
{
	short b;
	if (first)
		return SAVE_ALL;
	b = alert(1, "[2][This is a repeating event.|Change only this occurrence,|or this and all future ones?][Only this|All future|Cancel]");
	return b == 1 ? SAVE_THIS : b == 2 ? SAVE_FUTURE : -1;
}

/* e: the event (NULL for a new one, built from day/minutes); occ_start:
   the occurrence clicked, in the view's wall time */
static int run_editor(EVENT *e, CTIME occ_start, int is_new)
{
	static char title[200], loc[120], sdate[16], stime[12], edate[16], etime[12], notes[1200];
	ED x;
	DLG d;
	EVENT *m = e->res >= 0 ? ev_master(e) : 0;
	const TZONE *vz = view_zone(), *shown_z;
	CTIME occ_rep = ev_from_wall(e, occ_start, vz), dur = e->end - e->start, ws, we;
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
		ws = tz_to_local(shown_z, occ_rep);
		we = tz_to_local(shown_z, occ_rep + dur);
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
		static char custom[80];
		int kind = rule_kind(m ? m->rr : e->rr, opt.weekstart);
		repeat_items[R_NONE] = "  None";
		repeat_items[R_DAY] = "  Every day";
		repeat_items[R_WEEKDAYS] = opt.weekstart == 0 ? "  Every weekday (Sun-Thu)" : "  Every weekday (Mon-Fri)";
		repeat_items[R_WEEK] = "  Every week";
		repeat_items[R_2WEEKS] = "  Every 2 weeks";
		repeat_items[R_MONTH] = "  Every month";
		repeat_items[R_YEAR] = "  Every year";
		if (kind == R_CUSTOM) {
			char desc[70];
			rrule_describe(m ? m->rr : e->rr, desc, sizeof(desc));
			snprintf(custom, sizeof(custom), "  %s", desc);
			repeat_items[R_CUSTOM] = custom;
		}
		put(&x.f[L_REP], FT_LABEL, 0, 0, row, 0, "Repeat:");
		put(&x.f[F_REP], FT_POPUP, 0, 11, row, 26, 0);
		x.f[F_REP].items = repeat_items;
		x.f[F_REP].nitems = kind == R_CUSTOM ? 8 : 7;
		x.f[F_REP].val = (short)kind;
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
	}
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
		/* repeat */
		{
			int kind = x.f[F_REP].val;
			const RRULE *old = m ? m->rr : e->rr;
			int same_kind = kind == rule_kind(old, opt.weekstart);
			if (kind == R_NONE) {
				free(n->rr);
				n->rr = 0;
			} else if (kind != R_CUSTOM && !same_kind) {
				if (!n->rr)
					n->rr = malloc(sizeof(RRULE));
				if (n->rr)
					make_rule(n->rr, kind, sday);
			} else if (old && !n->rr) {
				n->rr = malloc(sizeof(RRULE));
				if (n->rr)
					*n->rr = *old;
			}
			if (n->rr) {
				long until;
				n->rr->count = 0;
				n->rr->until = CT_NONE;
				if (x.f[F_REPEND].val == 2)
					n->rr->count = atol(repval) > 0 ? atol(repval) : 1;
				else if (x.f[F_REPEND].val == 1 && parse_date(repval, &until))
					n->rr->until = n->allday ? until * MIN_PER_DAY : until * MIN_PER_DAY + MIN_PER_DAY - 1;
				/* a weekly rule follows the event to its new weekday */
				if (n->rr->freq == F_WEEKLY && n->rr->nbyday == 1 && (kind == R_WEEK || kind == R_2WEEKS))
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
			int rule_changed = x.f[F_REP].val != rule_kind(m->rr, opt.weekstart) ||
					   (n->rr && m->rr && (n->rr->count != m->rr->count || n->rr->until != m->rr->until));
			int first = occ_rep == m->start && e == m;
			how = rule_changed ? (first ? SAVE_ALL : SAVE_FUTURE) : ask_how(first);
			if (how < 0) {
				ev_free(n);
				continue;
			}
			if (how == SAVE_ALL && e->recurid == CT_NONE) {
				/* the whole series moves by as much as this occurrence did */
				CTIME shift = n->start - occ_rep, len = n->end - n->start;
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

int editor_open(EVENT *e, CTIME occ_start, long day, short hour, short minutes)
{
	(void)day;
	(void)hour;
	(void)minutes;
	return run_editor(e, occ_start, 0);
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
	if (run_editor(e, start, 1))
		after_change();
	ev_free(e);
}
