/*
 * calstore.c - the calendars: settings, caches on disk, syncing with
 * CalDAV servers and subscriptions, occurrences for the views, and
 * saving changes.
 *
 * Each calendar has a cache file in CACHE\ next to CALENDAR.INF:
 *   CALnnnnn.DAT  CalDAV and On My Atari: every event's iCalendar text
 *                 as the server sent it, with its href and etag
 *   CALnnnnn.ICS  subscriptions: the feed as downloaded
 * so the calendar opens at once, offline too, and an event can be
 * written back without losing what this program doesn't understand.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include "calstore.h"
#include "caldav.h"
#include "http.h"
#include "conn.h"
#include "secret.h"
#include "plat.h"
#include "util.h"

OPT opt;
ACCOUNT accts[MAXACCT];
int naccts;
CALENDAR cals[MAXCAL];
int ncals;
char cal_err[200];
void (*cal_status)(const char *msg);
int store_lost_passwords;

static char inf_path[200], cache_dir[200];

static void status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void status(const char *fmt, ...)
{
	char buf[120];
	va_list ap;
	if (!cal_status)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	cal_status(buf);
}

static void status_clear(void)
{
	if (cal_status)
		cal_status("");
}

/* ---------------- time ---------------- */

/* looked up by name once, then again only when the setting changes: a
   name search over all the zones is slow on an 8 MHz ST and the views
   ask for the zone for every day they draw */
const TZONE *home_zone(void)
{
	static char name[40];
	static const TZONE *z;
	if (!z || strcmp(name, opt.home_tz)) {
		z = tz_find(opt.home_tz);
		str_copy(name, opt.home_tz, sizeof(name));
		if (!z)
			z = tz_utc();
	}
	return z;
}

const TZONE *view_zone(void)
{
	static char name[40];
	static const TZONE *z;
	if (!opt.tzsupport)
		return home_zone();
	if (!z || strcmp(name, opt.view_tz)) {
		z = tz_find(opt.view_tz);
		str_copy(name, opt.view_tz, sizeof(name));
		if (!z)
			z = home_zone();
	}
	return z;
}

CTIME now_utc(void)
{
	PFTIME t;
	pf_now(&t);
	return tz_to_utc(home_zone(), ct_make(t.year, t.mon, t.day, t.hour, t.min));
}

CTIME now_view(void)
{
	return tz_to_local(view_zone(), now_utc());
}

/* the stretch of time kept from the servers, UTC */
static void window(CTIME *from, CTIME *to)
{
	CTIME today = ct_day_start(now_view());
	*from = today - (CTIME)opt.past_days * MIN_PER_DAY;
	*to = today + (CTIME)opt.future_days * MIN_PER_DAY;
}

/* ---------------- settings ---------------- */

static void defaults(void)
{
	str_copy(opt.home_tz, "Asia/Jerusalem", sizeof(opt.home_tz));
	str_copy(opt.view_tz, "Asia/Jerusalem", sizeof(opt.view_tz));
	opt.gateport = 8180;
	opt.dsp = 1;
	opt.weekstart = 0;
	opt.h24 = 1;
	opt.hebdates = 1;
	opt.view = V_MONTH;
	opt.daystart = 8;
	opt.alarms = 1;
	opt.alarm_default = -1;
	opt.font_id = 1;
	opt.sidebar = 1;
	opt.past_days = 92;
	opt.future_days = 400;
	opt.duration = 60;
	opt.sync_open = 1;
}

static void set_opt(const char *k, const char *v)
{
#define S(name) if (!strcmp(k, #name)) { str_copy(opt.name, v, sizeof(opt.name)); return; }
#define I(name) if (!strcmp(k, #name)) { opt.name = (short)atoi(v); return; }
	S(home_tz) S(view_tz) S(gate)
	I(tzsupport) I(falcon) I(dsp) I(weekstart) I(h24) I(hebdates) I(view) I(daystart)
	I(alarms) I(alarm_default) I(log) I(font_id) I(font_pt) I(hebfont) I(win_x) I(win_y)
	I(win_w) I(win_h) I(sidebar) I(past_days) I(future_days) I(duration) I(refresh) I(sync_open)
	if (!strcmp(k, "gateport")) {
		opt.gateport = (unsigned short)atoi(v);
		return;
	}
#undef S
#undef I
}

static void set_acct(ACCOUNT *a, const char *k, const char *v)
{
	if (!strcmp(k, "name"))
		str_copy(a->name, v, sizeof(a->name));
	else if (!strcmp(k, "kind"))
		a->kind = (short)atoi(v);
	else if (!strcmp(k, "url"))
		str_copy(a->url, v, sizeof(a->url));
	else if (!strcmp(k, "user"))
		str_copy(a->user, v, sizeof(a->user));
	else if (!strcmp(k, "refresh"))
		a->refresh = (short)atoi(v);
	else if (!strcmp(k, "pass")) {
		if (!secret_decode(v, a->pass, sizeof(a->pass))) {
			a->pass[0] = 0;
			store_lost_passwords = 1;
		}
	}
}

static void set_cal(CALENDAR *c, const char *k, const char *v)
{
	if (!strcmp(k, "id"))
		c->id = (short)atoi(v);
	else if (!strcmp(k, "name"))
		str_copy(c->name, v, sizeof(c->name));
	else if (!strcmp(k, "url"))
		str_copy(c->url, v, sizeof(c->url));
	else if (!strcmp(k, "ctag"))
		str_copy(c->ctag, v, sizeof(c->ctag));
	else if (!strcmp(k, "etag"))
		str_copy(c->etag, v, sizeof(c->etag));
	else if (!strcmp(k, "lastmod"))
		str_copy(c->lastmod, v, sizeof(c->lastmod));
	else if (!strcmp(k, "color"))
		c->color = (short)(atoi(v) % NCOLORS);
	else if (!strcmp(k, "visible"))
		c->visible = (unsigned char)atoi(v);
	else if (!strcmp(k, "readonly"))
		c->readonly = (unsigned char)atoi(v);
	else if (!strcmp(k, "alarms"))
		c->alarms = (unsigned char)atoi(v);
	else if (!strcmp(k, "synced"))
		c->synced = atol(v);
}

