/*
 * ics.c - iCalendar in and out, and repeating events.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "ics.h"
#include "charset.h"
#include "plat.h"
#include "version.h"

CTIME ics_now_utc;
char ics_calname[64];


/* ---------------- events ---------------- */

EVENT *ev_new(void)
{
	EVENT *e = calloc(1, sizeof(EVENT));
	if (e) {
		e->recurid = CT_NONE;
		e->alarm = -1;
		e->res = -1;
	}
	return e;
}

void ev_free(EVENT *e)
{
	if (!e)
		return;
	free(e->uid);
	free(e->summary);
	free(e->location);
	free(e->notes);
	free(e->url);
	free(e->rr);
	free(e->exdate);
	free(e->rdate);
	free(e->rdate_end);
	free(e->exrule);
	free(e);
}

static char *dup0(const char *s)
{
	return s ? strdup(s) : 0;
}

EVENT *ev_dup(const EVENT *s)
{
	EVENT *e = malloc(sizeof(EVENT));
	if (!e)
		return 0;
	*e = *s;
	e->uid = dup0(s->uid);
	e->summary = dup0(s->summary);
	e->location = dup0(s->location);
	e->notes = dup0(s->notes);
	e->url = dup0(s->url);
	if (s->rr) {
		e->rr = malloc(sizeof(RRULE));
		if (e->rr)
			*e->rr = *s->rr;
	}
	e->exdate = e->rdate = e->rdate_end = 0;
	e->exrule = 0;
	if (s->nexdate) {
		e->exdate = malloc(s->nexdate * sizeof(CTIME));
		if (e->exdate)
			memcpy(e->exdate, s->exdate, s->nexdate * sizeof(CTIME));
		else
			e->nexdate = 0;
	}
	if (s->nrdate) {
		e->rdate = malloc(s->nrdate * sizeof(CTIME));
		e->rdate_end = malloc(s->nrdate * sizeof(CTIME));
		if (e->rdate && e->rdate_end) {
			memcpy(e->rdate, s->rdate, s->nrdate * sizeof(CTIME));
			if (s->rdate_end)
				memcpy(e->rdate_end, s->rdate_end, s->nrdate * sizeof(CTIME));
			else {
				int i;
				for (i = 0; i < s->nrdate; i++)
					e->rdate_end[i] = CT_NONE;
			}
		} else {
			e->nrdate = 0;
		}
	}
	if (s->nexrule) {
		e->exrule = malloc(s->nexrule * sizeof(RRULE));
		if (e->exrule)
			memcpy(e->exrule, s->exrule, s->nexrule * sizeof(RRULE));
		else
			e->nexrule = 0;
	}
	return e;
}

void ev_set(char **field, const char *s)
{
	free(*field);
	*field = s && *s ? strdup(s) : 0;
}

/* ---------------- text ---------------- */

char *ics_unescape(const char *s, long n)
{
	char *tmp = malloc(n + 1), *out;
	long i, o = 0, outlen;
	if (!tmp)
		return 0;
	for (i = 0; i < n; i++) {
		if (s[i] == '\\' && i + 1 < n) {
			char c = s[++i];
			tmp[o++] = (c == 'n' || c == 'N') ? '\n' : c;
		} else {
			tmp[o++] = s[i];
		}
	}
	tmp[o] = 0;
	out = cs_to_atari(tmp, o, CS_UTF8, &outlen);
	free(tmp);
	if (out) {
		/* lines end in \n only, and no trailing blanks */
		long r = 0, w = 0;
		for (r = 0; r < outlen; r++)
			if (out[r] != '\r')
				out[w++] = out[r];
		while (w && (out[w - 1] == ' ' || out[w - 1] == '\n'))
			w--;
		out[w] = 0;
	}
	return out;
}

void ics_escape(SBUF *b, const char *atari)
{
	long n;
	char *u = atari_to_utf8(atari, (long)strlen(atari), &n), *p;
	if (!u)
		return;
	for (p = u; *p; p++) {
		if (*p == '\\' || *p == ';' || *p == ',') {
			sb_addc(b, '\\');
			sb_addc(b, *p);
		} else if (*p == '\n') {
			sb_adds(b, "\\n");
		} else if (*p != '\r') {
			sb_addc(b, *p);
		}
	}
	free(u);
}

/* ---------------- durations and rules ---------------- */

int ics_duration(const char *s, long *minutes)
{
	long v = 0, n = 0;
	int neg = 0, time = 0, any = 0;
	while (*s == ' ')
		s++;
	if (*s == '+' || *s == '-')
		neg = *s++ == '-';
	if (*s++ != 'P')
		return 0;
	for (; *s; s++) {
		if (*s >= '0' && *s <= '9') {
			n = n * 10 + (*s - '0');
			continue;
		}
		switch (*s) {
		case 'W': v += n * 7 * MIN_PER_DAY; break;
		case 'D': v += n * MIN_PER_DAY; break;
		case 'T': time = 1; break;
		case 'H': v += n * 60; break;
		case 'M': if (time) v += n; break;
		case 'S': v += n / 60; break;
		default: return 0;
		}
		any = 1;
		n = 0;
	}
	*minutes = neg ? -v : v;
	return any;
}

/* ---------------- the parser ---------------- */

#define LINE_MAX_LEN 8192

