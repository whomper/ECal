/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * calstore.c - the calendars: settings, caches on disk, syncing with
 * CalDAV servers and subscriptions, occurrences for the views, and
 * saving changes.
 *
 * Each calendar has a cache file in CACHE\ next to ECAL.INF:
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
#include "alarms.h"
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
int store_plain_passwords;		/* ECAL.INF has a password not yet encrypted */

static char inf_path[200], cache_dir[200];
int store_watch;			/* the accessory: store_changed() is asked */
static unsigned long known;		/* the files as we last left them */

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

/* the Atari's clock as it is (the home zone's wall time) */
CTIME now_clock(void)
{
	PFTIME t;
	pf_now(&t);
	return ct_make(t.year, t.mon, t.day, t.hour, t.min);
}

/* a wall time in the view's zone, by the Atari's clock */
CTIME view_to_clock(CTIME t)
{
	const TZONE *v = view_zone(), *h = home_zone();
	if (v == h)
		return t;		/* the usual: the clock shows the view's time */
	return tz_to_local(h, tz_to_utc(v, t));
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
	opt.past_days = 0;	/* from today: past events aren't copied */
	opt.future_days = 400;
	opt.duration = 60;
	opt.sync_open = 1;
	opt.bgsync = 1;
	str_copy(opt.start, "60", sizeof(opt.start));
}

static void set_opt(const char *k, const char *v)
{
#define S(name) if (!strcasecmp(k, #name)) { str_copy(opt.name, v, sizeof(opt.name)); return; }
#define I(name) if (!strcasecmp(k, #name)) { opt.name = (short)atoi(v); return; }
	S(home_tz) S(view_tz) S(gate) S(folder) S(start)
	I(tzsupport) I(falcon) I(dsp) I(weekstart) I(h24) I(hebdates) I(view) I(daystart)
	I(alarms) I(alarm_default) I(log) I(font_id) I(font_pt) I(hebfont) I(win_x) I(win_y)
	I(win_w) I(win_h) I(sidebar) I(past_days) I(future_days) I(duration) I(refresh) I(sync_open) I(offline) I(hebrew) I(heapkb) I(memkb)
	if (!strcasecmp(k, "sync_closed")) {
		opt.bgsync = (short)atoi(v);	/* (BGSYNC= before 2.7, off then: left behind) */
		return;
	}
	if (!strcasecmp(k, "gateport")) {
		opt.gateport = (unsigned short)atoi(v);
		return;
	}
#undef S
#undef I
}

static void set_acct(ACCOUNT *a, const char *k, const char *v)
{
	if (!strcasecmp(k, "name"))
		str_copy(a->name, v, sizeof(a->name));
	else if (!strcasecmp(k, "kind"))
		a->kind = (short)atoi(v);
	else if (!strcasecmp(k, "url"))
		str_copy(a->url, v, sizeof(a->url));
	else if (!strcasecmp(k, "user"))
		str_copy(a->user, v, sizeof(a->user));
	else if (!strcasecmp(k, "refresh"))
		a->refresh = (short)atoi(v);
	else if (!strcasecmp(k, "pass")) {
		if (*v && strncmp(v, SECRET_TAG, strlen(SECRET_TAG)))
			store_plain_passwords = 1;	/* typed in by hand: encrypted at start-up */
		if (!secret_decode(v, a->pass, sizeof(a->pass))) {
			a->pass[0] = 0;
			store_lost_passwords = 1;
		}
	}
}

static void set_cal(CALENDAR *c, const char *k, const char *v)
{
	if (!strcasecmp(k, "id"))
		c->id = (short)atoi(v);
	else if (!strcasecmp(k, "name"))
		str_copy(c->name, v, sizeof(c->name));
	else if (!strcasecmp(k, "url"))
		str_copy(c->url, v, sizeof(c->url));
	else if (!strcasecmp(k, "ctag"))
		str_copy(c->ctag, v, sizeof(c->ctag));
	else if (!strcasecmp(k, "etag"))
		str_copy(c->etag, v, sizeof(c->etag));
	else if (!strcasecmp(k, "lastmod"))
		str_copy(c->lastmod, v, sizeof(c->lastmod));
	else if (!strcasecmp(k, "color"))
		c->color = (short)(atoi(v) % NCOLORS);
	else if (!strcasecmp(k, "visible"))
		c->visible = (unsigned char)atoi(v);
	else if (!strcasecmp(k, "readonly"))
		c->readonly = (unsigned char)atoi(v);
	else if (!strcasecmp(k, "alarms"))
		c->alarms = (unsigned char)atoi(v);
	else if (!strcasecmp(k, "synced"))
		c->synced = atol(v);
}

/* Reading the settings, in steps for the accessory (common/ui/bgwork.c):
   the paths and the key, then ECAL.INF, a few lines at a time */
static char *si_text, *si_line;
static ACCOUNT *si_a;
static CALENDAR *si_c;
static int si_open;		/* ECAL.INF still to be loaded */
static int si_lent;		/* si_text is the caller's (store_init_text), not ours to free */

/* after store_init_begin: ECAL.INF's text already read, to take the
   settings from instead of the file (changed in place as it is read) */
void store_init_text(char *text)
{
	si_open = 0;
	si_line = si_text = text;
	si_lent = 1;
}

void store_init_begin(const char *dir, const char *inf)
{
	char key[200];
	if (!si_lent)
		free(si_text);
	si_text = si_line = 0;
	si_lent = 0;
	si_a = 0;
	si_c = 0;
	defaults();
	str_copy(opt.workdir, dir, sizeof(opt.workdir));
	if (inf)
		str_copy(inf_path, inf, sizeof(inf_path));
	else
		path_join(inf_path, sizeof(inf_path), dir, "ECAL.INF");
	path_join(cache_dir, sizeof(cache_dir), dir, "CACHE");
	pf_mkdir(cache_dir);
	path_join(key, sizeof(key), dir, "ECAL.KEY");
	secret_init(key);
	si_open = 1;
}

static void init_line(char *line)
{
	char *eq;
	if (!strcasecmp(line, "[options]")) {
		si_a = 0;
		si_c = 0;
	} else if (!strcasecmp(line, "[account]")) {
		si_a = naccts < MAXACCT ? &accts[naccts++] : 0;
		if (si_a)
			memset(si_a, 0, sizeof(*si_a));
		si_c = 0;
	} else if (!strcasecmp(line, "[calendar]")) {
		si_c = (si_a && ncals < MAXCAL) ? &cals[ncals++] : 0;
		if (si_c) {
			memset(si_c, 0, sizeof(*si_c));
			si_c->acct = (short)(si_a - accts);
			si_c->visible = 1;
			si_c->alarms = 1;
		}
	} else if ((eq = strchr(line, '='))) {
		*eq = 0;
		if (si_c)
			set_cal(si_c, line, eq + 1);
		else if (si_a)
			set_acct(si_a, line, eq + 1);
		else {
			/* a note after a setting: "7  ; a week" (not in names,
			   addresses or passwords, which may hold a ';') */
			char *v = eq + 1, *sc = strchr(v, ';');
			if (sc && sc > v && (sc[-1] == ' ' || sc[-1] == '\t')) {
				while (sc > v && (sc[-1] == ' ' || sc[-1] == '\t'))
					sc--;
				*sc = 0;
			}
			set_opt(line, v);
		}
	}
}

/* 1 while there is more */
int store_init_step(void)
{
	int k;
	if (si_open) {
		long len;
		si_open = 0;
		si_line = si_text = pf_load(inf_path, &len);
		if (si_text)
			return 1;
	}
	for (k = 0; k < 16 && si_line && *si_line; k++) {
		char *line = si_line, *nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		if (nl && nl > line && nl[-1] == '\r')
			nl[-1] = 0;
		si_line = nl ? nl + 1 : 0;
		if (*line && *line != ';')
			init_line(line);
	}
	if (si_line && *si_line)
		return 1;
	if (!si_lent)
		free(si_text);
	si_text = si_line = 0;
	si_lent = 0;
	/* 92 days back was the first default: what a file still says from
	   then moves to the new one (past events aren't copied) */
	if (opt.past_days == 92)
		opt.past_days = 0;
	str_copy(http_cfg.gate, opt.gate, sizeof(http_cfg.gate));
	http_cfg.gateport = opt.gateport;
	http_cfg.falcon = opt.falcon;
	http_cfg.offline = opt.offline;
	return 0;
}

void store_init(const char *dir, const char *inf)
{
	store_init_begin(dir, inf);
	while (store_init_step())
		;
}

/* each setting with a note, so the file explains itself (as EMail's) */
/* (without printf: ECAL.INF's seventy lines and notes took 55 ms on a
   Falcon that way, written in the accessory's background) */
static void note(SBUF *b, const char *text)
{
	if (*text) {
		sb_adds(b, "; ");
		sb_adds(b, text);
		sb_adds(b, "\r\n");
	}
}

/* KEY=value with its note and a blank line after */
static void kv(SBUF *b, const char *text, const char *key, const char *value)
{
	note(b, text);
	sb_adds(b, key);
	sb_addc(b, '=');
	sb_adds(b, value);
	sb_adds(b, "\r\n\r\n");
}