void store_init(const char *dir)
{
	char key[200], *text, *line;
	long len;
	ACCOUNT *a = 0;
	CALENDAR *c = 0;
	defaults();
	str_copy(opt.workdir, dir, sizeof(opt.workdir));
	path_join(inf_path, sizeof(inf_path), dir, "CALENDAR.INF");
	path_join(cache_dir, sizeof(cache_dir), dir, "CACHE");
	pf_mkdir(cache_dir);
	path_join(key, sizeof(key), dir, "CALENDAR.KEY");
	secret_init(key);
	text = pf_load(inf_path, &len);
	if (!text)
		return;
	for (line = text; line && *line;) {
		char *nl = strchr(line, '\n'), *eq;
		if (nl)
			*nl = 0;
		if (nl && nl > line && nl[-1] == '\r')
			nl[-1] = 0;
		if (!strcmp(line, "[account]")) {
			a = naccts < MAXACCT ? &accts[naccts++] : 0;
			if (a)
				memset(a, 0, sizeof(*a));
			c = 0;
		} else if (!strcmp(line, "[calendar]")) {
			c = (a && ncals < MAXCAL) ? &cals[ncals++] : 0;
			if (c) {
				memset(c, 0, sizeof(*c));
				c->acct = (short)(a - accts);
				c->visible = 1;
				c->alarms = 1;
			}
		} else if ((eq = strchr(line, '='))) {
			*eq = 0;
			if (c)
				set_cal(c, line, eq + 1);
			else if (a)
				set_acct(a, line, eq + 1);
			else
				set_opt(line, eq + 1);
		}
		line = nl ? nl + 1 : 0;
	}
	free(text);
	str_copy(http_cfg.gate, opt.gate, sizeof(http_cfg.gate));
	http_cfg.gateport = opt.gateport;
	http_cfg.falcon = opt.falcon;
}

void store_save_settings(void)
{
	SBUF b;
	int i, j;
	char enc[200];
	sb_init(&b);
	sb_adds(&b, "; Calendar for the Atari - settings\r\n");
#define S(name) sb_printf(&b, #name "=%s\r\n", opt.name);
#define I(name) sb_printf(&b, #name "=%d\r\n", opt.name);
	S(home_tz) S(view_tz) S(gate)
	I(tzsupport) I(falcon) I(dsp) I(weekstart) I(h24) I(hebdates) I(view) I(daystart)
	I(alarms) I(alarm_default) I(log) I(font_id) I(font_pt) I(hebfont) I(win_x) I(win_y)
	I(win_w) I(win_h) I(sidebar) I(past_days) I(future_days) I(duration) I(refresh) I(sync_open)
	sb_printf(&b, "gateport=%u\r\n", opt.gateport);
#undef S
#undef I
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = &accts[i];
		sb_printf(&b, "[account]\r\nname=%s\r\nkind=%d\r\nurl=%s\r\nuser=%s\r\nrefresh=%d\r\n",
			  a->name, a->kind, a->url, a->user, a->refresh);
		if (a->pass[0]) {
			secret_encode(a->pass, enc, sizeof(enc));
			sb_printf(&b, "pass=%s\r\n", enc);
		}
		for (j = 0; j < ncals; j++) {
			CALENDAR *c = &cals[j];
			if (c->acct != i)
				continue;
			sb_printf(&b, "[calendar]\r\nid=%d\r\nname=%s\r\nurl=%s\r\nctag=%s\r\netag=%s\r\n"
				  "lastmod=%s\r\ncolor=%d\r\nvisible=%d\r\nreadonly=%d\r\nalarms=%d\r\nsynced=%ld\r\n",
				  c->id, c->name, c->url, c->ctag, c->etag, c->lastmod, c->color,
				  c->visible, c->readonly, c->alarms, (long)c->synced);
		}
	}
	pf_save(inf_path, b.s, b.len);
	sb_free(&b);
	str_copy(http_cfg.gate, opt.gate, sizeof(http_cfg.gate));
	http_cfg.gateport = opt.gateport;
	http_cfg.falcon = opt.falcon;
}

/* ---------------- accounts and calendars ---------------- */

ACCOUNT *acct_add(int kind)
{
	ACCOUNT *a;
	if (naccts >= MAXACCT)
		return 0;
	a = &accts[naccts++];
	memset(a, 0, sizeof(*a));
	a->kind = (short)kind;
	a->refresh = kind == AK_SUBSCR ? 60 : 0;
	return a;
}

static void cache_path(const CALENDAR *c, const char *ext, char *out, int size)
{
	char n[16];
	snprintf(n, sizeof(n), "CAL%05d.%s", c->id, ext);
	path_join(out, size, cache_dir, n);
}

static void cal_free_events(CALENDAR *c)
{
	int i;
	for (i = 0; i < c->nev; i++)
		ev_free(c->ev[i]);
	free(c->ev);
	c->ev = 0;
	c->nev = c->maxev = 0;
	for (i = 0; i < c->nres; i++) {
		free(c->res[i].href);
		free(c->res[i].etag);
	}
	free(c->res);
	c->res = 0;
	c->nres = c->maxres = 0;
}

void cal_delete(int i)
{
	char p[200];
	int k;
	CALENDAR *c = &cals[i];
	cache_path(c, "DAT", p, sizeof(p));
	pf_remove(p);
	cache_path(c, "ICS", p, sizeof(p));
	pf_remove(p);
	cal_free_events(c);
	for (k = i; k < ncals - 1; k++)
		cals[k] = cals[k + 1];
	ncals--;
	/* the events know their calendar by index */
	for (k = i; k < ncals; k++) {
		int e;
		for (e = 0; e < cals[k].nev; e++)
			cals[k].ev[e]->cal = (short)k;
	}
}

void acct_delete(int a)
{
	int i;
	for (i = ncals - 1; i >= 0; i--)
		if (cals[i].acct == a)
			cal_delete(i);
	for (i = 0; i < ncals; i++)
		if (cals[i].acct > a)
			cals[i].acct--;
	for (i = a; i < naccts - 1; i++)
		accts[i] = accts[i + 1];
	naccts--;
}