typedef struct {
	char tzid[40];
	char value[32];
	int kind;			/* ct_parse kind */
	int date;			/* VALUE=DATE */
} TVAL;

typedef struct {
	short off_to, off_from;
	TZCHANGE ch;
	CTIME start;
	int rule;			/* has a usable RRULE */
} TZSUB;

struct ICSP {
	ICS_CB cb;
	void *ud;
	CTIME from, to;
	SBUF line;
	int skip;			/* depth inside components we ignore */
	int in_event, in_alarm, in_tz, in_sub;
	EVENT *ev;
	TVAL start, end, recur;
	int have_start, have_end, have_recur, have_dur;
	long dur;
	char rrule[300];
	SBUF exd;			/* "tzid\tvalues\n" for each EXDATE */
	SBUF rd;			/* the same for RDATE */
	SBUF exr;			/* EXRULE values, one per line */
	int range_future;		/* RECURRENCE-ID;RANGE=THISANDFUTURE */
	char trigger[40];
	int alarm_none;
	/* VTIMEZONE */
	char tzname[40];
	TZSUB sub, std, dl;
	int sub_dl, have_std, have_dl;
	int count;
};

ICSP *ics_begin(ICS_CB cb, void *ud, CTIME from, CTIME to)
{
	ICSP *p = calloc(1, sizeof(ICSP));
	if (!p)
		return 0;
	p->cb = cb;
	p->ud = ud;
	p->from = from;
	p->to = to;
	sb_init(&p->line);
	sb_init(&p->exd);
	sb_init(&p->rd);
	sb_init(&p->exr);
	ics_calname[0] = 0;
	return p;
}

