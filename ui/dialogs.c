/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dialogs.c - adding and changing accounts, a calendar's details, the
 * settings, the connection, going to a date, finding events, the keys,
 * About, and picking a time zone.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "util.h"
#include "conn.h"
#include "../src/http.h"
#include "../src/version.h"
#include "../src/plat.h"
#include "tls.h"
#include "bgwork.h"
#include "dialog.h"

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

static void text(FIELD *f, short flags, short x, short y, short w, char *buf, short max)
{
	put(f, FT_TEXT, flags, x, y, w, 0);
	f->buf = buf;
	f->max = max;
}

static short run(const char *title, FIELD *f, short n, short w, short h, short focus)
{
	DLG d;
	memset(&d, 0, sizeof(d));
	d.title = title;
	d.f = f;
	d.n = n;
	d.w = w;
	d.h = h;
	d.focus = focus;
	return dlg_run(&d);
}

/* ---------------- time zones ---------------- */

/* a dialog's big lists come from the heap while it is open, not kept in
   the program: a 1 MB ST needs the room */
void no_memory(void)
{
	mem_ran_out();
	alert(1, "[1][Memory is full: ECal can't do|that now. %s][ OK ]", mem_full_advice());
}

/* the time zones, a list dialog (a long list is not a pop-up: the
   guidelines, 5.7): the name chosen, or 0. With floating, "Floating (any
   zone)" comes first, given back as "" */
const char *zone_pick(const char *current, int floating)
{
	char (*lab)[48];
	const char **items;
	int n = tz_count(), i, sel = 0, f = floating ? 1 : 0;
	CTIME now = now_utc();
	if (n > 100)
		n = 100;
	/* the labels and the list of them in one block from the heap */
	lab = malloc(n * sizeof(*lab) + (n + 1) * sizeof(*items));
	if (!lab) {
		no_memory();
		return 0;
	}
	items = (const char **)(lab + n);
	if (f)
		items[0] = "Floating (any zone)";
	for (i = 0; i < n; i++) {
		const TZONE *z = tz_index(i);
		char off[16];
		tz_label(z, now, off, sizeof(off));
		snprintf(lab[i], sizeof(lab[i]), "%-30s %s", z->name, off);
		items[f + i] = lab[i];
		if (current && !strcmp(z->name, current))
			sel = f + i;
	}
	i = dlg_pick("Time zone", items, (short)(n + f), (short)sel);
	free(lab);
	return i < 0 ? 0 : i < f ? "" : tz_index(i - f)->name;
}

/* ---------------- accounts ---------------- */

static int connection_ready(const char *url)
{
	URL u;
	if (!url_parse(url, &u) || !u.tls || opt.falcon || opt.gate[0])
		return 1;
	if (alert(1, "[1][Calendar servers want a secure connection. Set up the Gateway, or a Direct connection, in Settings, Secure.][Settings|Cancel]") == 1)
		dlg_connection();
	return opt.falcon || opt.gate[0];
}

static int try_account(int a)
{
	int r;
	busy(1);
	r = sync_account(a, 1);
	busy(0);
	if (r < 0) {
		alert(1, "[1][%s: %s][ OK ]", accts[a].name, cal_err);
		return 0;
	}
	return 1;
}