CALENDAR *cal_add(int acct, const char *name)
{
	CALENDAR *c;
	int id = 1, i, used[NCOLORS];
	if (ncals >= MAXCAL)
		return 0;
	for (i = 0; i < ncals; i++)
		if (cals[i].id >= id)
			id = cals[i].id + 1;
	memset(used, 0, sizeof(used));
	for (i = 0; i < ncals; i++)
		used[cals[i].color]++;
	c = &cals[ncals++];
	memset(c, 0, sizeof(*c));
	c->id = (short)id;
	c->acct = (short)acct;
	str_copy(c->name, name, sizeof(c->name));
	c->visible = 1;
	c->alarms = accts[acct].kind != AK_SUBSCR;
	/* the least used colour */
	for (i = 0; i < NCOLORS; i++)
		if (used[i] < used[c->color])
			c->color = (short)i;
	return c;
}

CALENDAR *cal_by_id(int id)
{
	int i;
	for (i = 0; i < ncals; i++)
		if (cals[i].id == id)
			return &cals[i];
	return 0;
}

int cal_writable(const CALENDAR *c)
{
	return c && !c->readonly && accts[c->acct].kind != AK_SUBSCR;
}

static DAVAUTH auth_of(int a)
{
	DAVAUTH d;
	d.user = accts[a].user[0] ? accts[a].user : 0;
	d.pass = accts[a].pass;
	return d;
}

/* #RRGGBB -> the nearest of our colours */
static short color_of(const char *hex)
{
	static const unsigned char pal[NCOLORS][3] = {
		{ 255, 59, 48 }, { 255, 149, 0 }, { 255, 204, 0 }, { 40, 205, 65 },
		{ 0, 122, 255 }, { 175, 82, 222 }, { 162, 132, 94 }, { 142, 142, 147 }
	};
	long r, g, b, best = 0x7fffffffL;
	short i, pick = 4;
	if (hex[0] != '#' || strlen(hex) < 7)
		return -1;
	r = strtol((char[]){ hex[1], hex[2], 0 }, 0, 16);
	g = strtol((char[]){ hex[3], hex[4], 0 }, 0, 16);
	b = strtol((char[]){ hex[5], hex[6], 0 }, 0, 16);
	for (i = 0; i < NCOLORS; i++) {
		long dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
		long d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < best) {
			best = d;
			pick = i;
		}
	}
	return pick;
}

int acct_discover(int a)
{
	DAVCAL *dc;
	int n, i, j, changed = 0;
	DAVAUTH au = auth_of(a);
	char seen[MAXCAL];
	status("Looking for the calendars of %s...", accts[a].name);
	if (!caldav_discover(accts[a].url, &au, &dc, &n, cal_err, sizeof(cal_err))) {
		str_copy(accts[a].err, cal_err, sizeof(accts[a].err));
		return 0;
	}
	accts[a].err[0] = 0;
	memset(seen, 0, sizeof(seen));
	for (i = 0; i < n; i++) {
		CALENDAR *c = 0;
		for (j = 0; j < ncals; j++)
			if (cals[j].acct == a && !strcmp(cals[j].url, dc[i].url))
				c = &cals[j];
		if (!c) {
			c = cal_add(a, dc[i].name);
			if (!c)
				break;
			str_copy(c->url, dc[i].url, sizeof(c->url));
			if (color_of(dc[i].color) >= 0)
				c->color = color_of(dc[i].color);
			changed++;
		} else if (strcmp(c->name, dc[i].name)) {
			str_copy(c->name, dc[i].name, sizeof(c->name));
			changed++;
		}
		c->readonly = (unsigned char)dc[i].readonly;
		seen[c - cals] = 1;
	}
	/* calendars deleted on the server */
	for (j = ncals - 1; j >= 0; j--)
		if (cals[j].acct == a && !seen[j]) {
			cal_delete(j);
			changed++;
		}
	free(dc);
	store_save_settings();
	return 1 + changed;
}

/* ---------------- the cache files ---------------- */

/* a buffered reader that knows where it is */
typedef struct {
	int h;
	char buf[2048];
	int pos, len;
	long off;		/* file offset of buf[pos] */
} FR;

static int fr_fill(FR *f)
{
	if (f->pos < f->len)
		return 1;
	f->len = (int)pf_read(f->h, f->buf, sizeof(f->buf));
	f->pos = 0;
	return f->len > 0;
}

static int fr_line(FR *f, char *out, int size)
{
	int o = 0;
	for (;;) {
		char c;
		if (!fr_fill(f))
			break;
		c = f->buf[f->pos++];
		f->off++;
		if (c == '\n') {
			out[o] = 0;
			return 1;
		}
		if (o < size - 1)
			out[o++] = c;
	}
	out[o] = 0;
	return o > 0;
}

static int fr_bytes(FR *f, char *out, long n)
{
	while (n > 0) {
		int k;
		if (!fr_fill(f))
			return 0;
		k = f->len - f->pos;
		if (k > n)
			k = (int)n;
		if (out) {
			memcpy(out, f->buf + f->pos, k);
			out += k;
		}
		f->pos += k;
		f->off += k;
		n -= k;
	}
	return 1;
}

typedef struct {
	CALENDAR *c;
	int res;
} LOADCTX;

static int add_event(EVENT *e, void *ud)
{
	LOADCTX *l = ud;
	CALENDAR *c = l->c;
	if (c->nev >= c->maxev) {
		EVENT **n;
		int max = c->maxev ? c->maxev * 2 : 64;
		n = realloc(c->ev, max * sizeof(EVENT *));
		if (!n) {
			ev_free(e);
			return 0;
		}
		c->ev = n;
		c->maxev = max;
	}
	e->cal = (short)(c - cals);
	e->res = (short)l->res;
	if (!c->alarms)
		e->alarm = -1;
	c->ev[c->nev++] = e;
	return 1;
}