/* name, parameters and value of one content line */
static int split_line(char *s, char **name, char **params, char **value)
{
	char *p = s;
	int q = 0;
	*name = s;
	*params = 0;
	while (*p && *p != ';' && *p != ':')
		p++;
	if (!*p)
		return 0;
	if (*p == ';') {
		*p++ = 0;
		*params = p;
		while (*p && (q || *p != ':')) {
			if (*p == '"')
				q = !q;
			p++;
		}
		if (!*p)
			return 0;
	}
	*p++ = 0;
	*value = p;
	for (p = *name; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return 1;
}

/* the value of parameter key in params ("TZID=Asia/Jerusalem;VALUE=DATE") */
static void param(const char *params, const char *key, char *out, int size)
{
	int kl = (int)strlen(key);
	out[0] = 0;
	while (params && *params) {
		if (!strncasecmp(params, key, kl) && params[kl] == '=') {
			const char *v = params + kl + 1;
			int o = 0, q = 0;
			if (*v == '"') {
				q = 1;
				v++;
			}
			while (*v && o < size - 1 && (q ? *v != '"' : (*v != ';' && *v != ':')))
				out[o++] = *v++;
			out[o] = 0;
			return;
		}
		while (*params && *params != ';') {
			if (*params == '"') {
				params++;
				while (*params && *params != '"')
					params++;
			}
			if (*params)
				params++;
		}
		if (*params == ';')
			params++;
	}
}

static void tval(TVAL *t, const char *params, const char *value)
{
	char v[16];
	param(params, "TZID", t->tzid, sizeof(t->tzid));
	param(params, "VALUE", v, sizeof(v));
	str_copy(t->value, value, sizeof(t->value));
	t->date = !strcasecmp(v, "DATE") || (strlen(value) == 8);
}

/* a time value -> the event's representation: UTC for zoned and UTC
   times, the wall clock for floating ones, 00:00 for dates. *tz and
   *floating say which it was (may be NULL) */
static int resolve(const TVAL *t, CTIME *out, const TZONE **tz, int *floating)
{
	CTIME w;
	int k = ct_parse(t->value, &w);
	const TZONE *z = 0;
	if (!k)
		return 0;
	if (floating)
		*floating = 0;
	if (k == 1 || t->date) {
		*out = ct_days(w) * MIN_PER_DAY;
		if (tz)
			*tz = 0;
		return 1;
	}
	if (k == 3) {
		*out = w;
		if (tz)
			*tz = tz_utc();
		return 2;
	}
	if (t->tzid[0])
		z = tz_find(t->tzid);
	if (tz)
		*tz = z;
	if (!z) {
		if (floating)
			*floating = 1;
		*out = w;
		return 2;
	}
	*out = tz_to_utc(z, w);
	return 2;
}

/* UNTIL in the event's own wall time, inclusive */
static void fix_until(const EVENT *e, RRULE *r)
{
	if (r->until == CT_NONE)
		return;
	if (r->until_kind == 1)
		r->until = r->until + (e->allday ? 0 : MIN_PER_DAY - 1);
	else if (r->until_kind == 3 && e->tz && !e->allday)
		r->until = tz_to_local(e->tz, r->until);
}

/* EXDATE or RDATE lines ("zone\tvalue,value...\n") -> times in the
   event's representation; ends: for RDATE periods ("start/end" or
   "start/duration"), else CT_NONE. Returns how many */
static int date_list(EVENT *e, SBUF *b, CTIME **out, CTIME **ends)
{
	char *s = b->s;
	int max = 0, n = 0;
	*out = 0;
	if (ends)
		*ends = 0;
	if (!b->len)
		return 0;
	while (*s) {
		char *nl = strchr(s, '\n'), *tab = strchr(s, '\t'), *v;
		if (!nl)
			break;
		*nl = 0;
		if (tab && tab < nl) {
			*tab = 0;
			for (v = tab + 1; *v;) {
				TVAL t;
				char *c = strchr(v, ','), *slash;
				CTIME x, end = CT_NONE;
				if (c)
					*c = 0;
				slash = strchr(v, '/');
				if (slash)
					*slash = 0;
				memset(&t, 0, sizeof(t));
				str_copy(t.tzid, s, sizeof(t.tzid));
				str_copy(t.value, v, sizeof(t.value));
				t.date = strlen(v) == 8;
				if (!t.tzid[0] && e->tz && !e->allday && !strchr(v, 'Z'))
					str_copy(t.tzid, e->tz->name, sizeof(t.tzid));
				if (resolve(&t, &x, 0, 0)) {
					if (slash) {
						long dur;
						TVAL te = t;
						str_copy(te.value, slash + 1, sizeof(te.value));
						if (slash[1] == 'P' || slash[1] == '+' || slash[1] == '-') {
							if (ics_duration(slash + 1, &dur))
								end = x + dur;
						} else if (!resolve(&te, &end, 0, 0)) {
							end = CT_NONE;
						}
					}
					if (n >= max) {
						CTIME *nv, *ne = 0;
						max = max ? max * 2 : 8;
						nv = realloc(*out, max * sizeof(CTIME));
						if (!nv)
							break;
						*out = nv;
						if (ends) {
							ne = realloc(*ends, max * sizeof(CTIME));
							if (!ne)
								break;
							*ends = ne;
						}
					}
					(*out)[n] = x;
					if (ends)
						(*ends)[n] = end;
					n++;
				}
				if (!c)
					break;
				v = c + 1;
			}
		}
		s = nl + 1;
	}
	return n;
}

static void finish_event(ICSP *p)
{
	EVENT *e = p->ev;
	const TZONE *z = 0;
	int fl = 0, k;
	p->ev = 0;
	if (!e)
		return;
	if (!p->have_start || !(k = resolve(&p->start, &e->start, &z, &fl))) {
		ev_free(e);
		return;
	}
	e->allday = k == 1;
	e->tz = z;
	e->floating = (unsigned char)fl;
	if (p->have_end) {
		const TZONE *z2 = 0;
		int fl2;
		TVAL t = p->end;
		/* an end without a zone of its own is in the start's */
		if (!t.tzid[0] && k == 2 && z && !strchr(t.value, 'Z'))
			str_copy(t.tzid, z->name, sizeof(t.tzid));
		if (!resolve(&t, &e->end, &z2, &fl2))
			e->end = e->start;
	} else if (p->have_dur) {
		e->end = e->start + p->dur;
	} else {
		e->end = e->allday ? e->start + MIN_PER_DAY : e->start;
	}
	if (e->end < e->start)
		e->end = e->start;
	if (e->allday && e->end == e->start)
		e->end = e->start + MIN_PER_DAY;
	if (p->have_recur && !resolve(&p->recur, &e->recurid, 0, 0))
		e->recurid = CT_NONE;
	if (p->rrule[0] && e->recurid == CT_NONE) {
		RRULE r;
		if (rrule_parse(p->rrule, &r) && (e->rr = malloc(sizeof(RRULE)))) {
			fix_until(e, &r);
			*e->rr = r;
		}
	}
	e->nexdate = (short)date_list(e, &p->exd, &e->exdate, 0);
	e->nrdate = (short)date_list(e, &p->rd, &e->rdate, &e->rdate_end);
	if (p->exr.len) {
		char *line = p->exr.s;
		while (*line) {
			char *nl = strchr(line, '\n');
			RRULE r, *n;
			if (!nl)
				break;
			*nl = 0;
			if (rrule_parse(line, &r) && (n = realloc(e->exrule, (e->nexrule + 1) * sizeof(RRULE)))) {
				fix_until(e, &r);
				e->exrule = n;
				e->exrule[e->nexrule++] = r;
			}
			line = nl + 1;
		}
	}
	e->range_future = (unsigned char)(e->recurid != CT_NONE && p->range_future);
	if (p->trigger[0] && !p->alarm_none) {
		long m;
		/* before the start, or (all-day events) during the day */
		if (ics_duration(p->trigger, &m) && m > -40L * MIN_PER_DAY && m < MIN_PER_DAY && m != 1)
			e->alarm = (short)-m;
	}
	/* outside the window, and not repeating: not needed */
	if (!e->rr && p->from != CT_NONE && e->recurid == CT_NONE &&
	    (e->end < p->from - 2 * MIN_PER_DAY || e->start > p->to + 2 * MIN_PER_DAY)) {
		ev_free(e);
		return;
	}
	if (!e->uid) {
		char u[48];
		ics_new_uid(u, sizeof(u));
		e->uid = strdup(u);
	}
	p->count++;
	if (!p->cb(e, p->ud))
		p->cb = 0;
}

/* VTIMEZONE: STANDARD and DAYLIGHT with yearly rules -> a TZONE */
static void sub_rule(TZSUB *s, const char *rr)
{
	RRULE r;
	DT d;
	int ord, w;
	ct_split(s->start, &d);
	if (!rrule_parse(rr, &r) || r.freq != F_YEARLY)
		return;
	s->ch.mon = (unsigned char)(r.bymonth ? 0 : d.mon);
	for (ord = 0; ord < 12; ord++)
		if (r.bymonth & (1 << ord)) {
			s->ch.mon = (unsigned char)(ord + 1);
			break;
		}
	s->ch.min = (short)(d.hour * 60 + d.min);
	s->ch.utc = 0;
	if (!r.nbyday)
		return;
	rr_byday(r.byday[0], &ord, &w);
	s->ch.wday = (unsigned char)w;
	if (ord < 0)
		s->ch.day = 0;
	else if (ord > 0)
		s->ch.day = (signed char)((ord - 1) * 7 + 1);
	else if (r.nbymd) {
		int i, lo = 31;
		for (i = 0; i < r.nbymd; i++)
			if (r.bymd[i] > 0 && r.bymd[i] < lo)
				lo = r.bymd[i];
		s->ch.day = (signed char)lo;
	} else {
		return;
	}
	s->rule = 1;
}

static int parse_off(const char *v)
{
	int sign = 1, h, m;
	if (*v == '-' || *v == '+')
		sign = *v++ == '-' ? -1 : 1;
	if (strlen(v) < 4)
		return 0;
	h = (v[0] - '0') * 10 + (v[1] - '0');
	m = (v[2] - '0') * 10 + (v[3] - '0');
	return sign * (h * 60 + m);
}

static void finish_tz(ICSP *p)
{
	TZONE z;
	if (!p->tzname[0] || tz_find_builtin(p->tzname) || !p->have_std)
		return;
	memset(&z, 0, sizeof(z));
	str_copy(z.name, p->tzname, sizeof(z.name));
	z.std = p->std.off_to;
	if (p->have_dl && p->std.rule && p->dl.rule) {
		z.dst = (short)(p->dl.off_to - p->std.off_to);
		z.on = p->dl.ch;
		z.off = p->std.ch;
	}
	tz_learn(&z);
}

static void logical_line(ICSP *p, char *s)
{
	char *name, *params, *value;
	if (!*s || !split_line(s, &name, &params, &value))
		return;
	if (!strcmp(name, "BEGIN")) {
		if (p->skip) {
			p->skip++;
		} else if (!strcasecmp(value, "VCALENDAR")) {
			/* nothing to do */
		} else if (!strcasecmp(value, "VEVENT") && !p->in_event && !p->in_tz) {
			p->in_event = 1;
			p->ev = ev_new();
			p->have_start = p->have_end = p->have_recur = p->have_dur = 0;
			p->rrule[0] = p->trigger[0] = 0;
			p->alarm_none = 0;
			p->range_future = 0;
			sb_reset(&p->exd);
			sb_reset(&p->rd);
			sb_reset(&p->exr);
		} else if (!strcasecmp(value, "VALARM") && p->in_event && !p->in_alarm) {
			p->in_alarm = 1;
		} else if (!strcasecmp(value, "VTIMEZONE") && !p->in_event) {
			p->in_tz = 1;
			p->tzname[0] = 0;
			p->have_std = p->have_dl = 0;
		} else if (p->in_tz && !p->in_sub &&
			   (!strcasecmp(value, "STANDARD") || !strcasecmp(value, "DAYLIGHT"))) {
			p->in_sub = 1;
			p->sub_dl = !strcasecmp(value, "DAYLIGHT");
			memset(&p->sub, 0, sizeof(p->sub));
		} else {
			p->skip = 1;
		}
		return;
	}
	if (!strcmp(name, "END")) {
		if (p->skip) {
			p->skip--;
		} else if (p->in_alarm && !strcasecmp(value, "VALARM")) {
			p->in_alarm = 0;
		} else if (p->in_event && !strcasecmp(value, "VEVENT")) {
			p->in_event = 0;
			if (p->cb)
				finish_event(p);
			else {
				ev_free(p->ev);
				p->ev = 0;
			}
		} else if (p->in_sub && (!strcasecmp(value, "STANDARD") || !strcasecmp(value, "DAYLIGHT"))) {
			TZSUB *d = p->sub_dl ? &p->dl : &p->std;
			int *have = p->sub_dl ? &p->have_dl : &p->have_std;
			p->in_sub = 0;
			/* several of a kind (history): the latest wins */
			if (!*have || p->sub.start >= d->start) {
				*d = p->sub;
				*have = 1;
			}
		} else if (p->in_tz && !strcasecmp(value, "VTIMEZONE")) {
			p->in_tz = 0;
			finish_tz(p);
		}
		return;
	}
	if (p->skip)
		return;
	if (p->in_sub) {
		if (!strcmp(name, "TZOFFSETTO"))
			p->sub.off_to = (short)parse_off(value);
		else if (!strcmp(name, "TZOFFSETFROM"))
			p->sub.off_from = (short)parse_off(value);
		else if (!strcmp(name, "DTSTART"))
			ct_parse(value, &p->sub.start);
		else if (!strcmp(name, "RRULE"))
			sub_rule(&p->sub, value);
		return;
	}
	if (p->in_tz) {
		if (!strcmp(name, "TZID"))
			str_copy(p->tzname, value, sizeof(p->tzname));
		return;
	}
	if (p->in_alarm) {
		if (!strcmp(name, "TRIGGER") && !p->trigger[0]) {
			char rel[16];
			param(params, "RELATED", rel, sizeof(rel));
			if (strcasecmp(rel, "END"))
				str_copy(p->trigger, value, sizeof(p->trigger));
		} else if (!strcmp(name, "ACTION") && !strcasecmp(value, "NONE")) {
			p->alarm_none = 1;
		}
		return;
	}
	if (!p->in_event) {
		if (!strcmp(name, "X-WR-CALNAME")) {
			char *t = ics_unescape(value, (long)strlen(value));
			if (t) {
				str_copy(ics_calname, t, sizeof(ics_calname));
				free(t);
			}
		}
		return;
	}
	if (!p->ev)
		return;
	if (!strcmp(name, "UID")) {
		free(p->ev->uid);
		p->ev->uid = strdup(value);
	} else if (!strcmp(name, "SUMMARY") || !strcmp(name, "LOCATION") || !strcmp(name, "DESCRIPTION")) {
		char **f = name[0] == 'S' ? &p->ev->summary : name[0] == 'L' ? &p->ev->location : &p->ev->notes;
		long n = (long)strlen(value);
		if (f == &p->ev->notes && n > 3000)
			n = 3000;
		free(*f);
		*f = ics_unescape(value, n);
	} else if (!strcmp(name, "URL")) {
		free(p->ev->url);
		p->ev->url = strdup(value);
	} else if (!strcmp(name, "DTSTART")) {
		tval(&p->start, params, value);
		p->have_start = 1;
	} else if (!strcmp(name, "DTEND")) {
		tval(&p->end, params, value);
		p->have_end = 1;
	} else if (!strcmp(name, "DURATION")) {
		p->have_dur = ics_duration(value, &p->dur);
	} else if (!strcmp(name, "RECURRENCE-ID")) {
		char range[20];
		tval(&p->recur, params, value);
		p->have_recur = 1;
		param(params, "RANGE", range, sizeof(range));
		p->range_future = !strcasecmp(range, "THISANDFUTURE");
	} else if (!strcmp(name, "RRULE")) {
		str_copy(p->rrule, value, sizeof(p->rrule));
	} else if (!strcmp(name, "EXDATE") || !strcmp(name, "RDATE")) {
		char z[40];
		SBUF *b = name[0] == 'E' ? &p->exd : &p->rd;
		param(params, "TZID", z, sizeof(z));
		sb_adds(b, z);
		sb_addc(b, '\t');
		sb_adds(b, value);
		sb_addc(b, '\n');
	} else if (!strcmp(name, "EXRULE")) {
		sb_adds(&p->exr, value);
		sb_addc(&p->exr, '\n');
	} else if (!strcmp(name, "STATUS")) {
		p->ev->status = !strcasecmp(value, "CANCELLED") ? ST_CANCELLED :
				!strcasecmp(value, "TENTATIVE") ? ST_TENTATIVE : ST_CONFIRMED;
	} else if (!strcmp(name, "SEQUENCE")) {
		p->ev->seq = (short)atoi(value);
	}
}

static void flush_line(ICSP *p)
{
	if (p->line.len) {
		logical_line(p, p->line.s);
		sb_reset(&p->line);
	}
}

void ics_line(ICSP *p, const char *s, long n)
{
	if (n && s[n - 1] == '\r')
		n--;
	if (n && (s[0] == ' ' || s[0] == '\t')) {
		if (p->line.len + n < LINE_MAX_LEN)
			sb_add(&p->line, s + 1, n - 1);
		return;
	}
	flush_line(p);
	if (n > LINE_MAX_LEN)
		n = LINE_MAX_LEN;
	if (n)
		sb_add(&p->line, s, n);
}

int ics_end(ICSP *p)
{
	int n;
	flush_line(p);
	ev_free(p->ev);
	n = p->count;
	sb_free(&p->line);
	sb_free(&p->exd);
	sb_free(&p->rd);
	sb_free(&p->exr);
	free(p);
	return n;
}

int ics_parse(const char *text, long n, ICS_CB cb, void *ud, CTIME from, CTIME to)
{
	ICSP *p = ics_begin(cb, ud, from, to);
	long i = 0;
	if (!p)
		return 0;
	while (i < n) {
		const char *nl = memchr(text + i, '\n', n - i);
		long len = nl ? nl - (text + i) : n - i;
		ics_line(p, text + i, len);
		i += len + 1;
	}
	return ics_end(p);
}

int ics_parse_file(const char *path, ICS_CB cb, void *ud, CTIME from, CTIME to)
{
	static char buf[4096];
	SBUF part;
	ICSP *p;
	long r;
	int h = pf_open(path, PF_READ);
	if (h < 0)
		return -1;
	p = ics_begin(cb, ud, from, to);
	if (!p) {
		pf_close(h);
		return -1;
	}
	sb_init(&part);
	while ((r = pf_read(h, buf, sizeof(buf))) > 0) {
		long i = 0;
		while (i < r) {
			char *nl = memchr(buf + i, '\n', r - i);
			long len = nl ? nl - (buf + i) : r - i;
			if (!nl) {		/* the line goes on in the next block */
				if (part.len < LINE_MAX_LEN)
					sb_add(&part, buf + i, len);
				break;
			}
			if (part.len) {
				sb_add(&part, buf + i, len);
				ics_line(p, part.s, part.len);
				sb_reset(&part);
			} else {
				ics_line(p, buf + i, len);
			}
			i += len + 1;
		}
		if (pf_idle)
			pf_idle();
	}
	if (part.len)
		ics_line(p, part.s, part.len);
	sb_free(&part);
	pf_close(h);
	return ics_end(p);
}

/* ---------------- writing ---------------- */

void ics_new_uid(char *out, int size)
{
	unsigned char r[16];
	char hex[40];
	int i;
	pf_entropy(r, sizeof(r));
	for (i = 0; i < 16; i++)
		sprintf(hex + i * 2, "%02X", r[i] ^ (unsigned char)(ics_now_utc >> (i % 4 * 8)));
	snprintf(out, size, "%s@atari", hex);
}

/* one logical line, folded at 75 bytes without splitting a UTF-8 character */
static void out_line(SBUF *o, const char *s, long n)
{
	long col = 0, i;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (col >= 74 && (c & 0xC0) != 0x80) {
			sb_add(o, "\r\n ", 3);
			col = 1;
		}
		sb_addc(o, (char)c);
		col++;
	}
	sb_add(o, "\r\n", 2);
}