static int edit_caldav(ACCOUNT *a, int is_new, int icloud)
{
	static char name[40], url[400], user[80], pass[80];
	enum { L1, F1, L2, F2, L3, F3, L4, F4, N1, N2, N3, BDEL, BCAN, BOK, N };
	FIELD f[N];
	short r, row = 1;
	str_copy(name, a->name, sizeof(name));
	str_copy(url, a->url, sizeof(url));
	str_copy(user, a->user, sizeof(user));
	str_copy(pass, a->pass, sizeof(pass));
	put(&f[L1], FT_LABEL, 0, 0, row, 0, "Name:");
	text(&f[F1], 0, 13, row, 30, name, sizeof(name));
	row++;
	put(&f[L2], FT_LABEL, icloud ? FF_DISABLED : 0, 0, row, 0, icloud ? "" : "Server:");
	text(&f[F2], FF_LTR | (icloud ? FF_DISABLED : 0), 13, row, 42, url, sizeof(url));
	if (icloud)
		f[F2].type = FT_LABEL, f[F2].label = "";
	row++;
	put(&f[L3], FT_LABEL, 0, 0, row, 0, icloud ? "Apple ID:" : "User name:");
	text(&f[F3], FF_LTR, 13, row, 42, user, sizeof(user));
	row++;
	put(&f[L4], FT_LABEL, 0, 0, row, 0, "Password:");
	text(&f[F4], FF_LTR, 13, row, 30, pass, sizeof(pass));
	f[F4].type = FT_PASS;
	row += 2;
	put(&f[N1], FT_LABEL, FF_LTR, 0, row, 0, icloud ? "iCloud needs an app-specific password: make one at"
						    : "e.g. https://caldav.fastmail.com/  https://cloud.example.org/remote.php/dav");
	put(&f[N2], FT_LABEL, FF_LTR, 0, row + 1, 0, icloud ? "appleid.apple.com > Sign-In and Security >"
						        : "or the address of one calendar. Google wants OAuth, which");
	put(&f[N3], FT_LABEL, FF_LTR, 0, row + 2, 0, icloud ? "App-Specific Passwords, and type it here."
						        : "ECal can't do: subscribe to its secret iCal address.");
	row += 4;
	put(&f[BDEL], FT_BUTTON, is_new ? FF_DISABLED : FF_DANGER, 0, row, 10, "Delete");
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 34, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, 46, row, 10, is_new ? "Sign In" : "OK");
	for (;;) {
		r = run(icloud ? "iCloud account" : "CalDAV account", f, N, 57, row + 1, is_new ? F3 : F1);
		if (r == BDEL)
			return -1;
		if (r != BOK)
			return 0;
		if (!url[0] && !icloud) {
			alert(1, "[1][Please type the server's address.][ OK ]");
			continue;
		}
		break;
	}
	str_copy(a->name, name[0] ? name : icloud ? "iCloud" : "Calendar", sizeof(a->name));
	if (!icloud) {
		if (strncasecmp(url, "http", 4) && strncasecmp(url, "webcal", 6)) {
			char tmp[220];
			snprintf(tmp, sizeof(tmp), "https://%s", url);
			str_copy(url, tmp, sizeof(url));
		}
		str_copy(a->url, url, sizeof(a->url));
	} else {
		str_copy(a->url, "https://caldav.icloud.com/", sizeof(a->url));
	}
	str_copy(a->user, user, sizeof(a->user));
	str_copy(a->pass, pass, sizeof(a->pass));
	memset(pass, 0, sizeof(pass));
	return 1;
}

static const char *const refresh_items[] = {
	"  Never", "  Every 15 minutes", "  Every hour", "  Every day", "  Every week"
};
static const short refresh_min[] = { 0, 15, 60, 1440, 10080 };

/* when the accessory reads the calendars (START=, common/ui/bgwork.h) */
static const char *const start_val[4] = { "now", "60", "300", "opened" };

static int edit_subscription(ACCOUNT *a, CALENDAR *c, int is_new)
{
	static char url[400], name[64], user[80], pass[80];
	enum { L1, F1, L2, F2, L3, F3, L4, F4, L5, F5, N1, BDEL, BCAN, BOK, N };
	FIELD f[N];
	short r, row = 1, i;
	str_copy(url, c ? c->url : a->url, sizeof(url));
	str_copy(name, c ? c->name : "", sizeof(name));
	str_copy(user, a->user, sizeof(user));
	str_copy(pass, a->pass, sizeof(pass));
	put(&f[L1], FT_LABEL, 0, 0, row, 0, "Address:");
	text(&f[F1], FF_LTR, 12, row, 46, url, sizeof(url));
	row++;
	put(&f[L2], FT_LABEL, 0, 0, row, 0, "Name:");
	text(&f[F2], 0, 12, row, 30, name, sizeof(name));
	row++;
	put(&f[L3], FT_LABEL, 0, 0, row, 0, "Refresh:");
	put(&f[F3], FT_POPUP, 0, 12, row, 22, 0);
	f[F3].items = refresh_items;
	f[F3].nitems = 5;
	f[F3].val = 2;
	for (i = 0; i < 5; i++)
		if (refresh_min[i] == a->refresh)
			f[F3].val = i;
	row += 2;
	put(&f[L4], FT_LABEL, 0, 0, row, 0, "User:");
	text(&f[F4], FF_LTR, 12, row, 30, user, sizeof(user));
	row++;
	put(&f[L5], FT_LABEL, 0, 0, row, 0, "Password:");
	text(&f[F5], FF_LTR, 12, row, 30, pass, sizeof(pass));
	f[F5].type = FT_PASS;
	put(&f[N1], FT_LABEL, FF_LTR, 0, row + 1, 0, "(only for calendars that need a login)");
	row += 3;
	put(&f[BDEL], FT_BUTTON, is_new ? FF_DISABLED : 0, 0, row, 12, "Unsubscribe");
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 36, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, 48, row, 10, is_new ? "Subscribe" : "OK");
	for (;;) {
		URL u;
		r = run("Calendar subscription", f, N, 58, row + 1, F1);
		if (r == BDEL)
			return -1;
		if (r != BOK)
			return 0;
		if (!url_parse(url, &u)) {
			alert(1, "[1][That isn't a calendar address: it starts with webcal:// or https://][ OK ]");
			continue;
		}
		break;
	}
	str_copy(a->url, url, sizeof(a->url));
	str_copy(a->name, "Subscriptions", sizeof(a->name));
	str_copy(a->user, user, sizeof(a->user));
	str_copy(a->pass, pass, sizeof(a->pass));
	a->refresh = refresh_min[f[F3].val];
	if (c) {
		if (strcmp(c->url, url)) {
			c->etag[0] = c->lastmod[0] = 0;
			str_copy(c->url, url, sizeof(c->url));
		}
		str_copy(c->name, name[0] ? name : "Subscription", sizeof(c->name));
	}
	return 1;
}