static void kn(SBUF *b, const char *text, const char *key, long value)
{
	char v[16], *p = v + sizeof(v) - 1;
	unsigned long u = value < 0 ? -(unsigned long)value : (unsigned long)value;
	*p = 0;
	do
		*--p = (char)('0' + u % 10);
	while ((u /= 10) != 0);
	if (value < 0)
		*--p = '-';
	kv(b, text, key, p);
}

/* ECAL.INF's text, in b (sb_init'ed): part 0 the options, 1 the
   accounts and calendars, -1 both */
void store_settings_text(SBUF *bp, int part)
{
#define b (*bp)
	int i, j;
	char enc[200];
	if (part == 1)
		goto accounts;
	sb_reserve(&b, 8192);
	sb_adds(&b, "; ECal settings, written by ECAL.ACC / ECAL.PRG. Lines starting with ; are notes.\r\n"
		    "; Most of these are set in the menu > Settings; see the guide (the Help key).\r\n"
		    "; 1 means on, 0 means off. Keys may be in any case.\r\n\r\n[OPTIONS]\r\n");
#define S(key, name, text) kv(&b, text, key, opt.name);
#define I(key, name, text) kn(&b, text, key, opt.name);
	/* first: the accessory reads it at boot from the start of the file */
	sb_printf(&b, "; ECal's folder, with its calendars, keys and certificates; ECAL.PRG\r\n"
		    "; writes where it was started from, the accessory asks when it's missing\r\nFOLDER=%s\r\n\r\n",
		  opt.workdir);
	S("START", start, "when the accessory reads the calendars: now (at boot), opened (when you first\r\n"
	  "; open it) or seconds after boot; reminders come from ALARMS.IDX meanwhile")
	I("HEAPKB", heapkb, "memory the accessory takes at boot, in KB, as you set it; 0: as ECal measures")
	I("MEMKB", memkb, "memory the accessory measured it needs (each refresh writes it), taken at\r\n"
	  "; the next start, in KB; 0: not measured yet")
	S("HOME_TZ", home_tz, "the time zone the Atari's clock is set to (Settings > Time zones)")
	S("VIEW_TZ", view_tz, "the zone times are shown in, with time zone support on")
	I("TZSUPPORT", tzsupport, "time zone support: each event shows and edits its own zone")
	I("FALCON", falcon, "Falcon mode: TLS on this Atari (Falcon, TT); 0: through the Pi gateway")
	I("DSP", dsp, "check certificates on the Falcon's DSP56001")
	S("GATE", gate, "the Raspberry Pi gateway's address (Settings > Connection)")
	sb_printf(&b, "; the gateway's port\r\nGATEPORT=%u\r\n\r\n", opt.gateport);
	I("OFFLINE", offline, "work offline: no connections at all (the menu > Work offline)")
	I("LOG", log, "write the conversation with the servers to ECAL.LOG (passwords left out)")
	I("PAST_DAYS", past_days, "days back kept on this Atari; 0: from today, past events aren't copied")
	I("FUTURE_DAYS", future_days, "days ahead kept on this Atari")
	I("REFRESH", refresh, "minutes between refreshes of all calendars; 0: only by hand")
	I("SYNC_OPEN", sync_open, "refresh when the window opens")
	I("SYNC_CLOSED", bgsync, "the accessory refreshes with its window closed too, in the background (REFRESH\r\n"
	  "; minutes apart, and once after START=)")
	I("WEEKSTART", weekstart, "first day of the week: 0 Sunday, 1 Monday, 6 Saturday")
	I("DAYSTART", daystart, "first hour shown in the day and week views")
	I("H24", h24, "24-hour clock")
	I("HEBDATES", hebdates, "Hebrew dates next to the civil ones")
	I("HEBFONT", hebfont, "where the font has the Hebrew letters: 0 Atari, 1 ISO-8859-8, 2 DOS 862")
	I("HEBREW", hebrew, "start with the Hebrew keyboard (F10 switches)")
	I("DURATION", duration, "minutes a new event lasts")
	I("ALARM_DEFAULT", alarm_default, "a new event's alert, minutes before; -1 none")
	I("ALARMS", alarms, "show alerts")
	I("VIEW", view, "the view: 0 day, 1 week, 2 month, 3 year")
	I("SIDEBAR", sidebar, "show the calendar list")
	I("FONT_ID", font_id, "the font, as a GDOS id; 1: the system font")
	I("FONT_PT", font_pt, "the font's size in points; 0: the system font's")
	I("WIN_X", win_x, "the window's left edge, in pixels")
	I("WIN_Y", win_y, "the window's top edge, in pixels")
	I("WIN_W", win_w, "the window's width, in pixels")
	I("WIN_H", win_h, "the window's height, in pixels")
#undef S
#undef I
	if (part == 0)
		return;
accounts:
	for (i = 0; i < naccts; i++) {
		ACCOUNT *a = &accts[i];
		sb_adds(&b, "[ACCOUNT]\r\n");
		kv(&b, "the account's name in the calendar list", "NAME", a->name);
		kn(&b, "0 CalDAV (iCloud, Fastmail...), 1 a subscription, 2 On My Atari", "KIND", a->kind);
		kv(&b, "the server's address, or the subscription's", "URL", a->url);
		kv(&b, "the user name (iCloud: the Apple ID)", "USER", a->user);
		kn(&b, "minutes between refreshes of this account; 0: with the others", "REFRESH", a->refresh);
		if (a->pass[0]) {
			secret_encode(a->pass, enc, sizeof(enc));
			kv(&b, "the password, encrypted with ECAL.KEY (type a new one in plain text: it is encrypted at start-up)",
			   "PASS", enc);
		}
		for (j = 0; j < ncals; j++) {
			CALENDAR *c = &cals[j];
			if (c->acct != i)
				continue;
			sb_adds(&b, "[CALENDAR]\r\n");
			kn(&b, "its cache file in CACHE: CALnnnnn.DAT or .ICS", "ID", c->id);
			kv(&b, "the calendar's name", "NAME", c->name);
			kv(&b, "where the server keeps it", "URL", c->url);
			kv(&b, "the server's mark for its contents, to see what changed", "CTAG", c->ctag);
			kv(&b, "a subscription's version, to see whether it changed", "ETAG", c->etag);
			kv(&b, "a subscription's last change, as the server says", "LASTMOD", c->lastmod);
			kn(&b, "its colour: 0 red, 1 orange, 2 yellow, 3 green, 4 blue, 5 purple, 6 brown, 7 grey, 8 pink, 9 teal, 10 navy, 11 dark green", "COLOR", c->color);
			kn(&b, "shown in the views", "VISIBLE", c->visible);
			kn(&b, "can't be changed on this Atari", "READONLY", c->readonly);
			kn(&b, "its alerts show", "ALARMS", c->alarms);
			kn(&b, "last brought up to date (minutes since 1970, UTC); 0: never", "SYNCED", (long)c->synced);
		}
	}
#undef b
}

/* ECAL.INF written with that text (b freed) */
void store_write_text(SBUF *b)
{
	pf_save(inf_path, b->s, b->len);
	store_mark();
	sb_free(b);
	str_copy(http_cfg.gate, opt.gate, sizeof(http_cfg.gate));
	http_cfg.gateport = opt.gateport;
	http_cfg.falcon = opt.falcon;
	http_cfg.offline = opt.offline;
}

void store_write_settings(void)
{
	SBUF b;
	sb_init(&b);
	store_settings_text(&b, -1);
	store_write_text(&b);
}

void store_save_settings(void)
{
	store_write_settings();
	store_mark();
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
	c->ndead = 0;
}

/* ECAL.INF, ECAL.KEY and the calendars' caches as they are now: their
   sizes and when they were written. Three looks (guidelines 6.5): ECAL.INF
   and ECAL.KEY one each, the caches from one listing of CACHE (its .DAT
   and .ICS files, in any order). ALARMS.IDX's stamp is the same
   (store_files), so each can stand for the other */
int store_files(const char **out, int max)
{
	static char p[3][200];
	if (max < 3)
		return 0;
	str_copy(p[0], inf_path, sizeof(p[0]));
	path_join(p[1], sizeof(p[1]), opt.workdir, "ECAL.KEY");
	snprintf(p[2], sizeof(p[2]), "%s%c", cache_dir, PF_SEP);
	out[0] = p[0];
	out[1] = p[1];
	out[2] = p[2];
	return 3;
}

static unsigned long inf_seen;	/* ECAL.INF's date and size at the last look */
static long inf_seen_size;

static unsigned long stamp_one(unsigned long h, int i)
{
	const char *f[3];
	store_files(f, 3);
	h = alarms_stamp_add(h, f[i + 2]);
	if (i == -2)
		inf_seen = alarms_stamp_info(&inf_seen_size);
	return h;
}

unsigned long store_inf_seen(long *size)
{
	*size = inf_seen_size;
	return inf_seen;
}

static unsigned long store_stamp(void)
{
	unsigned long h = ALARM_STAMP0;
	int i;
	for (i = -2; i <= 0; i++)
		h = stamp_one(h, i);
	return h;
}

/* after our own writes: the files as they are now are ours. Taken at the
   next look (store_changed), not after each write: a refresh writes a
   cache several times, and each look is three directory searches */
static int mark_due;
static int known_ok;		/* known is of the files as they are */
unsigned long store_gen;	/* counts our writes: ALARMS.IDX is written again only after one */