static void out_str(SBUF *o, const char *s)
{
	out_line(o, s, (long)strlen(s));
}

static void out_text(SBUF *o, const char *name, const char *atari)
{
	SBUF b;
	if (!atari || !*atari)
		return;
	sb_init(&b);
	sb_adds(&b, name);
	sb_addc(&b, ':');
	ics_escape(&b, atari);
	out_line(o, b.s, b.len);
	sb_free(&b);
}

/* DTSTART and friends, in the event's representation */
static void out_time(SBUF *o, const char *name, const EVENT *e, CTIME t)
{
	char v[24], line[120];
	if (e->allday) {
		ct_format_ics(t, 1, v, sizeof(v));
		snprintf(line, sizeof(line), "%s;VALUE=DATE:%s", name, v);
	} else if (e->floating || !e->tz) {
		ct_format_ics(t, 2, v, sizeof(v));
		snprintf(line, sizeof(line), "%s:%s", name, v);
	} else if (e->tz == tz_utc()) {
		ct_format_ics(t, 3, v, sizeof(v));
		snprintf(line, sizeof(line), "%s:%s", name, v);
	} else {
		ct_format_ics(tz_to_local(e->tz, t), 2, v, sizeof(v));
		snprintf(line, sizeof(line), "%s;TZID=%s:%s", name, e->tz->name, v);
	}
	out_str(o, line);
}