/* a published calendar: the address asked for (or given, from a
   webcal: link a program sent with VA_START), then its events */
void dlg_subscribe(const char *url)
{
	ACCOUNT *a;
	CALENDAR *c;
	int ai;
	a = acct_add(AK_SUBSCR);
	if (!a) {
		alert(1, "[1][There are too many accounts.][ OK ]");
		return;
	}
	ai = naccts - 1;
	c = cal_add(ai, "Subscription");
	if (c && url)
		str_copy(c->url, url, sizeof(c->url));
	if (!c || edit_subscription(a, c, 1) <= 0 || !connection_ready(a->url)) {
		acct_delete(ai);
		store_save_settings();
		return;
	}
	busy(1);
	if (sync_calendar(c, 1) < 0) {
		busy(0);
		alert(1, "[1][%s][ OK ]", cal_err);
	}
	busy(0);
	store_save_settings();
	after_change();
}

void dlg_add_account(void)
{
	enum { L1, B1, B2, B3, B4, BCAN, N };
	FIELD f[N];
	short r;
	put(&f[L1], FT_LABEL, 0, 0, 1, 0, "Which kind of calendar?");
	put(&f[B1], FT_BUTTON, FF_DEFAULT, 0, 3, 40, "iCloud");
	put(&f[B2], FT_BUTTON, 0, 0, 4, 40, "Other CalDAV account...");
	put(&f[B3], FT_BUTTON, 0, 0, 5, 40, "Subscribe to a published calendar...");
	put(&f[B4], FT_BUTTON, 0, 0, 6, 40, "On My Atari (no server)");
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 30, 8, 10, "Cancel");
	r = run("Add a calendar", f, N, 40, 9, -1);
	if (r == B1 || r == B2) {
		ACCOUNT *a = acct_add(AK_CALDAV);
		int ai;
		if (!a) {
			alert(1, "[1][There are too many accounts.][ OK ]");
			return;
		}
		ai = naccts - 1;
		if (r == B1) {
			str_copy(a->name, "iCloud", sizeof(a->name));
			str_copy(a->url, "https://caldav.icloud.com/", sizeof(a->url));
		}
		for (;;) {
			if (edit_caldav(a, 1, r == B1) <= 0 || !connection_ready(a->url)) {
				acct_delete(ai);
				store_save_settings();
				return;
			}
			if (try_account(ai))
				break;
			if (alert(2, "[2][Try again with other settings?][Edit|Give up]") != 1) {
				acct_delete(ai);
				store_save_settings();
				after_change();
				return;
			}
		}
		store_save_settings();
		after_change();
	} else if (r == B3) {
		dlg_subscribe("");
	} else if (r == B4) {
		char name[64] = "Atari";
		int i, a = -1;
		if (!dlg_ask("New calendar on this Atari", "Name:", name, sizeof(name) - 1) || !name[0])
			return;
		for (i = 0; i < naccts; i++)
			if (accts[i].kind == AK_LOCAL)
				a = i;
		if (a < 0) {
			ACCOUNT *ac = acct_add(AK_LOCAL);
			if (!ac)
				return;
			str_copy(ac->name, "On My Atari", sizeof(ac->name));
			a = naccts - 1;
		}
		cal_add(a, name);
		store_save_settings();
		after_change();
	}
}

int dlg_account(int a)
{
	ACCOUNT *ac = &accts[a];
	int r;
	if (ac->kind == AK_SUBSCR) {
		int i;
		CALENDAR *c = 0;
		for (i = 0; i < ncals; i++)
			if (cals[i].acct == a)
				c = &cals[i];
		r = edit_subscription(ac, c, 0);
	} else if (ac->kind == AK_CALDAV) {
		r = edit_caldav(ac, 0, strstr(ac->url, "icloud.com") != 0);
	} else {
		return 0;
	}
	if (r < 0) {
		if (alert(2, "[2][Delete \"%s\" from this Atari?|(Nothing is deleted on the server.)][Delete|Cancel]", ac->name) == 1) {
			views_select_none();
			acct_delete(a);
			store_save_settings();
			return -1;
		}
		return 0;
	}
	if (r > 0) {
		http_close_all();
		store_save_settings();
		try_account(a);
	}
	return r;
}