void store_mark(void)
{
	mark_due = store_watch;
	store_gen++;
}

int store_known(unsigned long *h)
{
	if (!store_watch || !known_ok || mark_due)
		return 0;
	*h = known;
	return 1;
}

void store_mark_known(unsigned long h)
{
	known = h;
	known_ok = 1;
	mark_due = 0;
}

void store_adopt(unsigned long h)
{
	if (mark_due)
		store_mark_known(h);
}

/* the same a look a step; 0 when done */
static int mk_i = -3;
static unsigned long mk_h;

int store_mark_step(void)
{
	if (!store_watch)
		return 0;
	if (mk_i == -3) {
		mk_i = -2;
		mk_h = ALARM_STAMP0;
	}
	mk_h = stamp_one(mk_h, mk_i++);
	if (mk_i <= 0)
		return 1;
	store_mark_known(mk_h);
	mk_i = -3;
	return 0;
}

int store_changed(void)
{
	unsigned long h;
	if (!store_watch)
		return 0;
	h = store_stamp();
	if (mark_due)
		store_mark_known(h);	/* our writes since the last look */
	if (h != known)
		known_ok = 0;
	return h != known;
}

/* the same a look a step: 1 while there is more, then *changed */
int store_changed_step(int *changed)
{
	static int i = -3;
	static unsigned long h;
	*changed = 0;
	if (!store_watch)
		return 0;
	if (i == -3) {
		i = -2;
		h = ALARM_STAMP0;
	}
	h = stamp_one(h, i++);
	if (i <= 0)
		return 1;
	i = -3;
	if (mark_due)
		store_mark_known(h);
	if (h != known)
		known_ok = 0;
	*changed = h != known;
	return 0;
}