static void out_event(SBUF *o, const EVENT *e)
{
	char line[400], v[24];
	int i;
	out_str(o, "BEGIN:VEVENT");
	snprintf(line, sizeof(line), "UID:%s", e->uid ? e->uid : "");
	out_str(o, line);
	ct_format_ics(ics_now_utc, 3, v, sizeof(v));
	snprintf(line, sizeof(line), "DTSTAMP:%s", v);
	out_str(o, line);
	snprintf(line, sizeof(line), "LAST-MODIFIED:%s", v);
	out_str(o, line);
	if (e->seq) {
		snprintf(line, sizeof(line), "SEQUENCE:%d", e->seq);
		out_str(o, line);
	}
	out_text(o, "SUMMARY", e->summary ? e->summary : "");
	out_text(o, "LOCATION", e->location);
	out_text(o, "DESCRIPTION", e->notes);
	if (e->url && *e->url) {
		snprintf(line, sizeof(line), "URL:%s", e->url);
		out_str(o, line);
	}
	out_time(o, "DTSTART", e, e->start);
	out_time(o, "DTEND", e, e->end);
	if (e->recurid != CT_NONE)
		out_time(o, e->range_future ? "RECURRENCE-ID;RANGE=THISANDFUTURE" : "RECURRENCE-ID", e, e->recurid);
	if (e->rr && e->recurid == CT_NONE) {
		char rr[200];
		rrule_format(e->rr, e->floating ? 0 : e->tz, e->allday, rr, sizeof(rr));
		snprintf(line, sizeof(line), "RRULE:%s", rr);
		out_str(o, line);
	}
	for (i = 0; i < e->nexdate && e->recurid == CT_NONE; i++)
		out_time(o, "EXDATE", e, e->exdate[i]);
	for (i = 0; i < e->nrdate && e->recurid == CT_NONE; i++) {
		if (e->rdate_end && e->rdate_end[i] != CT_NONE) {
			char a[24], b[24];
			int kind = e->floating || !e->tz ? 2 : 3;	/* zoned times are kept in UTC */
			ct_format_ics(e->rdate[i], kind, a, sizeof(a));
			ct_format_ics(e->rdate_end[i], kind, b, sizeof(b));
			snprintf(line, sizeof(line), "RDATE;VALUE=PERIOD:%s/%s", a, b);
			out_str(o, line);
		} else {
			out_time(o, "RDATE", e, e->rdate[i]);
		}
	}
	for (i = 0; i < e->nexrule && e->recurid == CT_NONE; i++) {
		char rr[300];
		rrule_format(&e->exrule[i], e->floating ? 0 : e->tz, e->allday, rr, sizeof(rr));
		snprintf(line, sizeof(line), "EXRULE:%s", rr);
		out_str(o, line);
	}
	if (e->status == ST_TENTATIVE)
		out_str(o, "STATUS:TENTATIVE");
	else if (e->status == ST_CANCELLED)
		out_str(o, "STATUS:CANCELLED");
	if (e->alarm < -1) {
		/* during the day: an all-day event's "on the day at 9:00" */
		out_str(o, "BEGIN:VALARM");
		out_str(o, "ACTION:DISPLAY");
		out_str(o, "DESCRIPTION:Reminder");
		snprintf(line, sizeof(line), "TRIGGER:PT%dM", -e->alarm);
		out_str(o, line);
		out_str(o, "END:VALARM");
	} else if (e->alarm >= 0) {
		out_str(o, "BEGIN:VALARM");
		out_str(o, "ACTION:DISPLAY");
		out_str(o, "DESCRIPTION:Reminder");
		if (e->alarm % 1440 == 0 && e->alarm)
			snprintf(line, sizeof(line), "TRIGGER:-P%dD", e->alarm / 1440);
		else if (e->alarm % 60 == 0 && e->alarm)
			snprintf(line, sizeof(line), "TRIGGER:-PT%dH", e->alarm / 60);
		else
			snprintf(line, sizeof(line), "TRIGGER:-PT%dM", e->alarm);
		out_str(o, line);
		out_str(o, "END:VALARM");
	}
	out_str(o, "END:VEVENT");
}