/* 1 if anything changed (the window is drawn again only then) */
int dlg_calendar(int ci)
{
	static char name[64];
	CALENDAR *c = &cals[ci];
	ACCOUNT *a = &accts[c->acct];
	enum { L1, F1, L2, F2, F3, L4, L5, L6, L7, BCAN, BOK, N };
	FIELD f[N];
	char l4[90], l5[90], l6[80], l7[80];
	short r, row = 1;
	int renamable = a->kind != AK_CALDAV;
	str_copy(name, c->name, sizeof(name));
	put(&f[L1], FT_LABEL, 0, 0, row, 0, "Name:");
	text(&f[F1], renamable ? 0 : FF_DISABLED, 10, row, 34, name, sizeof(name));
	row += 2;
	put(&f[L2], FT_LABEL, 0, 0, row, 0, "Colour:");
	put(&f[F2], FT_COLOR, 0, 10, row, NCOLORS * 3, 0);
	f[F2].val = c->color;
	row += 2;
	put(&f[F3], FT_CHECK, 0, 10, row, 30, "Show its alerts");
	f[F3].val = c->alarms;
	row += 2;
	snprintf(l4, sizeof(l4), "Account: %s%s", a->name, cal_writable(c) ? "" : " (read-only)");
	snprintf(l5, sizeof(l5), "%.80s", c->url[0] ? c->url : "kept on this Atari");
	if (c->synced) {
		DT d;
		ct_split(tz_to_local(home_zone(), c->synced), &d);
		snprintf(l6, sizeof(l6), "Updated %d %s %d, %02d:%02d", d.day, month_short[d.mon - 1], d.year, d.hour, d.min);
	} else {
		snprintf(l6, sizeof(l6), a->kind == AK_LOCAL ? "" : "Not updated yet");
	}
	snprintf(l7, sizeof(l7), "%d events kept here%s%.40s", c->nev, a->err[0] ? "; last problem: " : "", a->err);
	put(&f[L4], FT_LABEL, FF_LTR, 0, row, 0, l4);
	put(&f[L5], FT_LABEL, FF_LTR, 0, row + 1, 46, l5);
	put(&f[L6], FT_LABEL, FF_LTR, 0, row + 2, 0, l6);
	put(&f[L7], FT_LABEL, FF_LTR, 0, row + 3, 46, l7);
	row += 5;
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, 24, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, 36, row, 10, "OK");
	r = run("Calendar", f, N, 47, row + 1, renamable ? F1 : -1);
	if (r != BOK)
		return 0;
	if ((!renamable || !name[0] || !strcmp(name, c->name)) && c->color == f[F2].val &&
	    c->alarms == f[F3].val)
		return 0;		/* OK without a change: nothing to do */
	if (renamable && name[0])
		str_copy(c->name, name, sizeof(c->name));
	c->color = f[F2].val;
	if (c->alarms != f[F3].val) {
		c->alarms = (unsigned char)f[F3].val;
		cal_load(c);		/* the alerts come from the data again */
	}
	settings_later();
	return 1;
}

/* ---------------- settings ----------------
 * The family's Settings dialog (guidelines 6.1; Erez, 9 Nov 2026), common's
 * engine (common/ui/settings.c): the list of pages at the left, the page's
 * name and a line over its settings, Cancel and OK at the bottom right;
 * the radio buttons of up to four choices (D24), the keys that reach every
 * control (D25), numbers out of their limits refused with an alert (D30).
 * The pages in 6.1's order: General, ECal's own, Secure, Advanced. */

#include "settings.h"

static const char *const week_words[] = { "Sunday", "Monday", "Saturday" };
static const char *const hour_words[] = { "6:00", "7:00", "8:00", "9:00", "10:00" };
static const char *const heb_words[] = { "Atari (standard)", "ISO-8859-8 font", "PC (code page 862)" };
static const char *const len_words[] = { "15 minutes", "30 minutes", "1 hour", "90 minutes", "2 hours" };
static const short len_min[] = { 15, 30, 60, 90, 120 };
static const char *const alert_words[] = { "None", "At the time", "5 minutes before", "10 minutes before",
					   "15 minutes before", "30 minutes before", "1 hour before" };
static const short alert_min[] = { -1, 0, 5, 10, 15, 30, 60 };
static const char *const refresh_words[] = { "Never", "Every 15 minutes", "Every hour", "Every day", "Every week" };
static const char *start_words[5] = { "At start", "After a minute", "After 5 minutes", "When opened", 0 };

enum { P_GENERAL, P_EVENTS, P_ZONES, P_REFRESH, P_CONTACTS, P_SECURE, P_ADVANCED, NPAGES };

/* the values while the dialog is open: the engine's (ticks and choices
   0..n-1, texts), taken at OK */
static struct {
	short week, hour, h24, hebdates, hebfont;
	short len, alert, alarms;
	short tzsupport;
	char hz[44], vz[44], hz_lab[32], vz_lab[32];
	short refresh, sync_open, bgsync, start;
	char past[6], future[6];
	short peoplefile;
	char people_dir[160];
	short falcon, dsp;
	char gate[64], port[6];
	short log;
	short f_past, f_future, f_port, f_people;	/* the fields' objects (check, Choose...) */
} sv;