static void mark_exceptions(CALENDAR *c)
{
	int i, j;
	for (i = 0; i < c->nev; i++)
		if (c->ev[i]->recurid != CT_NONE)
			for (j = 0; j < c->nev; j++)
				if (c->ev[j]->rr && c->ev[j]->recurid == CT_NONE && c->ev[j]->uid &&
				    c->ev[i]->uid && !strcmp(c->ev[j]->uid, c->ev[i]->uid))
					c->ev[j]->hasexc |= c->ev[i]->range_future ? 3 : 1;
}

void cal_load(CALENDAR *c)
{
	char path[200], line[700];
	CTIME from, to;
	LOADCTX l;
	FR f;
	cal_free_events(c);
	window(&from, &to);
	l.c = c;
	l.res = -1;
	if (accts[c->acct].kind == AK_SUBSCR) {
		cache_path(c, "ICS", path, sizeof(path));
		if (pf_exists(path))
			ics_parse_file(path, add_event, &l, from, to);
		mark_exceptions(c);
		return;
	}
	cache_path(c, "DAT", path, sizeof(path));
	memset(&f, 0, sizeof(f));
	f.h = pf_open(path, PF_READ);
	if (f.h < 0)
		return;
	if (!fr_line(&f, line, sizeof(line)) || strncmp(line, "CALCACHE 1", 10)) {
		pf_close(f.h);
		return;
	}
	while (fr_line(&f, line, sizeof(line))) {
		char *tab1, *tab2, *raw;
		long len;
		RESOURCE *r;
		if (line[0] == 'C' && line[1] == ' ')
			continue;		/* the ctag lives in CALENDAR.INF */
		if (line[0] != 'R' || line[1] != ' ')
			break;
		tab1 = strchr(line + 2, '\t');
		tab2 = tab1 ? strchr(tab1 + 1, '\t') : 0;
		if (!tab2)
			break;
		*tab1 = *tab2 = 0;
		len = atol(tab2 + 1);
		if (c->nres >= c->maxres) {
			RESOURCE *n;
			int max = c->maxres ? c->maxres * 2 : 64;
			n = realloc(c->res, max * sizeof(RESOURCE));
			if (!n)
				break;
			c->res = n;
			c->maxres = max;
		}
		r = &c->res[c->nres];
		r->etag = strdup(line + 2);
		r->href = strdup(tab1 + 1);
		r->off = f.off;
		r->len = len;
		l.res = c->nres++;
		raw = len < 65536L ? malloc(len + 1) : 0;
		if (raw) {
			if (!fr_bytes(&f, raw, len)) {
				free(raw);
				break;
			}
			raw[len] = 0;
			ics_parse(raw, len, add_event, &l, from, to);
			free(raw);
		} else if (!fr_bytes(&f, 0, len)) {
			break;
		}
		fr_bytes(&f, 0, 1);		/* the newline after it */
	}
	pf_close(f.h);
	mark_exceptions(c);
}

void cal_load_all(void)
{
	int i;
	for (i = 0; i < ncals; i++) {
		status("Reading %s...", cals[i].name);
		cal_load(&cals[i]);
	}
	status_clear();
}

/* writing a new cache file: header, then resources one by one */
typedef struct {
	CALENDAR *c;
	int h, old;		/* new file, old one (for copying) */
	char tmp[200];
	int failed;
} CW;

static int cw_open(CW *w, CALENDAR *c)
{
	char path[200];
	memset(w, 0, sizeof(*w));
	w->c = c;
	cache_path(c, "TMP", w->tmp, sizeof(w->tmp));
	w->h = pf_open(w->tmp, PF_WRITE);
	if (w->h < 0) {
		snprintf(cal_err, sizeof(cal_err), "can't write the calendar's cache (disk full or locked?)");
		return 0;
	}
	cache_path(c, "DAT", path, sizeof(path));
	w->old = pf_open(path, PF_READ);
	pf_write(w->h, "CALCACHE 1\n", 11);
	return 1;
}

static void cw_add(CW *w, const char *href, const char *etag, const char *raw, long len)
{
	char head[700];
	int n = snprintf(head, sizeof(head), "R %s\t%s\t%ld\n", etag ? etag : "", href, len);
	if (pf_write(w->h, head, n) != n || pf_write(w->h, raw, len) != len || pf_write(w->h, "\n", 1) != 1)
		w->failed = 1;
}

static void cw_copy(CW *w, const RESOURCE *r)
{
	char *raw;
	if (w->old < 0 || !(raw = malloc(r->len + 1)))
		return;
	if (pf_seek(w->old, r->off) == r->off && pf_read(w->old, raw, r->len) == r->len)
		cw_add(w, r->href, r->etag, raw, r->len);
	else
		w->failed = 1;
	free(raw);
}

static int cw_close(CW *w)
{
	char path[200];
	if (w->old >= 0)
		pf_close(w->old);
	pf_close(w->h);
	if (w->failed) {
		pf_remove(w->tmp);
		snprintf(cal_err, sizeof(cal_err), "couldn't write the calendar's cache (disk full?)");
		return 0;
	}
	cache_path(w->c, "DAT", path, sizeof(path));
	pf_remove(path);
	pf_rename(w->tmp, path);
	return 1;
}

/* the cache with resource `drop` left out and one resource added */
static int cache_change(CALENDAR *c, int drop, const char *href, const char *etag, const char *raw, long len)
{
	CW w;
	int i;
	if (!cw_open(&w, c))
		return 0;
	for (i = 0; i < c->nres; i++)
		if (i != drop && !(href && !strcmp(c->res[i].href, href)))
			cw_copy(&w, &c->res[i]);
	if (href)
		cw_add(&w, href, etag, raw, len);
	if (!cw_close(&w))
		return 0;
	cal_load(c);
	return 1;
}

/* ---------------- syncing ---------------- */

/* etags compare without quotes and without a weak W/ */
static int same_etag(const char *a, const char *b)
{
	int la, lb;
	if (!strncmp(a, "W/", 2))
		a += 2;
	if (!strncmp(b, "W/", 2))
		b += 2;
	if (*a == '"')
		a++;
	if (*b == '"')
		b++;
	la = (int)strlen(a);
	lb = (int)strlen(b);
	if (la && a[la - 1] == '"')
		la--;
	if (lb && b[lb - 1] == '"')
		lb--;
	return la == lb && !strncmp(a, b, la) && la > 0;
}