static void out_tz(SBUF *o, const EVENT *e)
{
	char *buf;
	if (e->allday || e->floating || !e->tz || e->tz == tz_utc())
		return;
	buf = malloc(1200);
	if (!buf)
		return;
	tz_vtimezone(e->tz, buf, 1200);
	sb_adds(o, buf);
	free(buf);
}

/* unfold orig into logical lines; calls fn for each (NUL-terminated,
   may be modified) */
typedef void (*LINE_FN)(char *line, void *ud);
static void each_line(const char *orig, long n, LINE_FN fn, void *ud)
{
	SBUF l;
	long i = 0;
	sb_init(&l);
	while (i < n) {
		const char *nl = memchr(orig + i, '\n', n - i);
		long len = nl ? nl - (orig + i) : n - i;
		const char *s = orig + i;
		if (len && s[len - 1] == '\r')
			len--;
		if (len && (s[0] == ' ' || s[0] == '\t')) {
			sb_add(&l, s + 1, len - 1);
		} else {
			if (l.len)
				fn(l.s, ud);
			sb_reset(&l);
			if (len)
				sb_add(&l, s, len);
		}
		i += (nl ? len + (s[len] == '\r' ? 2 : 1) : len);
		if (!nl)
			break;
	}
	if (l.len)
		fn(l.s, ud);
	sb_free(&l);
}