static void zone_label(char *lab, const char *zone)
{
	snprintf(lab, 32, " %-29.29s", zone);
}

/* a time zone: the list, at once (5.7); the label shows it */
static void pick_zone(SETTINGS *s, void *user)
{
	char *zone = (char *)user;
	const char *z = zone_pick(zone, 0);
	(void)s;
	if (z)
		str_copy(zone, z, 44);
	zone_label(zone == sv.hz ? sv.hz_lab : sv.vz_lab, zone);
}

/* Contacts: Choose... picks EContact's folder with the file selector */
static void pick_people(SETTINGS *s, void *user)
{
	char path[200], name[20] = "", *e;
	short button = 0;
	LFIELD *f;
	(void)user;
	if (sv.people_dir[0])
		snprintf(path, sizeof(path), "%s\\*.*", sv.people_dir);
	else
		snprintf(path, sizeof(path), "%c:\\*.*", 'A' + boot_drive());
	if (!fsel_exinput(path, name, &button, "EContact's folder: any file") || !button)
		return;
	e = strrchr(path, '\\');
	if (e)
		*e = 0;
	str_copy(sv.people_dir, path, sizeof(sv.people_dir));
	if ((f = lf_of(set_tree(s), sv.f_people)) != 0) {
		lf_end(f);
		lf_view(f);
	}
}

static void page_general(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "Week starts on:");
	set_radio(s, x, set_y(s, 1), 9, week_words, 3, &sv.week);
	set_text(s, x, set_y(s, 3), "Day view from:");
	set_popup(s, x + SET_FX, set_y(s, 3), 8, hour_words, 5, &sv.hour);
	set_tick(s, x, set_y(s, 4), "24-hour clock", &sv.h24);
	set_tick(s, x, set_y(s, 5), "Show Hebrew dates", &sv.hebdates);
	set_text(s, x, set_y(s, 7), "Hebrew in font:");
	set_popup(s, x + 20, set_y(s, 7), 21, heb_words, 3, &sv.hebfont);
}

static void page_events(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "New events last:");
	set_popup(s, x + 20, set_y(s, 0), 21, len_words, 5, &sv.len);
	set_text(s, x, set_y(s, 1), "...with alert:");
	set_popup(s, x + 20, set_y(s, 1), 21, alert_words, 7, &sv.alert);
	set_tick(s, x, set_y(s, 2), "Show alerts", &sv.alarms);
}

static void page_zones(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "The Atari's clock is in:");
	set_value(s, x + 3, set_y(s, 1), 31, sv.hz_lab, pick_zone, sv.hz);
	set_tick(s, x, set_y(s, 3), "Time zone support", &sv.tzsupport);
	set_text(s, x, set_y(s, 4), "Show times in:");
	set_value(s, x + 3, set_y(s, 5), 31, sv.vz_lab, pick_zone, sv.vz);
}

static void page_refresh(SETTINGS *s, short x, short y, void *user)
{
	short t;
	int acc = is_acc;
	(void)y;
	(void)user;
	set_text(s, x, set_y(s, 0), "Refresh all:");
	set_popup(s, x + 20, set_y(s, 0), 21, refresh_words, 5, &sv.refresh);
	set_tick(s, x, set_y(s, 1), "When the window opens", &sv.sync_open);
	t = set_tick(s, x, set_y(s, 2), "Also with the window closed", &sv.bgsync);
	if (!acc && t >= 0)
		set_tick_enable(s, t, 0);	/* (the accessory's) */
	set_text(s, x, set_y(s, 3), "Read calendars:");
	t = set_popup(s, x + 20, set_y(s, 3), 21, start_words, start_words[4] ? 5 : 4, &sv.start);
	if (!acc && t >= 0) {
		short k;
		for (k = 0; k < 5; k++)
			set_popup_enable(s, t, k, 0);
	}
	set_text(s, x, set_y(s, 5), "Keep on this Atari:");
	set_text(s, x + 2, set_y(s, 6), "Days back");
	sv.f_past = set_field(s, x + 20, set_y(s, 6), sv.past, 5, '9');
	set_text(s, x + 2, set_y(s, 7), "Days ahead");
	sv.f_future = set_field(s, x + 20, set_y(s, 7), sv.future, 5, '9');
}

static void page_contacts(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_tick(s, x, set_y(s, 0), "When EContact isn't loaded, read", &sv.peoplefile);
	set_text(s, x + 3, set_y(s, 1), "its contacts file");
	set_text(s, x, set_y(s, 3), "EContact's folder:");
	sv.f_people = set_long(s, x, set_y(s, 4), sv.people_dir, 28, sizeof(sv.people_dir) - 1, 0);
	set_button(s, x + 31, set_y(s, 4), 10, "Choose...", pick_people, 0);
	set_text(s, x, set_y(s, 5), "CONTACTS.TXT, which EContact writes there");
}