static int mget_cb(void *ud, const char *href, const char *etag, const char *data, long len)
{
	CW *w = ud;
	cw_add(w, href, etag, data, len);
	return !w->failed;
}

static int sync_caldav(CALENDAR *c, int force)
{
	DAVAUTH au = auth_of(c->acct);
	DAVITEM *items;
	char ctag[100], **need;
	unsigned char *keep;
	int n, i, j, nneed = 0, nkept;
	CTIME from, to;
	CW w;
	status("%s: checking...", c->name);
	if (!caldav_ctag(c->url, &au, ctag, sizeof(ctag), cal_err, sizeof(cal_err)))
		return -1;
	if (!force && ctag[0] && !strcmp(ctag, c->ctag) && c->synced)
		return 0;
	window(&from, &to);
	from = tz_to_utc(view_zone(), from);
	to = tz_to_utc(view_zone(), to);
	status("%s: what has changed...", c->name);
	if (!caldav_list(c->url, &au, from, to, &items, &n, cal_err, sizeof(cal_err)))
		return -1;
	keep = calloc(c->nres + 1, 1);
	need = malloc((n + 1) * sizeof(char *));
	if (!keep || !need) {
		free(keep);
		free(need);
		caldav_free_items(items, n);
		str_copy(cal_err, "out of memory", sizeof(cal_err));
		return -1;
	}
	for (i = 0; i < n; i++) {
		int found = -1;
		for (j = 0; j < c->nres; j++)
			if (!strcmp(c->res[j].href, items[i].href)) {
				found = j;
				break;
			}
		if (found >= 0 && same_etag(c->res[found].etag, items[i].etag))
			keep[found] = 1;
		else
			need[nneed++] = items[i].href;
	}
	if (!cw_open(&w, c)) {
		free(keep);
		free(need);
		caldav_free_items(items, n);
		return -1;
	}
	for (j = 0, nkept = 0; j < c->nres; j++)
		if (keep[j]) {
			cw_copy(&w, &c->res[j]);
			nkept++;
		}
	if (!nneed && nkept == c->nres) {
		/* the ctag moved (our own changes, say) but nothing is new */
		if (w.old >= 0)
			pf_close(w.old);
		pf_close(w.h);
		pf_remove(w.tmp);
		free(keep);
		free(need);
		caldav_free_items(items, n);
		str_copy(c->ctag, ctag, sizeof(c->ctag));
		c->synced = now_utc();
		return 0;
	}
	if (nneed) {
		status("%s: fetching %d event%s...", c->name, nneed, nneed == 1 ? "" : "s");
		if (!caldav_multiget(c->url, &au, need, nneed, mget_cb, &w, cal_err, sizeof(cal_err)))
			w.failed = 2;
	}
	free(keep);
	free(need);
	caldav_free_items(items, n);
	if (w.failed == 2) {
		if (w.old >= 0)
			pf_close(w.old);
		pf_close(w.h);
		pf_remove(w.tmp);
		return -1;
	}
	if (!cw_close(&w))
		return -1;
	str_copy(c->ctag, ctag, sizeof(c->ctag));
	c->synced = now_utc();
	cal_load(c);
	return 1;
}

typedef struct {
	int h;
	long n;
} FSINK;

static int file_sink(void *ud, const char *data, long n)
{
	FSINK *f = ud;
	f->n += n;
	if (pf_write(f->h, data, n) != n)
		return 0;
	if ((f->n & 0xffff) < n)
		status("Downloading... %s bytes", num(f->n));
	return 1;
}

static int sync_subscription(CALENDAR *c, int force)
{
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF errbody;
	FSINK fs;
	char tmp[200], path[200], hdr[400];
	CTIME from, to;
	int ok;
	DT a, b;
	window(&from, &to);
	ct_split(from, &a);
	ct_split(to, &b);
	cache_path(c, "NEW", tmp, sizeof(tmp));
	cache_path(c, "ICS", path, sizeof(path));
	fs.h = pf_open(tmp, PF_WRITE);
	fs.n = 0;
	if (fs.h < 0) {
		str_copy(cal_err, "can't write the subscription's cache", sizeof(cal_err));
		return -1;
	}
	/* the Pi gateway trims big feeds to this window for small Ataris */
	snprintf(hdr, sizeof(hdr), "Accept: text/calendar, */*\r\nX-Calendar-Window: %04d%02d%02d-%04d%02d%02d\r\n",
		 a.year, a.mon, a.day, b.year, b.mon, b.day);
	if (!force && pf_exists(path)) {
		long l = (long)strlen(hdr);
		if (c->etag[0])
			snprintf(hdr + l, sizeof(hdr) - l, "If-None-Match: %s\r\n", c->etag);
		else if (c->lastmod[0])
			snprintf(hdr + l, sizeof(hdr) - l, "If-Modified-Since: %s\r\n", c->lastmod);
	}
	memset(&rq, 0, sizeof(rq));
	rq.method = "GET";
	rq.url = c->url;
	rq.headers = hdr;
	rq.sink = file_sink;
	rq.ud = &fs;
	if (accts[c->acct].user[0]) {
		rq.user = accts[c->acct].user;
		rq.pass = accts[c->acct].pass;
	}
	sb_init(&errbody);
	status("%s: downloading...", c->name);
	ok = http_request(&rq, &rs, &errbody, cal_err, sizeof(cal_err));
	pf_close(fs.h);
	sb_free(&errbody);
	if (!ok) {
		pf_remove(tmp);
		return -1;
	}
	if (rs.status == 304) {
		pf_remove(tmp);
		c->synced = now_utc();
		return 0;
	}
	if (rs.status != 200) {
		pf_remove(tmp);
		snprintf(cal_err, sizeof(cal_err), "%s: %s (HTTP %d)", c->name, caldav_status_text(rs.status), rs.status);
		return -1;
	}
	pf_remove(path);
	pf_rename(tmp, path);
	str_copy(c->etag, rs.etag, sizeof(c->etag));
	str_copy(c->lastmod, rs.lastmod, sizeof(c->lastmod));
	c->synced = now_utc();
	status("%s: reading...", c->name);
	cal_load(c);
	/* a new subscription takes the feed's own name */
	if ((!c->name[0] || !strcmp(c->name, "Subscription")) && ics_calname[0])
		str_copy(c->name, ics_calname, sizeof(c->name));
	return 1;
}