typedef struct {
	SBUF *o;
	const EVENT *e;
	int depth;		/* inside a VEVENT */
	int master;		/* that VEVENT is the one being replaced */
	int alarm;		/* inside one of its VALARMs */
	int done;		/* our event has been written */
	int has_tz;		/* orig already defines our zone */
	CTIME drop;		/* exceptions to leave out (CT_NONE: none; 1: all) */
	int dropping;
	SBUF comp;		/* the current VEVENT, held until we know whether it stays */
	int is_exc;		/* it has a RECURRENCE-ID */
	CTIME exc_at;
	int mode;		/* 0 build, 1 drop exceptions */
} BUILD;

static int prop_is(const char *line, const char *name)
{
	int n = (int)strlen(name);
	return !strncasecmp(line, name, n) && (line[n] == ':' || line[n] == ';');
}

/* the RECURRENCE-ID of an exception line, in event representation */
static CTIME recur_of(const char *line)
{
	char buf[200], *name, *params, *value;
	TVAL t;
	CTIME x;
	str_copy(buf, line, sizeof(buf));
	if (!split_line(buf, &name, &params, &value))
		return CT_NONE;
	memset(&t, 0, sizeof(t));
	tval(&t, params, value);
	return resolve(&t, &x, 0, 0) ? x : CT_NONE;
}