void store_release(void)
{
	int i;
	for (i = 0; i < ncals; i++)
		cal_free_events(&cals[i]);
	ncals = naccts = 0;
	known_ok = 0;
	store_lost_passwords = store_plain_passwords = 0;
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
	store_mark();
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
		{ 255, 40, 40 },
		{ 255, 140, 0 },
		{ 255, 215, 0 },
		{ 40, 190, 60 },
		{ 0, 110, 255 },
		{ 150, 60, 220 },
		{ 150, 100, 50 },
		{ 140, 140, 140 },
		{ 255, 100, 180 },
		{ 0, 180, 190 },
		{ 20, 30, 140 },
		{ 0, 110, 40 },
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

/* a buffered reader that knows where it is. The file is open only while
   the buffer is filled: the accessory reads in steps between event calls,
   and a file it opens belongs to the program in front, whose end closes
   it and can give the handle to another file (guidelines 2.2) */
typedef struct {
	char path[200];		/* empty: nothing to read */
	char *buf;		/* 16 KB if there is room: each GEMDOS call costs, on a floppy most */
	int cap, pos, len;
	long off;		/* file offset of buf[pos] */
	long next;		/* of the next fill */
	long size;		/* the file's, found at the first fill */
} FR;

static int fr_fill(FR *f)
{
	int h;
	if (f->pos < f->len)
		return 1;
	f->pos = f->len = 0;
	if (!f->path[0] || (h = pf_open(f->path, PF_READ)) < 0)
		return 0;
	if (!f->next)
		f->size = pf_handle_size(h);	/* no look at the directory for it */
	if (!f->next || pf_seek(h, f->next) == f->next)
		f->len = (int)pf_read(h, f->buf, f->cap);
	pf_close(h);
	if (f->len < 0)
		f->len = 0;
	f->next += f->len;
	return f->len > 0;
}

static int fr_line(FR *f, char *out, int size)
{
	int o = 0;
	for (;;) {
		char *nl;
		int k, n;
		if (!fr_fill(f))
			break;
		/* up to the newline in what is buffered, copied at once (a
		   character at a time was most of reading a calendar on an ST) */
		n = f->len - f->pos;
		nl = memchr(f->buf + f->pos, '\n', n);
		k = nl ? (int)(nl - (f->buf + f->pos)) : n;
		if (k > size - 1 - o)
			memcpy(out + o, f->buf + f->pos, size - 1 - o), o = size - 1;
		else
			memcpy(out + o, f->buf + f->pos, k), o += k;
		f->pos += k;
		f->off += k;
		if (nl) {
			f->pos++;
			f->off++;
			out[o] = 0;
			return 1;
		}
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
			/* no memory left: counted, and the user told (cal_lost) */
			ev_free(e);
			if (c->lost >= 0)
				c->lost++;
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

/* the events that came from resource r leave memory (it was replaced) */
static void drop_res_events(CALENDAR *c, int r)
{
	int i, n = 0;
	for (i = 0; i < c->nev; i++) {
		if (c->ev[i]->res == r)
			ev_free(c->ev[i]);
		else
			c->ev[n++] = c->ev[i];
	}
	c->nev = n;
}

static int live_res(CALENDAR *c, const char *href)
{
	int i;
	for (i = 0; i < c->nres; i++)
		if (!c->res[i].dead && !strcmp(c->res[i].href, href))
			return i;
	return -1;
}

static void kill_res(CALENDAR *c, int r)
{
	c->res[r].dead = 1;
	c->ndead++;
	drop_res_events(c, r);
}

static void mark_exceptions(CALENDAR *c)
{
	int i, j;
	for (i = 0; i < c->nev; i++)
		c->ev[i]->hasexc = 0;
	for (i = 0; i < c->nev; i++)
		if (c->ev[i]->recurid != CT_NONE)
			for (j = 0; j < c->nev; j++)
				if (c->ev[j]->rr && c->ev[j]->recurid == CT_NONE && c->ev[j]->uid &&
				    c->ev[i]->uid && !strcmp(c->ev[j]->uid, c->ev[i]->uid))
					c->ev[j]->hasexc |= c->ev[i]->range_future ? 3 : 1;
}

/* the account's own address, so that its declined invitations are known */
static void set_me(CALENDAR *c)
{
	const char *u = accts[c->acct].user;
	str_copy(ics_me, strchr(u, '@') ? u : "", sizeof(ics_me));
}

void cal_me(int ci)
{
	if (ci >= 0 && ci < ncals)
		set_me(&cals[ci]);
	else
		ics_me[0] = 0;
}

/* While a cache file is read, its events by href: each record must
   replace an older copy of itself, and looking through all the others
   for it (live_res) made reading a big calendar quadratic: 1500 events
   took over a million string compares, seconds on a Falcon. */
static int *hx;			/* resource numbers, -1 for empty; hx_cap a power of 2 */
static int hx_cap;
static CALENDAR *hx_c;		/* whose */

static unsigned long hx_hash(const char *s)
{
	unsigned long h = 5381;			/* djb2: shifts and adds, no multiply (a 68000 has none) */
	while (*s)
		h = ((h << 5) + h) ^ (unsigned char)*s++;
	return h;
}

static void hx_put(CALENDAR *c, int r)
{
	unsigned long i = hx_hash(c->res[r].href) & (hx_cap - 1);
	while (hx[i] >= 0)
		i = (i + 1) & (hx_cap - 1);
	hx[i] = r;
}

/* room for n resources, the table at most half full; 0 if no memory
   (then hx_live falls back to live_res) */
static int hx_alloc(CALENDAR *c, int n)
{
	int cap = hx_cap && hx_c == c ? hx_cap : 256, i, *t;
	while (n * 2 >= cap)
		cap *= 2;
	t = malloc(cap * sizeof(int));
	if (!t)
		return 0;
	free(hx);
	hx = t;
	hx_cap = cap;
	hx_c = c;
	for (i = 0; i < cap; i++)
		hx[i] = -1;
	return 1;
}

static void hx_fill(CALENDAR *c, int from, int to)
{
	for (; from < to; from++)
		if (!c->res[from].dead && c->res[from].href)
			hx_put(c, from);
}

static int hx_room(CALENDAR *c, int n)
{
	if (hx && hx_c == c && n * 2 < hx_cap)
		return 1;
	if (!hx_alloc(c, n))
		return 0;
	hx_fill(c, 0, c->nres);
	return 1;
}

static int hx_live(CALENDAR *c, const char *href)
{
	unsigned long i;
	if (!hx || hx_c != c)
		return live_res(c, href);
	i = hx_hash(href) & (hx_cap - 1);
	for (; hx[i] >= 0; i = (i + 1) & (hx_cap - 1)) {
		int r = hx[i];
		if (!c->res[r].dead && !strcmp(c->res[r].href, href))
			return r;
	}
	return -1;
}

static void hx_done(void)
{
	free(hx);
	hx = 0;
	hx_cap = 0;
	hx_c = 0;
}

/* Reading a calendar in steps, so the accessory can do it between event
   calls: one cached event (or a few lines of a subscription) per step */
static struct {
	CALENDAR *c;		/* NULL: none being read */
	FR f;
	char *rawbuf;		/* one buffer for all, grown as needed */
	long rawcap;
	LOADCTX l;
	CTIME from, to;
	ICSP *p;		/* a subscription: its parser */
	unsigned long t0;
} ld;
static char *ld_line;		/* a subscription's line (as long as ics.c takes) */
static int ld_linecap;

/* the reader's buffer and the line, one block from the heap while a file
   is read (not kept in the program: a 1 MB ST needs the room); 0: no
   memory even for a small one */
static int load_buffers(int linecap)
{
	int cap;
	for (cap = (int)cal_piece; cap >= 2048; cap /= 4) {
		char *b = malloc(cap + linecap);
		if (b) {
			ld.f.buf = b;
			ld.f.cap = cap;
			ld_line = b + cap;
			ld_linecap = linecap;
			return 1;
		}
	}
	return 0;
}

static void load_open(const char *path, int linecap)
{
	/* (a missing file shows at the first fill: no look for it first) */
	if (!load_buffers(linecap)) {
		ld.c->lost = -1;	/* nothing could be read: no memory */
		return;
	}
	str_copy(ld.f.path, path, sizeof(ld.f.path));
}

static void load_done(void)
{
	CALENDAR *c = ld.c;
	char m[120];
	unsigned long t = pf_ms() - ld.t0;
	if (ld.p)
		ics_end(ld.p);
	ld.f.path[0] = 0;
	free(ld.f.buf);
	ld.f.buf = 0;
	ld_line = 0;
	free(ld.rawbuf);
	ld.rawbuf = 0;
	ld.rawcap = 0;
	ld.p = 0;
	ld.c = 0;
	hx_done();
	mark_exceptions(c);
	if (c->lost > 0 && accts[c->acct].kind == AK_SUBSCR)
		c->lost = -1;		/* its parser stopped there: the rest unread */
	snprintf(m, sizeof(m), "read %s: %d events in %lu.%02lu s%s", c->name, c->nev, t / 1000, t % 1000 / 10,
		 c->lost ? " (not all: no memory left)" : "");
	conn_log("ECal", "--", m, (long)strlen(m));
}

void cal_load_begin(CALENDAR *c)
{
	char path[200];
	if (ld.c)
		while (cal_load_step())
			;		/* the one before, to its end */
	cal_free_events(c);
	c->lost = 0;
	window(&ld.from, &ld.to);
	set_me(c);
	memset(&ld.f, 0, sizeof(ld.f));
	ld.c = c;
	ld.p = 0;
	ld.l.c = c;
	ld.l.res = -1;
	ld.t0 = pf_ms();
	if (accts[c->acct].kind == AK_SUBSCR) {
		cache_path(c, "ICS", path, sizeof(path));
		load_open(path, 8192);
		if (ld.f.path[0])
			ld.p = ics_begin(add_event, &ld.l, ld.from, ld.to);
		return;
	}
	cache_path(c, "DAT", path, sizeof(path));
	load_open(path, 700);
	if (ld.f.path[0] && (!fr_line(&ld.f, ld_line, 700) || strncmp(ld_line, "CALCACHE 1", 10)))
		ld.f.path[0] = 0;	/* not one of ours: nothing read */
	{
		/* room for about as many events as the file holds, at once: the
		   tables doubling (and the index made again) as they filled were
		   the longest steps, 90 ms each on an ST */
		long size = ld.f.path[0] ? ld.f.size : 0;	/* its first fill's */
		int est = size > 0 ? (int)(size / 600) + 16 : 0;
		if (est > 4000)
			est = 4000;
		if (est > c->maxres) {
			RESOURCE *n = realloc(c->res, est * sizeof(RESOURCE));
			if (n) {
				c->res = n;
				c->maxres = est;
			}
		}
		if (est > c->maxev) {
			EVENT **n = realloc(c->ev, est * sizeof(EVENT *));
			if (n) {
				c->ev = n;
				c->maxev = est;
			}
		}
		if (est)
			hx_room(c, est);
	}
}

int cal_loading(void)
{
	return ld.c != 0;
}

/* a reading in steps given up (the calendars are about to be freed) */
void cal_load_stop(void)
{
	if (ld.c)
		load_done();
}

/* a subscription: forty lines, about an event */
static int load_ics_step(void)
{
	int k;
	for (k = 0; k < 40; k++) {
		long n;
		if (!fr_line(&ld.f, ld_line, ld_linecap))
			return 0;
		n = (long)strlen(ld_line);
		ics_line(ld.p, ld_line, n);
	}
	return 1;
}

/* a cache file: the next record, an event as the server sent it */
static int load_dat_step(void)
{
	CALENDAR *c = ld.c;
	char *line = ld_line;
	for (;;) {
		char *tab1, *tab2, *raw;
		long len;
		RESOURCE *r;
		if (!fr_line(&ld.f, line, 700))
			return 0;
		if (line[0] == 'C' && line[1] == ' ')
			continue;		/* the ctag lives in ECAL.INF */
		if (line[0] == 'X' && line[1] == ' ') {
			/* an event deleted since the file was written */
			int old = hx_live(c, line + 2);
			if (old >= 0)
				kill_res(c, old);
			continue;
		}
		if (line[0] != 'R' || line[1] != ' ')
			return 0;
		tab1 = strchr(line + 2, '\t');
		tab2 = tab1 ? strchr(tab1 + 1, '\t') : 0;
		if (!tab2)
			return 0;
		*tab1 = *tab2 = 0;
		len = atol(tab2 + 1);
		{
			/* a newer copy of an event, appended when it was changed */
			int old = hx_live(c, tab1 + 1);
			if (old >= 0)
				kill_res(c, old);
		}
		if (c->nres >= c->maxres) {
			RESOURCE *n;
			int max = c->maxres ? c->maxres * 2 : 64;
			n = realloc(c->res, max * sizeof(RESOURCE));
			if (!n) {
				c->lost = -1;	/* the rest can't be kept */
				return 0;
			}
			c->res = n;
			c->maxres = max;
		}
		if (!hx_room(c, c->nres + 1))
			hx_done();		/* no memory: the slow way, which copes */
		r = &c->res[c->nres];
		r->etag = strdup(line + 2);
		r->href = strdup(tab1 + 1);
		r->off = ld.f.off;
		r->len = len;
		r->dead = 0;
		ld.l.res = c->nres++;
		if (hx && hx_c == c && r->href)
			hx_put(c, ld.l.res);
		/* one buffer for all: not a malloc and free for each event */
		if (len < 65536L && len + 1 > ld.rawcap) {
			free(ld.rawbuf);
			ld.rawcap = len + 1 > 4096 ? len + 1 : 4096;
			if (!(ld.rawbuf = malloc(ld.rawcap)))
				ld.rawcap = 0;
		}
		raw = len < 65536L && ld.rawbuf ? ld.rawbuf : 0;
		if (raw) {
			if (!fr_bytes(&ld.f, raw, len))
				return 0;
			raw[len] = 0;
			ics_parse(raw, len, add_event, &ld.l, ld.from, ld.to);
		} else if (!fr_bytes(&ld.f, 0, len)) {
			return 0;
		}
		fr_bytes(&ld.f, 0, 1);		/* the newline after it */
		return 1;
	}
}

int cal_load_step(void)
{
	int more;
	if (!ld.c)
		return 0;
	more = ld.f.path[0] && (ld.p ? load_ics_step() : accts[ld.c->acct].kind != AK_SUBSCR && load_dat_step());
	if (!more)
		load_done();
	return more;
}

/* reading a calendar's cache at once, with how long it took in the log */
void cal_load(CALENDAR *c)
{
	cal_load_begin(c);
	while (cal_load_step())
		;
}

/* after a refresh brought changes: read at once, or (the accessory's
   background refresh, cal_defer_load) only begun, the rest in steps */
int cal_defer_load;

static void reload(CALENDAR *c)
{
	if (cal_defer_load)
		cal_load_begin(c);
	else
		cal_load(c);
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

/* Writing a new cache file: header, then resources one by one. Neither
   file stays open across an event call (a refresh waits for the network
   between events, the accessory's packing goes a step at a time;
   guidelines 2.2): what is written gathers in memory and goes out at
   8 KB, each time opened, added to and closed; the old file is open only
   between cw_copy_begin and cw_copy_end, or for one cw_copy */
/* how much a file is read at a time (guidelines 6.5): 16 KB, 32 KB on a
   68030; and how much a job gathers before it writes: as much in the
   accessory (its heap is fixed), more in ECAL.PRG, so that a refresh is
   one write (main.c sets both) */
long cal_piece = 16384, cal_gather = 16384;
#define CW_FLUSH cal_gather
typedef struct {
	CALENDAR *c;
	char tmp[200], old[200];	/* the new file, the old one (copied from) */
	int oh;				/* the old one while copying, else -1 */
	SBUF out;			/* not written yet */
	int failed;
} CW;

static void cw_flush(CW *w)
{
	int h;
	if (!w->out.len)
		return;
	h = pf_open(w->tmp, PF_APPEND);
	if (h < 0 || pf_write(h, w->out.s, w->out.len) != w->out.len)
		w->failed = 1;
	if (h >= 0)
		pf_close(h);
	sb_reset(&w->out);
}

static int cw_open(CW *w, CALENDAR *c)
{
	int h;
	memset(w, 0, sizeof(*w));
	w->c = c;
	w->oh = -1;
	cache_path(c, "TMP", w->tmp, sizeof(w->tmp));
	cache_path(c, "DAT", w->old, sizeof(w->old));
	h = pf_open(w->tmp, PF_WRITE);
	if (h < 0) {
		snprintf(cal_err, sizeof(cal_err), "can't write the calendar's cache (disk full or locked?)");
		return 0;
	}
	if (pf_write(h, "CALCACHE 1\n", 11) != 11)
		w->failed = 1;
	pf_close(h);
	sb_init(&w->out);
	return 1;
}

static void cw_add(CW *w, const char *href, const char *etag, const char *raw, long len)
{
	char head[700];
	int n = snprintf(head, sizeof(head), "R %s\t%s\t%ld\n", etag ? etag : "", href, len);
	if (!sb_add(&w->out, head, n) || !sb_add(&w->out, raw, len) || !sb_addc(&w->out, '\n')) {
		/* no memory for it: what there is, then this one, written now */
		int h;
		sb_reset(&w->out);
		cw_flush(w);
		h = pf_open(w->tmp, PF_APPEND);
		if (h < 0 || pf_write(h, head, n) != n || pf_write(h, raw, len) != len || pf_write(h, "\n", 1) != 1)
			w->failed = 1;
		if (h >= 0)
			pf_close(h);
		return;
	}
	if (w->out.len >= CW_FLUSH)
		cw_flush(w);
}

static void cw_copy_begin(CW *w)
{
	w->oh = pf_open(w->old, PF_READ);
}

static void cw_copy_end(CW *w)
{
	if (w->oh >= 0)
		pf_close(w->oh);
	w->oh = -1;
}

static void cw_copy(CW *w, const RESOURCE *r)
{
	char *raw;
	int h = w->oh >= 0 ? w->oh : pf_open(w->old, PF_READ);
	if (h < 0)
		return;
	if (!(raw = malloc(r->len + 1)))
		w->failed = 1;
	else if (pf_seek(h, r->off) == r->off && pf_read(h, raw, r->len) == r->len)
		cw_add(w, r->href, r->etag, raw, r->len);
	else
		w->failed = 1;
	free(raw);
	if (h != w->oh)
		pf_close(h);
}

/* the new file given up: the old one stays */
static void cw_abort(CW *w)
{
	cw_copy_end(w);
	sb_free(&w->out);
	pf_remove(w->tmp);
}

static int cw_close(CW *w)
{
	cw_copy_end(w);
	cw_flush(w);
	sb_free(&w->out);
	if (w->failed) {
		pf_remove(w->tmp);
		snprintf(cal_err, sizeof(cal_err), "couldn't write the calendar's cache (disk full?)");
		return 0;
	}
	pf_remove(w->old);
	pf_rename(w->tmp, w->old);
	store_mark();
	return 1;
}

/* the whole cache written again, without the superseded copies */
static int cache_rewrite(CALENDAR *c, int drop, const char *href, const char *etag, const char *raw, long len)
{
	CW w;
	int i;
	if (!cw_open(&w, c))
		return 0;
	cw_copy_begin(&w);
	for (i = 0; i < c->nres; i++)
		if (i != drop && !c->res[i].dead && !(href && !strcmp(c->res[i].href, href)))
			cw_copy(&w, &c->res[i]);
	cw_copy_end(&w);
	if (href)
		cw_add(&w, href, etag, raw, len);
	if (!cw_close(&w))
		return 0;
	cal_load(c);
	return 1;
}

/* a new copy of an event at the end of its cache file (h, *size
   long), replacing the one in memory: 1, 0 if the disk refused, -1 no
   memory. Its events are read with parse_res (resource c->nres - 1) */
/* the resource in memory for a record whose data starts at off in the
   cache file, replacing an older copy: its number, -1 if no memory.
   With *oldp, the older copy is left for the caller to drop (kill_res;
   freeing its events is slow on an ST), its number there, or -1 */
static int add_res_old(CALENDAR *c, long off, const char *href, const char *etag, long len, int *oldp)
{
	int old = hx_live(c, href);
	RESOURCE *r;
	if (c->nres >= c->maxres) {
		int max = c->maxres ? c->maxres * 2 : 64;
		RESOURCE *nr = realloc(c->res, max * sizeof(RESOURCE));
		if (!nr)
			return -1;
		c->res = nr;
		c->maxres = max;
	}
	if (oldp)
		*oldp = old;
	else if (old >= 0)
		kill_res(c, old);
	r = &c->res[c->nres];
	r->etag = strdup(etag ? etag : "");
	r->href = strdup(href);
	r->off = off;
	r->len = len;
	r->dead = 0;
	if (hx && hx_c == c && !hx_room(c, c->nres + 1))
		hx_done();		/* no memory: the slow way */
	c->nres++;
	if (hx && hx_c == c && r->href)
		hx_put(c, c->nres - 1);
	return c->nres - 1;
}

static int add_res(CALENDAR *c, long off, const char *href, const char *etag, long len)
{
	return add_res_old(c, off, href, etag, len, 0);
}

static int res_head(char *head, int size, const char *href, const char *etag, long len)
{
	return snprintf(head, size, "R %s\t%s\t%ld\n", etag ? etag : "", href, len);
}

static int write_res(CALENDAR *c, int h, long *size, const char *href, const char *etag,
		     const char *raw, long len)
{
	char head[700];
	int n = res_head(head, sizeof(head), href, etag, len), ok;
	char *rec;
	if (add_res(c, *size + n, href, etag, len) < 0)
		return -1;
	/* one write, not three (each a GEMDOS call) */
	rec = malloc(n + len + 1);
	if (rec) {
		memcpy(rec, head, n);
		memcpy(rec + n, raw, len);
		rec[n + len] = '\n';
		ok = pf_write(h, rec, n + len + 1) == n + len + 1;
		free(rec);
	} else {
		ok = pf_write(h, head, n) == n && pf_write(h, raw, len) == len && pf_write(h, "\n", 1) == 1;
	}
	*size += n + len + 1;
	return ok;
}

static void parse_res(CALENDAR *c, int res, const char *raw, long len)
{
	LOADCTX l;
	CTIME from, to;
	l.c = c;
	l.res = res;
	window(&from, &to);
	set_me(c);
	ics_parse(raw, len, add_event, &l, from, to);
}

static int append_res(CALENDAR *c, int h, long *size, const char *href, const char *etag,
		      const char *raw, long len)
{
	int ok = write_res(c, h, size, href, etag, raw, len);
	if (ok >= 0)
		parse_res(c, c->nres - 1, raw, len);
	return ok;
}

/* One event changed (or gone): the new copy is added to the end of the
   cache file, and only that event is read again. Writing the whole file
   and reading every event back took seconds on a 68030 for a change of
   one letter. When superseded copies pile up the file is written anew. */
static int cache_change(CALENDAR *c, int drop, const char *href, const char *etag, const char *raw, long len)
{
	char path[200], head[700];
	long size;
	int h, n, ok = 1;
	cache_path(c, "DAT", path, sizeof(path));
	size = pf_size(path);
	if (size <= 0 || c->ndead >= 32)
		return cache_rewrite(c, drop, href, etag, raw, len);
	h = pf_open(path, PF_APPEND);
	if (h < 0)
		return cache_rewrite(c, drop, href, etag, raw, len);
	/* the old copy goes */
	if (drop >= 0 && !c->res[drop].dead && (!href || strcmp(c->res[drop].href, href))) {
		n = snprintf(head, sizeof(head), "X %s\n", c->res[drop].href);
		ok = pf_write(h, head, n) == n;
		size += n;
		kill_res(c, drop);
	}
	if (ok && href) {
		ok = append_res(c, h, &size, href, etag, raw, len);
		if (ok < 0) {
			pf_close(h);
			return cache_rewrite(c, -1, href, etag, raw, len);
		}
	}
	pf_close(h);
	if (!ok) {
		/* the disk refused the end of the file: write it all again */
		cal_load(c);
		return cache_rewrite(c, -1, 0, 0, 0, 0);
	}
	store_mark();
	mark_exceptions(c);
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
	while (cal_load_step())
		;			/* a reading in steps first: the index is its */
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
	/* what the server has, against what is kept: by href through the
	   index (comparing each with each was n x m string compares) */
	hx_done();
	hx_room(c, c->nres + 1);	/* no memory: hx_live looks the slow way */
	for (i = 0; i < n; i++) {
		int found = hx_live(c, items[i].href);
		if (force < SYNC_AGAIN && found >= 0 && same_etag(c->res[found].etag, items[i].etag))
			keep[found] = 1;
		else
			need[nneed++] = items[i].href;
	}
	hx_done();
	if (!cw_open(&w, c)) {
		free(keep);
		free(need);
		caldav_free_items(items, n);
		return -1;
	}
	cw_copy_begin(&w);
	for (j = 0, nkept = 0; j < c->nres; j++)
		if (keep[j]) {
			cw_copy(&w, &c->res[j]);
			nkept++;
		}
	cw_copy_end(&w);		/* before the network's waits */
	if (!nneed && nkept == c->nres - c->ndead) {
		/* the ctag moved (our own changes, say) but nothing is new */
		cw_abort(&w);
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
		cw_abort(&w);
		return -1;
	}
	if (!cw_close(&w))
		return -1;
	str_copy(c->ctag, ctag, sizeof(c->ctag));
	c->synced = now_utc();
	reload(c);
	return 1;
}

/* a download to a file, which isn't kept open while the network is
   waited for (event calls, in the accessory; guidelines 2.2): gathered
   in memory and written in pieces of 8 KB, each opened, added to and
   closed */
typedef struct {
	const char *path;
	SBUF b;
	long n;
	int made;		/* the file begun */
} FSINK;

static int fs_flush(FSINK *f, const char *data, long n)
{
	int h = pf_open(f->path, f->made ? PF_APPEND : PF_WRITE), ok;
	if (h < 0)
		return 0;
	f->made = 1;
	ok = pf_write(h, data, n) == n;
	pf_close(h);
	return ok;
}

static int file_sink(void *ud, const char *data, long n)
{
	FSINK *f = ud;
	f->n += n;
	if (!sb_add(&f->b, data, n)) {
		/* no memory for it: what there is, then this, written now */
		if (f->b.len && !fs_flush(f, f->b.s, f->b.len))
			return 0;
		sb_reset(&f->b);
		if (!fs_flush(f, data, n))
			return 0;
	} else if (f->b.len >= CW_FLUSH) {
		if (!fs_flush(f, f->b.s, f->b.len))
			return 0;
		sb_reset(&f->b);
	}
	if ((f->n & 0xffff) < n)
		status("Downloading... %s bytes", num(f->n));
	return 1;
}

/* the rest written: 0 if the disk refused */
static int fs_close(FSINK *f)
{
	int ok = !f->b.len || fs_flush(f, f->b.s, f->b.len);
	sb_free(&f->b);
	return ok && (f->made || fs_flush(f, "", 0));
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
	memset(&fs, 0, sizeof(fs));
	fs.path = tmp;
	sb_init(&fs.b);
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
	sb_free(&errbody);
	if (ok && rs.status == 200 && !fs_close(&fs)) {
		str_copy(cal_err, "can't write the subscription's cache", sizeof(cal_err));
		ok = 0;
	}
	sb_free(&fs.b);
	if (!ok) {
		if (fs.made)
			pf_remove(tmp);
		return -1;
	}
	if (rs.status == 304) {
		if (fs.made)
			pf_remove(tmp);
		c->synced = now_utc();
		return 0;
	}
	if (rs.status != 200) {
		if (fs.made)
			pf_remove(tmp);
		snprintf(cal_err, sizeof(cal_err), "%s: %s (HTTP %d)", c->name, caldav_status_text(rs.status), rs.status);
		return -1;
	}
	pf_remove(path);
	pf_rename(tmp, path);
	store_mark();
	str_copy(c->etag, rs.etag, sizeof(c->etag));
	str_copy(c->lastmod, rs.lastmod, sizeof(c->lastmod));
	c->synced = now_utc();
	status("%s: reading...", c->name);
	reload(c);
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

/* ---------------- a refresh in steps ---------------- */

/* The accessory's background refresh of a CalDAV calendar, in steps
   between event calls (common/ui/bgwork.c). The requests wait for the
   network as they always have (the waits yield); what is done with the
   answers is cut small: the server's list compared a few entries a
   step, the events that changed fetched a few at a time, and each read
   in its own step and added to the end of the cache file, the way an
   edit is (cache_change). Nothing else is read again. Stopped half way
   (the window opened, ECAL.PRG wrote the files), what was done stays
   and the ctag isn't stored, so the next refresh finishes it. */
enum { SJ_CTAG = 1, SJ_LIST, SJ_INDEX, SJ_MATCH, SJ_GONE, SJ_FETCH, SJ_ADD, SJ_WRITE, SJ_PARSE, SJ_END, SJ_PACK,
       SJ_MARK };
#define SJ_BATCH 8		/* events asked for at a time */
#define SJ_FLUSH cal_gather	/* records gathered before they are written (guidelines 6.5) */

int sync_changes;

static struct {
	CALENDAR *c;
	int phase, force, changed, failed, wfailed;
	char ctag[100];
	DAVREAD *d;
	unsigned char *keep;	/* by resource: 1 on the server as it is, 2 changed there */
	int nres;		/* resources when the list came */
	int at;			/* the next resource (SJ_INDEX, SJ_GONE) */
	char **need;		/* hrefs to fetch */
	int nneed, maxneed, next;
	long size;		/* the cache file's, -1 not looked at yet */
	int fresh;		/* the cache file still to be made */
	SBUF rec;		/* records not written yet (X lines, then events), as they will be */
	struct {
		int res, old;	/* old: the copy it replaces, to drop first; -1 */
		long off, len;	/* its data in rec */
	} pend[SJ_BATCH + 8];	/* SJ_PARSE: the events written, to read */
	int npend, ipend;
	CW w;			/* SJ_PACK: the file written anew */
	long *off;		/* where each resource goes in it */
} sj;

static void sj_free(void)
{
	int i;
	caldav_close(sj.d);
	free(sj.keep);
	for (i = 0; i < sj.nneed; i++)
		free(sj.need[i]);
	free(sj.need);
	sb_free(&sj.rec);
	if (sj.off) {
		/* the new file unfinished: the old one stays */
		cw_abort(&sj.w);
		free(sj.off);
	}
	if (sj.c && hx_c == sj.c)
		hx_done();
	memset(&sj, 0, sizeof(sj));
}

int sync_begin(CALENDAR *c, int force)
{
	int k = accts[c->acct].kind;
	sj_free();
	if (k == AK_LOCAL || k == AK_SUBSCR)
		return 0;		/* (a subscription: sync_calendar, read in steps) */
	cal_err[0] = 0;
	sj.c = c;
	sj.force = force;
	sj.phase = SJ_CTAG;
	sj.size = -1;
	return 1;
}

int sync_busy(void)
{
	return sj.c != 0;
}

/* what the next step does (the profile build's label) */
const char *sync_phase_name(void)
{
	static const char *const n[] = { "", "ctag", "list", "index", "compare", "gone", "fetch", "take",
					 "write", "read", "end", "pack", "stamps" };
	return sj.c && sj.phase > 0 && sj.phase <= SJ_MARK ? n[sj.phase] : "";
}

int sync_net_step(void)
{
	return sj.phase == SJ_CTAG || sj.phase == SJ_LIST || sj.phase == SJ_FETCH;
}

static int sj_write(void);

void sync_stop(void)
{
	/* the events taken from the answer: read, and what was gathered
	   written (their places in memory are in the file) */
	for (; sj.c && (sj.phase == SJ_ADD || sj.phase == SJ_PARSE) && sj.ipend < sj.npend; sj.ipend++) {
		if (sj.pend[sj.ipend].old >= 0)
			kill_res(sj.c, sj.pend[sj.ipend].old);
		parse_res(sj.c, sj.pend[sj.ipend].res, sj.rec.s + sj.pend[sj.ipend].off, sj.pend[sj.ipend].len);
	}
	if (sj.c)
		sj_write();
	sj_free();
}

/* the end before the end: nothing new, or failed (the error kept in
   the account, shown at the next opening) */
static int sj_end(void)
{
	CALENDAR *c = sj.c;
	char err[sizeof(cal_err)];
	str_copy(err, cal_err, sizeof(err));
	sj_write();		/* what was gathered: its places in memory are in the file */
	if (sj.failed)
		str_copy(accts[c->acct].err, err[0] ? err : cal_err, sizeof(accts[c->acct].err));
	sync_changes |= sj.changed;
	sj_free();
	return 0;
}

/* the cache file's size (where the next record goes) */
static void sj_size(void)
{
	char path[200];
	if (sj.size >= 0)
		return;
	cache_path(sj.c, "DAT", path, sizeof(path));
	sj.size = pf_size(path);
	if (sj.size <= 0) {
		sj.fresh = 1;
		sj.size = 11;		/* after the header it starts with */
	}
}

/* the cache file, opened to add to its end */
static int sj_open(void)
{
	char path[200];
	int h;
	sj_size();
	cache_path(sj.c, "DAT", path, sizeof(path));
	if (sj.fresh) {
		h = pf_open(path, PF_WRITE);
		if (h >= 0 && pf_write(h, "CALCACHE 1\n", 11) != 11) {
			pf_close(h);
			h = -1;
		}
		sj.fresh = 0;
	} else {
		h = pf_open(path, PF_APPEND);
	}
	if (h < 0) {
		str_copy(cal_err, "can't write the calendar's cache (disk full or locked?)", sizeof(cal_err));
		sj.failed = 1;
	}
	return h;
}

/* the records gathered, at the end of the cache file in one write: 0 if
   it failed */
static int sj_write(void)
{
	int h;
	if (!sj.rec.len || sj.wfailed)
		return !sj.wfailed;
	if ((h = sj_open()) < 0) {
		sj.wfailed = 1;
		return 0;
	}
	if (pf_write(h, sj.rec.s, sj.rec.len) != sj.rec.len) {
		str_copy(cal_err, "couldn't write the calendar's cache (disk full?)", sizeof(cal_err));
		sj.failed = sj.wfailed = 1;
	}
	pf_close(h);
	sj.size += sj.rec.len;
	sb_reset(&sj.rec);
	return !sj.wfailed;
}

/* one step; 1 while there is more */
int sync_step(void)
{
	CALENDAR *c = sj.c;
	DAVAUTH au;
	const char *href, *etag, *data;
	long len;
	int i;
	if (!c)
		return 0;
	if (sj.failed)
		return sj_end();
	au = auth_of(c->acct);
	switch (sj.phase) {
	case SJ_CTAG:
		if (!caldav_ctag(c->url, &au, sj.ctag, sizeof(sj.ctag), cal_err, sizeof(cal_err))) {
			sj.failed = 1;
			return 1;
		}
		if (!sj.force && sj.ctag[0] && !strcmp(sj.ctag, c->ctag) && c->synced)
			return sj_end();	/* nothing new */
		sj.phase = SJ_LIST;
		return 1;
	case SJ_LIST: {
		CTIME from, to;
		window(&from, &to);
		from = tz_to_utc(view_zone(), from);
		to = tz_to_utc(view_zone(), to);
		sj.d = caldav_list_open(c->url, &au, from, to, cal_err, sizeof(cal_err));
		sj.nres = c->nres;
		sj.keep = calloc(sj.nres + 1, 1);
		if (!sj.d || !sj.keep) {
			if (sj.d)
				str_copy(cal_err, "out of memory", sizeof(cal_err));
			sj.failed = 1;
			return 1;
		}
		/* what is kept, by href (hx_live): the index made first, a
		   few hundred a step */
		if (!hx_alloc(c, sj.nres + 16))
			hx_done();	/* no memory: hx_live looks the slow way */
		sj.at = 0;
		sj.phase = hx ? SJ_INDEX : SJ_MATCH;
		return 1;
	}
	case SJ_INDEX:
		i = sj.at + 200 < sj.nres ? sj.at + 200 : sj.nres;
		hx_fill(c, sj.at, i);
		sj.at = i;
		if (sj.at >= sj.nres)
			sj.phase = SJ_MATCH;
		return 1;
	case SJ_MATCH:
		for (i = 0; i < 2; i++) {
			int found;
			if (!caldav_next(sj.d, &href, &etag, &data, &len)) {
				caldav_close(sj.d);
				sj.d = 0;
				sj.at = 0;
				sj.phase = SJ_GONE;
				return 1;
			}
			found = hx_live(c, href);
			if (sj.force < SYNC_AGAIN && found >= 0 && found < sj.nres &&
			    same_etag(c->res[found].etag, etag)) {
				sj.keep[found] = 1;
				continue;
			}
			if (found >= 0 && found < sj.nres)
				sj.keep[found] = 2;	/* its new copy replaces it (append_res) */
			if (sj.nneed >= sj.maxneed) {
				int max = sj.maxneed ? sj.maxneed * 2 : 16;
				char **n = realloc(sj.need, max * sizeof(char *));
				if (!n)
					break;
				sj.need = n;
				sj.maxneed = max;
			}
			if (!(sj.need[sj.nneed] = strdup(href)))
				break;
			sj.nneed++;
		}
		if (i < 2) {
			str_copy(cal_err, "out of memory", sizeof(cal_err));
			sj.failed = 1;
		}
		return 1;
	case SJ_GONE: {
		/* gone from the server (or out of the window now): an X line
		   each, a few a step, gathered with the rest */
		int n = 0;
		for (; sj.at < sj.nres && n < 2; sj.at++) {
			if (sj.keep[sj.at] || c->res[sj.at].dead)
				continue;
			if (!sb_adds(&sj.rec, "X ") || !sb_adds(&sj.rec, c->res[sj.at].href) || !sb_addc(&sj.rec, '\n')) {
				str_copy(cal_err, "out of memory", sizeof(cal_err));
				sj.failed = 1;
				return 1;
			}
			kill_res(c, sj.at);
			sj.changed = 1;
			n++;
		}
		if (sj.at >= sj.nres)
			sj.phase = sj.nneed ? SJ_FETCH : SJ_END;
		return 1;
	}
	case SJ_FETCH: {
		int n = sj.nneed - sj.next < SJ_BATCH ? sj.nneed - sj.next : SJ_BATCH;
		sj.d = caldav_multiget_open(c->url, &au, sj.need + sj.next, n, cal_err, sizeof(cal_err));
		if (!sj.d) {
			sj.failed = 1;
			return 1;
		}
		sj.next += n;
		sj.phase = SJ_ADD;
		return 1;
	}
	case SJ_ADD:
		/* the batch's events taken from the answer one a step (60 ms
		   each on an ST) and each read in a step; written to the cache
		   SJ_FLUSH at a time and at the end (opening the file is a
		   GEMDOS call and a look in the folder) */
		sj_size();
		if (sj.npend < SJ_BATCH + 8 && caldav_next(sj.d, &href, &etag, &data, &len)) {
			char head[700];
			int hn, r;
			if (!data || !len)
				return 1;
			hn = res_head(head, sizeof(head), href, etag, len);
			if ((sj.rec.len + hn) & 1 && hn < (int)sizeof(head) - 1) {
				/* the text at an even place, so it is copied a long
				   word at a time on a 68000 (a space after the length
				   is read past) */
				head[hn - 1] = ' ';
				head[hn++] = '\n';
			}
			if (!sj.rec.cap)
				sb_reserve(&sj.rec, 16384);
			r = add_res_old(c, sj.size + sj.rec.len + hn, href, etag, len, &sj.pend[sj.npend].old);
			sj.changed = 1;
			if (r < 0 || !sb_add(&sj.rec, head, hn) || !sb_add(&sj.rec, data, len) || !sb_addc(&sj.rec, '\n')) {
				str_copy(cal_err, "out of memory", sizeof(cal_err));
				sj.failed = 1;
				return 1;
			}
			sj.pend[sj.npend].res = r;
			sj.pend[sj.npend].off = sj.rec.len - len - 1;
			sj.pend[sj.npend++].len = len;
			return 1;
		}
		caldav_close(sj.d);
		sj.d = 0;
		sj.phase = sj.npend ? SJ_PARSE : sj.next < sj.nneed ? SJ_FETCH : SJ_END;
		return 1;
	case SJ_WRITE:
		if (!sj_write())
			return 1;
		sj.phase = sj.next < sj.nneed ? SJ_FETCH : SJ_END;
		return 1;
	case SJ_PARSE:
		i = sj.ipend;
		if (sj.pend[i].old >= 0) {
			kill_res(c, sj.pend[i].old);	/* the copy it replaces: a step of its own */
			sj.pend[i].old = -1;
			return 1;
		}
		sj.ipend++;
		parse_res(c, sj.pend[i].res, sj.rec.s + sj.pend[i].off, sj.pend[i].len);
		if (sj.ipend >= sj.npend) {
			sj.npend = sj.ipend = 0;
			sj.phase = sj.rec.len >= SJ_FLUSH ? SJ_WRITE : sj.next < sj.nneed ? SJ_FETCH : SJ_END;
		}
		return 1;
	case SJ_END:
		if (sj.rec.len) {
			sj_write();	/* the rest: a step of its own */
			return 1;
		}
		if (sj.changed)
			mark_exceptions(c);
		str_copy(c->ctag, sj.ctag, sizeof(c->ctag));
		c->synced = now_utc();
		sync_changes |= sj.changed | 2;
		if (!sj.changed) {
			sj_free();
			return 0;
		}
		sj.phase = SJ_MARK;
		if (c->ndead >= 32) {
			/* the superseded copies piling up: the file written
			   anew, an event a step */
			sj.off = malloc((c->nres + 1) * sizeof(long));
			if (sj.off && cw_open(&sj.w, c)) {
				sj.size = 11;
				sj.at = 0;
				sj.phase = SJ_PACK;
			} else {
				free(sj.off);
				sj.off = 0;
			}
		}
		return 1;
	case SJ_PACK:
		for (; sj.at < c->nres && c->res[sj.at].dead; sj.at++)
			;
		if (sj.at < c->nres) {
			RESOURCE *r = &c->res[sj.at];
			char head[700];
			int n = snprintf(head, sizeof(head), "R %s\t%s\t%ld\n", r->etag ? r->etag : "", r->href, r->len);
			sj.off[sj.at++] = sj.size + n;
			sj.size += n + r->len + 1;
			cw_copy(&sj.w, r);
			if (sj.w.failed) {
				str_copy(cal_err, "couldn't write the calendar's cache (disk full?)", sizeof(cal_err));
				sj.failed = 1;
			}
			return 1;
		}
		{
			/* the new file in place of the old; the resources
			   renumbered without the dead ones */
			int *map = malloc((c->nres + 1) * sizeof(int)), n = 0;
			if (!map) {
				str_copy(cal_err, "out of memory", sizeof(cal_err));
				sj.failed = 1;
				return 1;
			}
			if (!cw_close(&sj.w)) {
				free(map);
				sj.failed = 1;
				return 1;
			}
			for (i = 0; i < c->nres; i++) {
				if (c->res[i].dead) {
					free(c->res[i].etag);
					free(c->res[i].href);
					map[i] = -1;
					continue;
				}
				map[i] = n;
				c->res[n] = c->res[i];
				c->res[n++].off = sj.off[i];
			}
			for (i = 0; i < c->nev; i++)
				if (c->ev[i]->res >= 0 && c->ev[i]->res < c->nres)
					c->ev[i]->res = (short)map[c->ev[i]->res];
			c->nres = n;
			c->ndead = 0;
			free(map);
			free(sj.off);
			sj.off = 0;
			if (hx_c == c)
				hx_done();
			sj.phase = SJ_MARK;
		}
		return 1;
	case SJ_MARK:
		/* the files as written now are ours: taken at the next look,
		   so that it doesn't take them for ECAL.PRG's */
		store_mark();
		sj_free();
		return 0;
	}
	sj_free();
	return 0;
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
	if (l->limit && l->n >= l->limit)
		return;
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

/* ---------------- reminders: the queue (alarms.h) ---------------- */

static int ab_active, ab_ci, ab_ei;
static CTIME ab_now, ab_from, ab_to;	/* this step's day of the event's range */
static const TZONE *ab_vz;

static void alarm_occ(EVENT *e, CTIME s, CTIME en, void *ud)
{
	CTIME fire;
	(void)en;
	(void)ud;
	if ((e->hasexc & 2) && e->recurid == CT_NONE) {
		/* moved by a "this and all future" change: as that one is */
		EVENT *x = future_exception(e, ev_from_wall(e, s, ab_vz));
		if (x) {
			s += x->start - x->recurid;
			e = x;
		}
	}
	if (e->alarm == -1 || e->status == ST_CANCELLED || e->gone)
		return;
	if (s < ab_from || s >= ab_to)
		return;			/* another step's day (or one before the range) */
	fire = s - e->alarm;
	if (fire > ab_now - MIN_PER_DAY && fire <= ab_now + ALARM_DAYS * MIN_PER_DAY)
		alarms_add(view_to_clock(fire), s, e->allday, e->summary ? e->summary : "Event", e->location);
}

/* One day of one event's range per step (an event of the calendars that
   remind, over the days whose reminders it can have), so no step holds
   the machine long: a daily meeting is a few occurrences, each with its
   time zones worked out. 0 once all have been */
static CTIME ab_end;		/* the end of this event's range; CT_NONE: the next event */

/* the next step sorts the queue (the profile build's label) */
int alarms_sorting(void)
{
	return ab_active && ab_ci >= ncals;
}

int alarms_step(void)
{
	int skipped = 0;
	if (ab_active && alarms_stale())
		ab_active = 0;		/* changed meanwhile (read again, say): from the start */
	if (!ab_active) {
		if (!alarms_pending())
			return 0;
		alarms_begin_build();
		ab_active = 1;
		ab_ci = ab_ei = 0;
		ab_end = CT_NONE;
		ab_now = now_view();
		ab_vz = view_zone();
	}
	while (ab_ci < ncals) {
		CALENDAR *c = &cals[ab_ci];
		EVENT *e;
		if (ab_end == CT_NONE) {
			/* the next event: its range */
			if (!c->visible || !c->alarms || ab_ei >= c->nev) {
				ab_ci++;
				ab_ei = 0;
				continue;
			}
			e = c->ev[ab_ei];
			if (e->status == ST_CANCELLED || e->gone || (e->alarm == -1 && !(e->hasexc & 2))) {
				ab_ei++;
				if (++skipped >= 64)
					return 1;	/* a long run of them is a step too */
				continue;	/* nothing to remind of: no step spent on it */
			}
			if (e->hasexc & 2) {
				/* its later part may remind differently */
				ab_from = ab_now - 2 * MIN_PER_DAY;
				ab_end = ab_now + 2 * ALARM_DAYS * MIN_PER_DAY;
			} else {
				/* fire = start - alarm, within the next ALARM_DAYS days */
				ab_from = ab_now - MIN_PER_DAY + e->alarm - 1;
				ab_end = ab_now + ALARM_DAYS * MIN_PER_DAY + e->alarm + 1;
			}
			if ((!e->rr && !e->nrdate) || (e->rr && (e->rr->freq == F_MONTHLY || e->rr->freq == F_YEARLY) && !e->nrdate)) {
				/* once, or monthly or less: the whole range in one look
				   (a month's days are worked out once, not each day) */
				ab_to = ab_end;
			} else {
				ab_to = ab_from + MIN_PER_DAY;
			}
		} else if (ab_ei < c->nev) {
			e = c->ev[ab_ei];
		} else {
			ab_end = CT_NONE;
			continue;
		}
		/* the occurrences starting this day (ab_from..ab_to) */
		ics_expand(e, ab_from, ab_to, ab_vz, alarm_occ, occ_skip, 0);
		ab_from = ab_to;
		if (ab_from >= ab_end) {
			ab_end = CT_NONE;
			ab_ei++;
		} else {
			ab_to = ab_from + MIN_PER_DAY < ab_end ? ab_from + MIN_PER_DAY : ab_end;
		}
		return 1;
	}
	if (skipped)
		return 1;		/* the sorting in a step of its own */
	alarms_end_build(now_clock());
	ab_active = 0;
	return 0;
}

/* by date only, all-day ones among the others (the Find list) */
static int occ_cmp_date(const void *a, const void *b)
{
	const OCC *x = a, *y = b;
	return x->start < y->start ? -1 : x->start > y->start;
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
			/* cancelled (a whole series too), "Canceled:", declined */
			if (e->status == ST_CANCELLED || e->gone)
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

/* Only the events that match are expanded: expanding every event over
   three years and filtering after built a list of tens of thousands of
   occurrences (every daily meeting), slow on a 68030 and enough to
   exhaust memory */
void occ_search(OCCLIST *l, const char *text, CTIME from, CTIME to)
{
	const TZONE *vz = view_zone();
	int i, j;
	l->n = 0;
	l->from = from;
	l->to = to;
	l->limit = 3000;	/* the list shows 200; more is only memory */
	for (i = 0; i < ncals; i++) {
		CALENDAR *c = &cals[i];
		if (!c->visible)
			continue;
		for (j = 0; j < c->nev; j++) {
			EVENT *e = c->ev[j];
			if (e->status == ST_CANCELLED || e->gone)
				continue;
			if (contains(e->summary, text) || contains(e->location, text) || contains(e->notes, text))
				ics_expand(e, from, to, vz, occ_add, occ_skip, l);
		}
	}
	if (l->n > 1)
		qsort(l->o, l->n, sizeof(OCC), occ_cmp_date);
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

/* the people invited to e and who invited them, read from its text in
   the cache (not kept with every event read: see ics_people) */
typedef struct {
	const EVENT *want;
	EVENT *got;
} PEOPLECTX;

static int people_of(EVENT *e, void *ud)
{
	PEOPLECTX *p = ud;
	if (!p->got && e->recurid == p->want->recurid && e->uid && p->want->uid && !strcmp(e->uid, p->want->uid)) {
		p->got = e;
		return 0;	/* that's all */
	}
	ev_free(e);
	return 1;
}

void ev_people(EVENT *e)
{
	PEOPLECTX p;
	char *raw;
	long len;
	if (e->attendees || e->organizer || !(raw = ev_raw(e, &len)))
		return;
	p.want = e;
	p.got = 0;
	ics_people = 1;
	ics_parse(raw, len, people_of, &p, CT_NONE, CT_NONE);
	ics_people = 0;
	if (p.got) {
		e->attendees = p.got->attendees;
		e->organizer = p.got->organizer;
		p.got->attendees = p.got->organizer = 0;
		ev_free(p.got);
	}
	free(raw);
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
	set_me(c);		/* the organizer of its invitations */
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

/* An event to another calendar: made there, then deleted here, so that a
   failure leaves it where it was. iCloud keeps one event per UID in an
   account, all its calendars together: the copy is refused while the
   original is there (412). Then, within one account, the other way
   round: deleted here, then made there, and if that fails it is put
   back here. */
static int move_event(EVENT *e, int ci)
{
	CALENDAR *from = &cals[e->cal], *to = &cals[ci];
	EVENT *copy = ev_dup(e);
	char *raw, *text = 0, href[120], *oldhref = 0;
	long rawlen = 0, len;
	int ok = 0, r = e->res;
	if (!copy)
		return 0;
	raw = ev_raw(e, &rawlen);
	copy->res = -1;
	copy->recurid = CT_NONE;
	if (raw) {
		new_href(copy->uid, href, sizeof(href));
		text = ics_build(copy, raw, rawlen, &len);
	}
	ev_free(copy);
	if (!raw || !text) {
		if (!raw)
			str_copy(cal_err, "the event's data is missing from the cache: refresh and try again", sizeof(cal_err));
		free(raw);
		free(text);
		return 0;
	}
	if (put_resource(to, -1, href, text, len)) {
		ok = delete_resource(from, r);
	} else if (from->acct == to->acct && accts[to->acct].kind == AK_CALDAV &&
		   (caldav_status == 412 || caldav_status == 409 || caldav_status == 403) &&
		   (oldhref = strdup(from->res[r].href)) != 0) {
		char why[sizeof(cal_err)];
		if (delete_resource(from, r)) {
			ok = put_resource(to, -1, href, text, len);
			if (!ok) {
				str_copy(why, cal_err, sizeof(why));
				if (put_resource(from, -1, oldhref, raw, rawlen))
					snprintf(cal_err, sizeof(cal_err), "%s; it stays in %s", why, from->name);
				else
					snprintf(cal_err, sizeof(cal_err), "%s, and it couldn't be put back in %s: "
						 "refresh to see where it is", why, from->name);
			}
		}
	}
	free(oldhref);
	free(text);
	free(raw);
	return ok;
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
	set_me(&cals[ci]);	/* the organizer of its invitations */
	if (e->res < 0)
		return save_new(e, ci);
	c = &cals[e->cal];
	res = e->res;
	m = ev_master(e);
	/* moving to another calendar: new there, then gone here */
	if (ci != e->cal)
		return move_event(e, ci);
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