int sync_calendar(CALENDAR *c, int force)
{
	int k = accts[c->acct].kind, r;
	if (k == AK_LOCAL)
		return 0;
	cal_err[0] = 0;
	r = k == AK_SUBSCR ? sync_subscription(c, force) : sync_caldav(c, force);
	if (r < 0)
		str_copy(accts[c->acct].err, cal_err, sizeof(accts[c->acct].err));
	return r;
}

int sync_account(int a, int force)
{
	int i, changed = 0, failed = 0;
	char err[200] = "";
	if (accts[a].kind == AK_LOCAL)
		return 0;
	accts[a].err[0] = 0;
	if (accts[a].kind == AK_CALDAV) {
		int have = 0;
		for (i = 0; i < ncals; i++)
			have += cals[i].acct == a;
		/* the first time, and on a forced refresh, the calendar list too */
		if ((!have || force) && !acct_discover(a))
			return -1;
	}
	for (i = 0; i < ncals; i++) {
		int r;
		if (cals[i].acct != a)
			continue;
		r = sync_calendar(&cals[i], force);
		if (r > 0)
			changed++;
		if (r < 0) {
			failed++;
			if (!err[0])
				str_copy(err, cal_err, sizeof(err));
		}
	}
	if (failed) {
		str_copy(cal_err, err, sizeof(cal_err));
		str_copy(accts[a].err, err, sizeof(accts[a].err));
	}
	store_save_settings();
	status_clear();
	return failed && !changed ? -1 : changed;
}

int sync_all(int force)
{
	int a, changed = 0, failed = 0;
	char err[200] = "";
	for (a = 0; a < naccts; a++) {
		int r = sync_account(a, force);
		if (r > 0)
			changed += r;
		if (r < 0) {
			failed++;
			if (!err[0])
				snprintf(err, sizeof(err), "%s: %s", accts[a].name, cal_err);
		}
	}
	if (failed)
		str_copy(cal_err, err, sizeof(cal_err));
	http_close_all();
	conn_log_flush();
	return failed ? -1 : changed;
}

/* ---------------- occurrences ---------------- */

/* an exception with RANGE=THISANDFUTURE that covers the occurrence of
   series m starting at orig: the latest one at or before it */
static EVENT *future_exception(EVENT *m, CTIME orig)
{
	CALENDAR *c = &cals[m->cal];
	EVENT *best = 0;
	int i;
	for (i = 0; i < c->nev; i++) {
		EVENT *x = c->ev[i];
		if (x->range_future && x->recurid != CT_NONE && x->recurid <= orig && x->uid && m->uid &&
		    !strcmp(x->uid, m->uid) && (!best || x->recurid > best->recurid))
			best = x;
	}
	return best;
}

static void occ_add(EVENT *e, CTIME s, CTIME en, void *ud)
{
	OCCLIST *l = ud;
	CTIME orig = e->recurid != CT_NONE ? e->recurid : ev_from_wall(e, s, view_zone());
	if (l->n >= l->max) {
		OCC *n;
		int max = l->max ? l->max * 2 : 64;
		n = realloc(l->o, max * sizeof(OCC));
		if (!n)
			return;
		l->o = n;
		l->max = max;
	}
	if ((e->hasexc & 2) && e->recurid == CT_NONE) {
		/* a change to "this and all future" occurrences: they move
		   and look like that exception */
		EVENT *x = future_exception(e, orig);
		if (x) {
			CTIME shift = x->start - x->recurid;
			s += shift;
			en = s + (x->end - x->start);
			e = x;
		}
	}
	l->o[l->n].ev = e;
	l->o[l->n].start = s;
	l->o[l->n].end = en;
	l->o[l->n].orig = orig;
	l->n++;
}

static int occ_skip(EVENT *m, CTIME orig, void *ud)
{
	CALENDAR *c = &cals[m->cal];
	int i;
	(void)ud;
	if (!m->hasexc)
		return 0;
	for (i = 0; i < c->nev; i++) {
		EVENT *x = c->ev[i];
		if (x->recurid == orig && x->uid && !strcmp(x->uid, m->uid))
			return 1;
	}
	return 0;
}

static int occ_cmp(const void *a, const void *b)
{
	const OCC *x = a, *y = b;
	int xa = x->ev->allday || x->end - x->start >= MIN_PER_DAY;
	int ya = y->ev->allday || y->end - y->start >= MIN_PER_DAY;
	if (xa != ya)
		return ya - xa;
	if (x->start != y->start)
		return x->start < y->start ? -1 : 1;
	if (x->end != y->end)
		return x->end > y->end ? -1 : 1;
	return x->ev->cal - y->ev->cal;
}

void occ_free(OCCLIST *l)
{
	free(l->o);
	memset(l, 0, sizeof(*l));
}

void occ_collect(OCCLIST *l, CTIME from, CTIME to)
{
	const TZONE *vz = view_zone();
	int i, j;
	l->n = 0;
	l->from = from;
	l->to = to;
	for (i = 0; i < ncals; i++) {
		CALENDAR *c = &cals[i];
		if (!c->visible)
			continue;
		for (j = 0; j < c->nev; j++) {
			EVENT *e = c->ev[j];
			if (e->status == ST_CANCELLED && !e->rr)
				continue;
			ics_expand(e, from, to, vz, occ_add, occ_skip, l);
		}
	}
	if (l->n > 1)
		qsort(l->o, l->n, sizeof(OCC), occ_cmp);
}

static int contains(const char *h, const char *n)
{
	return h && str_istr(h, n);
}