static void build_line(char *line, void *ud)
{
	BUILD *b = ud;
	if (!strncasecmp(line, "BEGIN:VEVENT", 12)) {
		b->depth = 1;
		b->is_exc = 0;
		sb_reset(&b->comp);
		out_line(&b->comp, line, (long)strlen(line));
		return;
	}
	if (b->depth) {
		if (!strncasecmp(line, "BEGIN:VALARM", 12))
			b->alarm = 1;
		if (prop_is(line, "RECURRENCE-ID")) {
			b->is_exc = 1;
			b->exc_at = recur_of(line);
		}
		if (strncasecmp(line, "END:VEVENT", 10)) {
			out_line(&b->comp, line, (long)strlen(line));
			if (!strncasecmp(line, "END:VALARM", 10))
				b->alarm = 0;
			return;
		}
		/* the whole VEVENT is in comp: keep it, replace it or drop it */
		out_line(&b->comp, line, (long)strlen(line));
		b->depth = 0;
		if (b->mode == 1) {
			if (!(b->is_exc && (b->drop == 1 || b->exc_at == b->drop)))
				sb_add(b->o, b->comp.s, b->comp.len);
			return;
		}
		if (b->e->recurid == CT_NONE) {
			if (!b->is_exc && !b->done) {
				/* the master: ours instead, keeping what we don't edit */
				SBUF keep;
				char *s = b->comp.s;
				int in_alarm = 0;
				sb_init(&keep);
				out_event(b->o, b->e);
				/* re-insert unknown properties of the old master before its END */
				while (s && *s) {
					char *nl = strstr(s, "\r\n"), save;
					if (!nl)
						break;
					/* folded continuation lines belong to the previous one */
					while (nl[2] == ' ' || nl[2] == '\t') {
						char *n2 = strstr(nl + 2, "\r\n");
						if (!n2)
							break;
						nl = n2;
					}
					save = nl[2];
					nl[2] = 0;
					if (!strncasecmp(s, "BEGIN:VALARM", 12))
						in_alarm = 1;
					if (!in_alarm && strncasecmp(s, "BEGIN:VEVENT", 12) && strncasecmp(s, "END:VEVENT", 10) &&
					    !prop_is(s, "UID") && !prop_is(s, "DTSTAMP") && !prop_is(s, "LAST-MODIFIED") &&
					    !prop_is(s, "SEQUENCE") && !prop_is(s, "SUMMARY") && !prop_is(s, "LOCATION") &&
					    !prop_is(s, "DESCRIPTION") && !prop_is(s, "URL") && !prop_is(s, "DTSTART") &&
					    !prop_is(s, "DTEND") && !prop_is(s, "DURATION") && !prop_is(s, "RRULE") &&
					    !prop_is(s, "EXDATE") && !prop_is(s, "STATUS") && !prop_is(s, "CREATED") &&
					    !prop_is(s, "RDATE") && !prop_is(s, "EXRULE"))
						sb_adds(&keep, s);
					if (!strncasecmp(s, "END:VALARM", 10))
						in_alarm = 0;
					nl[2] = save;
					s = nl + 2;
				}
				if (keep.len) {
					/* before our END:VEVENT */
					b->o->len -= 12;
					sb_add(b->o, keep.s, keep.len);
					sb_adds(b->o, "END:VEVENT\r\n");
				}
				sb_free(&keep);
				b->done = 1;
				return;
			}
			sb_add(b->o, b->comp.s, b->comp.len);
			return;
		}
		/* writing an exception: an old one for the same start goes */
		if (b->is_exc && b->exc_at == b->e->recurid)
			return;
		sb_add(b->o, b->comp.s, b->comp.len);
		return;
	}
	if (b->mode == 0 && !strncasecmp(line, "END:VCALENDAR", 13)) {
		if (!b->done) {
			if (!b->has_tz)
				out_tz(b->o, b->e);
			out_event(b->o, b->e);
			b->done = 1;
		}
	}
	if (b->mode == 0 && !strncasecmp(line, "BEGIN:VTIMEZONE", 15))
		b->dropping = 0;
	out_line(b->o, line, (long)strlen(line));
}

char *ics_build(const EVENT *e, const char *orig, long origlen, long *outlen)
{
	SBUF o;
	sb_init(&o);
	if (!orig || !origlen) {
		out_str(&o, "BEGIN:VCALENDAR");
		out_str(&o, "VERSION:2.0");
		{
			char prodid[60];
			snprintf(prodid, sizeof(prodid), "PRODID:-//Atari Calendar %s//EN", cal_version);
			out_str(&o, prodid);
		}
		out_str(&o, "CALSCALE:GREGORIAN");
		out_tz(&o, e);
		out_event(&o, e);
		out_str(&o, "END:VCALENDAR");
	} else {
		BUILD b;
		memset(&b, 0, sizeof(b));
		b.o = &o;
		b.e = e;
		b.drop = CT_NONE;
		sb_init(&b.comp);
		if (e->tz && !e->allday && !e->floating && e->tz != tz_utc()) {
			/* does orig define the zone already? */
			char want[60];
			snprintf(want, sizeof(want), "TZID:%s", e->tz->name);
			b.has_tz = str_istr(orig, want) != 0;
			if (!b.has_tz) {
				/* insert ours after the VCALENDAR header */
				const char *v = str_istr(orig, "BEGIN:VCALENDAR");
				if (v) {
					const char *nl = strchr(v, '\n');
					long head = nl ? nl + 1 - orig : 0;
					SBUF tz;
					sb_init(&tz);
					out_tz(&tz, e);
					each_line(orig, head, build_line, &b);
					sb_add(&o, tz.s, tz.len);
					sb_free(&tz);
					b.has_tz = 1;
					each_line(orig + head, origlen - head, build_line, &b);
					goto done;
				}
			}
		}
		each_line(orig, origlen, build_line, &b);
done:
		sb_free(&b.comp);
	}
	if (outlen)
		*outlen = o.len;
	return o.s ? sb_steal(&o) : strdup("");
}

char *ics_drop_exceptions(const char *orig, long origlen, CTIME recurid, long *outlen)
{
	SBUF o;
	BUILD b;
	EVENT dummy;
	memset(&b, 0, sizeof(b));
	memset(&dummy, 0, sizeof(dummy));
	sb_init(&o);
	sb_init(&b.comp);
	b.o = &o;
	b.e = &dummy;
	b.mode = 1;
	b.drop = recurid == CT_NONE ? 1 : recurid;
	each_line(orig, origlen, build_line, &b);
	sb_free(&b.comp);
	if (outlen)
		*outlen = o.len;
	return o.s ? sb_steal(&o) : strdup("");
}