static void page_secure(SETTINGS *s, short x, short y, void *user)
{
	int can_falcon = get_cookie(0x5F435055L) >= 20;	/* _CPU: 68020 or better, for TLS.OVL */
	(void)y;
	(void)user;
	set_tick(s, x, set_y(s, 0), "Direct connection", &sv.falcon);
	set_text(s, x + 3, set_y(s, 1), "TLS on this Atari (68020 or better)");
	set_tick(s, x + 3, set_y(s, 2), "Check certificates on the DSP", &sv.dsp);
	set_text(s, x + 3, set_y(s, 3), can_falcon ? "TLS.OVL, CACERT.PEM, ROOTS.DAT in ECal's folder"
						   : "Needs a 68020 or better (TT, Falcon)");
	set_text(s, x, set_y(s, 5), "Through the Gateway:");
	set_text(s, x + 2, set_y(s, 6), "Address");
	set_long(s, x + 11, set_y(s, 6), sv.gate, 20, sizeof(sv.gate) - 1, 0);
	set_text(s, x + 2, set_y(s, 7), "Port");
	sv.f_port = set_field(s, x + 11, set_y(s, 7), sv.port, 5, '9');
	set_text(s, x + 2, set_y(s, 8), "AERA-Gateway, on a Raspberry Pi");
}

static void page_advanced(SETTINGS *s, short x, short y, void *user)
{
	(void)y;
	(void)user;
	set_tick(s, x, set_y(s, 0), "Write a log", &sv.log);
	set_text(s, x + 3, set_y(s, 1), "ECAL.LOG: the conversation with the");
	set_text(s, x + 3, set_y(s, 2), "servers, passwords left out");
}

/* OK: a number out of its limits is refused, the alert saying what to
   type, the field kept as typed and the cursor in it (D30) */
static int number_ok(SETTINGS *s, short page, short field, const char *v, long lo, long hi, const char *what)
{
	long n = atol(v);
	if (*v && n >= lo && n <= hi)
		return 1;
	alert(1, "[1][%s %ld to %ld.|Please type a number|from %ld to %ld.][ OK ]", what, lo, hi, lo, hi);
	set_show(s, page);
	set_focus(s, field);
	return 0;
}

static int settings_check(SETTINGS *s, void *user)
{
	(void)user;
	return number_ok(s, P_REFRESH, sv.f_past, sv.past, 0, 3650, "Keep days back:") &&
	       number_ok(s, P_REFRESH, sv.f_future, sv.future, 7, 3650, "Keep days ahead:") &&
	       number_ok(s, P_SECURE, sv.f_port, sv.port, 1, 65535, "The Gateway's port:");
}

/* the window under the dialog drawn again (a pop-up or a file selector
   over it, TOS) */
static void settings_under(short x, short y, short w, short h)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	font_sys_pop();
	win_redraw(&r);
	font_sys_push();
}