void occ_search(OCCLIST *l, const char *text, CTIME from, CTIME to)
{
	int i, w;
	occ_collect(l, from, to);
	for (i = w = 0; i < l->n; i++) {
		EVENT *e = l->o[i].ev;
		if (contains(e->summary, text) || contains(e->location, text) || contains(e->notes, text))
			l->o[w++] = l->o[i];
	}
	l->n = w;
	/* by date only, all-day ones among the others */
	for (i = 1; i < l->n; i++) {
		OCC t = l->o[i];
		int k = i;
		while (k > 0 && l->o[k - 1].start > t.start) {
			l->o[k] = l->o[k - 1];
			k--;
		}
		l->o[k] = t;
	}
}

/* ---------------- changing events ---------------- */

char *ev_raw(const EVENT *e, long *len)
{
	CALENDAR *c = &cals[e->cal];
	RESOURCE *r;
	char path[200], *buf;
	int h;
	if (e->res < 0 || e->res >= c->nres)
		return 0;
	r = &c->res[e->res];
	cache_path(c, "DAT", path, sizeof(path));
	h = pf_open(path, PF_READ);
	if (h < 0)
		return 0;
	buf = malloc(r->len + 1);
	if (buf && (pf_seek(h, r->off) != r->off || pf_read(h, buf, r->len) != r->len)) {
		free(buf);
		buf = 0;
	}
	pf_close(h);
	if (buf) {
		buf[r->len] = 0;
		if (len)
			*len = r->len;
	}
	return buf;
}

EVENT *ev_master(const EVENT *e)
{
	CALENDAR *c = &cals[e->cal];
	int i;
	if (e->recurid == CT_NONE)
		return (EVENT *)e;
	for (i = 0; i < c->nev; i++)
		if (c->ev[i]->recurid == CT_NONE && c->ev[i]->uid && e->uid && !strcmp(c->ev[i]->uid, e->uid))
			return c->ev[i];
	return 0;
}

/* a file-name-safe href for a new event */
static void new_href(const char *uid, char *out, int size)
{
	int o = 0;
	for (; *uid && o < size - 5; uid++)
		out[o++] = (isalnum((unsigned char)*uid) || *uid == '-' || *uid == '_') ? *uid : '_';
	strcpy(out + o, ".ics");
}

/* store text for resource res (-1: a new one at href) of calendar c,
   on the server and in the cache */
static int put_resource(CALENDAR *c, int res, const char *href, const char *text, long len)
{
	char etag[100] = "";
	const char *h = res >= 0 ? c->res[res].href : href;
	if (accts[c->acct].kind == AK_CALDAV) {
		DAVAUTH au = auth_of(c->acct);
		status("Saving to %s...", accts[c->acct].name);
		if (!caldav_put(c->url, h, &au, text, len, res >= 0 ? c->res[res].etag : 0,
				etag, sizeof(etag), cal_err, sizeof(cal_err))) {
			status_clear();
			return 0;
		}
		status_clear();
	} else if (accts[c->acct].kind != AK_LOCAL) {
		str_copy(cal_err, "subscribed calendars can't be changed", sizeof(cal_err));
		return 0;
	}
	{
		char *hh = strdup(h);
		int ok = hh && cache_change(c, res, hh, etag, text, len);
		free(hh);
		return ok;
	}
}

static int delete_resource(CALENDAR *c, int res)
{
	if (accts[c->acct].kind == AK_CALDAV) {
		DAVAUTH au = auth_of(c->acct);
		status("Deleting...");
		if (!caldav_delete(c->url, c->res[res].href, &au, c->res[res].etag, cal_err, sizeof(cal_err))) {
			status_clear();
			return 0;
		}
		status_clear();
	} else if (accts[c->acct].kind != AK_LOCAL) {
		str_copy(cal_err, "subscribed calendars can't be changed", sizeof(cal_err));
		return 0;
	}
	return cache_change(c, res, 0, 0, 0, 0);
}

static int same_rule(const RRULE *a, const RRULE *b)
{
	if (!a || !b)
		return a == b;
	return a->freq == b->freq && a->interval == b->interval && a->count == b->count &&
	       a->until == b->until && a->nbyday == b->nbyday && a->nbymd == b->nbymd &&
	       a->bymonth == b->bymonth && !memcmp(a->byday, b->byday, sizeof(a->byday)) &&
	       !memcmp(a->bymd, b->bymd, sizeof(a->bymd));
}

/* a new event (or a whole new series) into calendar ci */
static int save_new(EVENT *e, int ci)
{
	CALENDAR *c = &cals[ci];
	char href[120], uid[60];
	long len;
	char *text;
	int ok;
	if (!e->uid) {
		ics_new_uid(uid, sizeof(uid));
		e->uid = strdup(uid);
	}
	e->recurid = CT_NONE;
	e->seq = 0;
	new_href(e->uid, href, sizeof(href));
	ics_now_utc = now_utc();
	text = ics_build(e, 0, 0, &len);
	if (!text) {
		str_copy(cal_err, "out of memory", sizeof(cal_err));
		return 0;
	}
	ok = put_resource(c, -1, href, text, len);
	free(text);
	return ok;
}

/* the last start before occ, in the event's wall time, for UNTIL */
static CTIME until_before(const EVENT *m, CTIME occ)
{
	CTIME w = (m->allday || m->floating || !m->tz) ? occ : tz_to_local(m->tz, occ);
	return m->allday ? w - MIN_PER_DAY : w - 1;
}

int ev_save(EVENT *e, int ci, int how, CTIME occ)
{
	CALENDAR *c;
	EVENT *m;
	char *raw = 0, *text;
	long rawlen = 0, len;
	int ok, res;
	if (ci < 0 || ci >= ncals || !cal_writable(&cals[ci])) {
		str_copy(cal_err, "this calendar can't be changed", sizeof(cal_err));
		return 0;
	}
	ics_now_utc = now_utc();
	if (e->res < 0)
		return save_new(e, ci);
	c = &cals[e->cal];
	res = e->res;
	m = ev_master(e);
	/* moving to another calendar: new there, then gone here */
	if (ci != e->cal) {
		EVENT *copy = ev_dup(e);
		int r = res;
		CALENDAR *from = c;
		if (!copy)
			return 0;
		raw = ev_raw(e, &rawlen);
		copy->res = -1;
		copy->recurid = CT_NONE;
		ok = 0;
		if (raw) {
			char href[120];
			new_href(copy->uid, href, sizeof(href));
			text = ics_build(copy, raw, rawlen, &len);
			ok = text && put_resource(&cals[ci], -1, href, text, len);
			free(text);
		}
		ev_free(copy);
		free(raw);
		return ok && delete_resource(from, r);
	}
	raw = ev_raw(e, &rawlen);
	if (!raw) {
		str_copy(cal_err, "the event's data is missing from the cache: refresh and try again", sizeof(cal_err));
		return 0;
	}
	if (how == SAVE_THIS && m && m->rr) {
		/* an exception for one occurrence */
		e->recurid = occ;
		free(e->rr);
		e->rr = 0;
		free(e->exdate);
		e->exdate = 0;
		e->nexdate = 0;
		e->seq++;
		text = ics_build(e, raw, rawlen, &len);
	} else if (how == SAVE_FUTURE && m && m->rr && occ != m->start) {
		/* the old series stops before this one; a new one starts here */
		EVENT *old = ev_dup(m);
		if (!old) {
			free(raw);
			return 0;
		}
		old->rr->until = until_before(m, occ);
		old->rr->count = 0;
		old->seq++;
		text = ics_build(old, raw, rawlen, &len);
		ev_free(old);
		ok = text && put_resource(c, res, 0, text, len);
		free(text);
		free(raw);
		if (!ok)
			return 0;
		free(e->uid);
		e->uid = 0;
		e->res = -1;
		e->recurid = CT_NONE;
		free(e->exdate);
		e->exdate = 0;
		e->nexdate = 0;
		return save_new(e, ci);
	} else {
		/* the whole event or series */
		if (m && !same_rule(m->rr, e->rr)) {
			/* a new rule: the old exceptions no longer fit */
			char *r2 = ics_drop_exceptions(raw, rawlen, CT_NONE, &rawlen);
			free(raw);
			raw = r2;
			free(e->exdate);
			e->exdate = 0;
			e->nexdate = 0;
		}
		e->recurid = CT_NONE;
		e->seq++;
		text = ics_build(e, raw, rawlen, &len);
	}
	free(raw);
	if (!text)
		return 0;
	ok = put_resource(c, res, 0, text, len);
	free(text);
	return ok;
}

int ev_delete(EVENT *e, int how, CTIME occ)
{
	CALENDAR *c = &cals[e->cal];
	EVENT *m = ev_master(e), *copy;
	char *raw, *r2, *text;
	long rawlen, len;
	int ok;
	if (!cal_writable(c)) {
		str_copy(cal_err, "this calendar can't be changed", sizeof(cal_err));
		return 0;
	}
	if (e->res < 0)
		return 0;
	if (!m || !m->rr || how == SAVE_ALL || (how == SAVE_FUTURE && occ <= m->start))
		return delete_resource(c, e->res);
	raw = ev_raw(e, &rawlen);
	if (!raw)
		return 0;
	ics_now_utc = now_utc();
	copy = ev_dup(m);
	if (!copy) {
		free(raw);
		return 0;
	}
	if (how == SAVE_THIS) {
		CTIME *n = realloc(copy->exdate, (copy->nexdate + 1) * sizeof(CTIME));
		if (n) {
			copy->exdate = n;
			copy->exdate[copy->nexdate++] = occ;
		}
		r2 = ics_drop_exceptions(raw, rawlen, occ, &rawlen);
	} else {
		copy->rr->until = until_before(m, occ);
		copy->rr->count = 0;
		r2 = strdup(raw);
	}
	free(raw);
	copy->seq++;
	text = r2 ? ics_build(copy, r2, rawlen, &len) : 0;
	free(r2);
	ev_free(copy);
	if (!text)
		return 0;
	ok = put_resource(c, e->res, 0, text, len);
	free(text);
	return ok;
}

/* ---------------- import ---------------- */

typedef struct {
	EVENT **ev;
	int n, max;
} GROUP;

static int collect(EVENT *e, void *ud)
{
	GROUP *g = ud;
	if (g->n >= g->max) {
		EVENT **n;
		g->max = g->max ? g->max * 2 : 32;
		n = realloc(g->ev, g->max * sizeof(EVENT *));
		if (!n) {
			ev_free(e);
			return 0;
		}
		g->ev = n;
	}
	g->ev[g->n++] = e;
	return 1;
}

int cal_import(int ci, const char *path, int *count)
{
	GROUP g;
	int i, j, ok = 1;
	*count = 0;
	if (!cal_writable(&cals[ci])) {
		str_copy(cal_err, "this calendar can't be changed", sizeof(cal_err));
		return 0;
	}
	memset(&g, 0, sizeof(g));
	if (ics_parse_file(path, collect, &g, CT_NONE, CT_NONE) < 0) {
		str_copy(cal_err, "can't read the file", sizeof(cal_err));
		return 0;
	}
	ics_now_utc = now_utc();
	for (i = 0; i < g.n && ok; i++) {
		EVENT *m = g.ev[i];
		char *text, href[120];
		long len;
		if (!m || m->recurid != CT_NONE)
			continue;
		text = ics_build(m, 0, 0, &len);
		/* its exceptions go into the same resource */
		for (j = 0; j < g.n && text; j++) {
			EVENT *x = g.ev[j];
			if (x && x->recurid != CT_NONE && x->uid && !strcmp(x->uid, m->uid)) {
				char *t2 = ics_build(x, text, len, &len);
				free(text);
				text = t2;
			}
		}
		if (!text)
			break;
		new_href(m->uid, href, sizeof(href));
		status("Importing %d of %d...", i + 1, g.n);
		ok = put_resource(&cals[ci], -1, href, text, len);
		free(text);
		if (ok)
			(*count)++;
	}
	for (i = 0; i < g.n; i++)
		ev_free(g.ev[i]);
	free(g.ev);
	status_clear();
	return ok;
}