static void settings(short first)
{
	static const SET_PAGE pages[NPAGES] = {
		{ "General", page_general }, { "Events", page_events }, { "Time zones", page_zones },
		{ "Refresh", page_refresh }, { "Contacts", page_contacts }, { "Secure", page_secure },
		{ "Advanced", page_advanced }
	};
	static char other[30];
	SET_DEF d;
	long st;
	short i;

	memset(&sv, 0, sizeof(sv));
	sv.week = opt.weekstart == 1 ? 1 : opt.weekstart == 6 ? 2 : 0;
	sv.hour = opt.daystart >= 6 && opt.daystart <= 10 ? opt.daystart - 6 : 2;
	sv.h24 = opt.h24;
	sv.hebdates = opt.hebdates;
	sv.hebfont = opt.hebfont >= 0 && opt.hebfont <= 2 ? opt.hebfont : 0;
	sv.len = 2;
	for (i = 0; i < 5; i++)
		if (len_min[i] == opt.duration)
			sv.len = i;
	for (i = 0; i < 7; i++)
		if (alert_min[i] == opt.alarm_default)
			sv.alert = i;
	sv.alarms = opt.alarms;
	sv.tzsupport = opt.tzsupport;
	str_copy(sv.hz, opt.home_tz, sizeof(sv.hz));
	str_copy(sv.vz, opt.view_tz, sizeof(sv.vz));
	zone_label(sv.hz_lab, sv.hz);
	zone_label(sv.vz_lab, sv.vz);
	for (i = 0; i < 5; i++)
		if (refresh_min[i] == opt.refresh)
			sv.refresh = i;
	sv.sync_open = opt.sync_open;
	sv.bgsync = opt.bgsync;
	st = bg_parse(opt.start);
	sv.start = st == BG_NOW ? 0 : st == BG_OPENED ? 3 : st == 300 ? 2 : 1;
	start_words[4] = 0;
	if (st > 0 && st != 60 && st != 300) {
		snprintf(other, sizeof(other), "After %ld s", st);
		start_words[4] = other;
		sv.start = 4;
	}
	snprintf(sv.past, sizeof(sv.past), "%d", opt.past_days);
	snprintf(sv.future, sizeof(sv.future), "%d", opt.future_days);
	sv.peoplefile = opt.peoplefile;
	str_copy(sv.people_dir, opt.peopledir, sizeof(sv.people_dir));
	sv.falcon = opt.falcon;
	sv.dsp = opt.dsp;
	str_copy(sv.gate, opt.gate, sizeof(sv.gate));
	snprintf(sv.port, sizeof(sv.port), "%u", opt.gateport);
	sv.log = opt.log;

	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.pages = pages;
	d.npages = NPAGES;
	d.height = 17;
	d.first = first;
	d.check = settings_check;
	settings_uncover = settings_under;
	if (!settings_dialog(&d))
		return;

	opt.weekstart = sv.week == 1 ? 1 : sv.week == 2 ? 6 : 0;
	opt.daystart = sv.hour + 6;
	opt.h24 = sv.h24;
	opt.hebdates = sv.hebdates;
	opt.hebfont = sv.hebfont;
	opt.duration = len_min[sv.len];
	opt.alarm_default = alert_min[sv.alert];
	opt.alarms = sv.alarms;
	opt.tzsupport = sv.tzsupport;
	opt.refresh = refresh_min[sv.refresh];
	opt.sync_open = sv.sync_open;
	opt.bgsync = sv.bgsync;
	if (sv.start < 4)
		str_copy(opt.start, start_val[sv.start], sizeof(opt.start));
	opt.past_days = (short)atoi(sv.past);
	opt.future_days = (short)atoi(sv.future);
	str_copy(opt.home_tz, sv.hz, sizeof(opt.home_tz));
	if (strcmp(sv.vz, opt.view_tz)) {
		str_copy(opt.view_tz, sv.vz, sizeof(opt.view_tz));
		opt.tzsupport = 1;	/* a zone to show times in: support on */
	}
	if (!opt.tzsupport)
		str_copy(opt.view_tz, opt.home_tz, sizeof(opt.view_tz));
	str_copy(opt.gate, sv.gate, sizeof(opt.gate));
	opt.gateport = (unsigned short)atol(sv.port);
	opt.dsp = sv.dsp;
	opt.log = sv.log;
	opt.peoplefile = sv.peoplefile;
	str_trim(sv.people_dir);
	{
		long l = (long)strlen(sv.people_dir);
		if (l > 3 && sv.people_dir[l - 1] == '\\')
			sv.people_dir[l - 1] = 0;	/* C:\ECONTACT\ as C:\ECONTACT */
	}
	str_copy(opt.peopledir, sv.people_dir, sizeof(opt.peopledir));
	people_setup();
	if (sv.falcon != opt.falcon)
		falcon_mode_set(sv.falcon);
	falcon_dsp_init();
	falcon_dsp_ec_init();
	http_close_all();
	store_save_settings();
	set_logging();
	after_change();
}

void dlg_settings(void)
{
	settings(P_GENERAL);
}

/* Settings at its Secure page */
void dlg_connection(void)
{
	settings(P_SECURE);
}

void dlg_goto(void)
{
	static char date[16];
	DT d;
	int y, m, dd, n = 0, v[3] = { 0, 0, 0 };
	const char *p;
	ct_split(cur_day * MIN_PER_DAY, &d);
	snprintf(date, sizeof(date), "%02d.%02d.%04d", d.day, d.mon, d.year);
	if (!dlg_ask("Go to date", "Date (day.month.year):", date, 12))
		return;
	for (p = date; *p && n < 3; ) {
		while (*p && (*p < '0' || *p > '9'))
			p++;
		if (!*p)
			break;
		while (*p >= '0' && *p <= '9')
			v[n] = v[n] * 10 + (*p++ - '0');
		n++;
	}
	dd = v[0];
	m = n >= 2 ? v[1] : d.mon;
	y = n >= 3 ? v[2] : d.year;
	if (y < 100)
		y += 2000;
	/* a year far off would overflow the minutes ECal counts in (typed
	   onto the date already there: 22.10.202630) */
	if (y < 1900 || y > 2199 || m < 1 || m > 12 || dd < 1 || dd > days_in_month(y, m)) {
		alert(1, "[1][That isn't a date.][ OK ]");
		return;
	}
	views_goto(days_from_civil(y, m, dd), cur_view == V_YEAR ? V_MONTH : cur_view);
}

void find_dialog(void)
{
	static char what[60];
	char (*lab)[64];
	const char **items;
	OCCLIST l;
	CTIME now = now_view();
	int i, k;
	if (!dlg_ask("Find events", "Find:", what, sizeof(what) - 1) || !what[0])
		return;
	busy(1);
	memset(&l, 0, sizeof(l));
	/* from the first day kept on this Atari to two years ahead */
	occ_search(&l, what, ct_day_start(now) - (CTIME)opt.past_days * MIN_PER_DAY, now + 731L * MIN_PER_DAY);
	busy(0);
	if (!l.n) {
		occ_free(&l);
		alert(1, "[1][Nothing found for \"%s\".][ OK ]", what);
		return;
	}
	k = l.n < 200 ? l.n : 200;
	lab = malloc(k * (sizeof(*lab) + sizeof(*items)));
	if (!lab) {
		occ_free(&l);
		no_memory();
		return;
	}
	items = (const char **)(lab + k);
	for (i = 0; i < l.n && i < 200; i++) {
		DT d;
		char t[12] = "";
		ct_split(l.o[i].start, &d);
		if (!l.o[i].ev->allday)
			fmt_time(d.hour, d.min, opt.h24, t, sizeof(t));
		snprintf(lab[i], sizeof(lab[i]), "%02d.%02d.%04d %-7s %.40s", d.day, d.mon, d.year, t,
			 l.o[i].ev->summary ? l.o[i].ev->summary : "");
		items[i] = lab[i];
	}
	/* start at the first one from today on */
	for (k = 0; k < i && l.o[k].start < ct_day_start(now); k++)
		;
	k = dlg_pick(what, items, (short)i, (short)(k < i ? k : 0));
	if (k >= 0) {
		views_goto(ct_days(l.o[k].start), cur_view == V_YEAR ? V_MONTH : cur_view);
		sel_occ = l.o[k];
		occ_find_again();
		win_redraw(0);
	}
	free(lab);
	occ_free(&l);
}

/* the keys, as a table: headings, the keys in one column and what they
   do in the next, two to a row where they're short */
/* Help: the user guide, ECAL.HYP, in EHypView or ST-Guide (common/ui/help.c,
   which also keeps the name where the viewer may read it); without a
   viewer, an alert says how to get one and offers the keys dialog */
void help_open(void)
{
	if (help_key(opt.workdir, "ecal.hyp", 0, "Keys"))
		keys_dialog();
}

void keys_dialog(void)
{
	static const struct {
		short row, col;		/* col 0: left pair, 1: right pair, -1: heading */
		const char *key, *does;
	} k[] = {
		{ 1, -1, "View", 0 },
		{ 2, 0, "T  Clr/Home", "today" },
		{ 2, 1, "D W M Y", "day, week, month, year" },
		{ 3, 0, "\x04 \x03", "a day back, ahead" },
		{ 3, 1, "Shift+\x04 \x03", "a whole period" },
		{ 4, 0, "\x01 \x02", "a week (month view)" },
		{ 4, 1, "\x01 \x02", "the hours (day, week)" },
		{ 5, -1, "Events", 0 },
		{ 6, 0, "N  ^N", "a new event" },
		{ 6, 1, "Return", "open the selected one" },
		{ 7, 0, "Tab", "the next event" },
		{ 7, 1, "Delete", "delete the selected one" },
		{ 8, 0, "Shift+Tab", "the one before" },
		{ 8, 1, "Esc", "nothing selected" },
		{ 9, -1, "Commands", 0 },
		{ 10, 0, "^F", "find" },
		{ 10, 1, "^G", "go to a date" },
		{ 11, 0, "^R", "refresh" },
		{ 11, 1, "Shift+^R", "everything again" },
		{ 12, 0, "\x01^S", "the calendar list" },
		{ 12, 1, "F10", "Hebrew keyboard" },
		{ 13, 0, "^U  ^Q", "close the window" },
		{ 13, 1, "Help", "the user guide" },
		{ 14, 0, "Double-click", "open; on a day: new" },
		{ 14, 1, "Right-click", "more choices" },
		{ 15, 0, "^C", "copy the event" },
		{ 15, 1, "^C ^X ^V", "in a dialog's field" },
		{ 16, 0, "^,", "settings" },
		{ 16, 1, "Insert", "the right-click menu" },
		{ 17, 0, "Esc", "a field: empty it" },
		{ 17, 1, "Undo", "cancel a dialog" },
	};
	enum { N = sizeof(k) / sizeof(k[0]) };
	FIELD f[2 * N + 1];
	short i, n = 0;
	for (i = 0; i < N; i++) {
		/* left pair: keys from column 1, words from 14; right pair:
		   keys from 37, words from 49 */
		short kx = k[i].col == 1 ? 37 : 1, dx = k[i].col == 1 ? 49 : 14;
		if (k[i].col < 0) {
			put(&f[n++], FT_HEAD, 0, 0, k[i].row, 0, k[i].key);
			continue;
		}
		put(&f[n++], FT_LABEL, FF_LTR, kx, k[i].row, 0, k[i].key);
		put(&f[n++], FT_LABEL, FF_LTR, dx, k[i].row, 0, k[i].does);
	}
	put(&f[n++], FT_BUTTON, FF_DEFAULT | FF_CANCEL, 61, 19, 10, "OK");	/* at W - 12 (CONTROLS) */
	run("Keys", f, n, 73, 20, -1);
}
