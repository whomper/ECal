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
	alert(1, "[1][Memory is full: ECal can't do|that now. Give ECal more memory|(HEAPKB= in ECAL.INF) or keep|fewer days (Settings).][ OK ]");
}

const char *zone_pick(const char *current)
{
	char (*lab)[48];
	static const char *items[100];
	int n = tz_count(), i, sel = 0;
	CTIME now = now_utc();
	if (n > 100)
		n = 100;
	lab = malloc(n * sizeof(*lab));
	if (!lab) {
		no_memory();
		return 0;
	}
	for (i = 0; i < n; i++) {
		const TZONE *z = tz_index(i);
		char off[16];
		tz_label(z, now, off, sizeof(off));
		snprintf(lab[i], sizeof(lab[i]), "%-30s %s", z->name, off);
		items[i] = lab[i];
		if (current && !strcmp(z->name, current))
			sel = i;
	}
	i = dlg_pick("Time zone", items, (short)n, (short)sel);
	free(lab);
	return i >= 0 ? tz_index(i)->name : 0;
}

/* ---------------- accounts ---------------- */

static int connection_ready(const char *url)
{
	URL u;
	if (!url_parse(url, &u) || !u.tls || opt.falcon || opt.gate[0])
		return 1;
	if (alert(1, "[1][Calendar servers want a secure connection. Set up the Raspberry Pi gateway, or Falcon mode, in Settings, Connection.][Settings|Cancel]") == 1)
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
	static char name[40], url[200], user[80], pass[80];
	enum { L1, F1, L2, F2, L3, F3, L4, F4, N1, N2, N3, BDEL, BCAN, BOK, N };
	FIELD f[N];
	short r, row = 1;
	str_copy(name, a->name, sizeof(name));
	str_copy(url, a->url, sizeof(url));
	str_copy(user, a->user, sizeof(user));
	str_copy(pass, a->pass, sizeof(pass));
	put(&f[L1], FT_LABEL, 0, 0, row, 0, "Name:");
	text(&f[F1], 0, 13, row, 30, name, 31);
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

/* when the accessory reads the calendars (START=, common/ui/bgwork.h);
   the last item is another number of seconds typed into ECAL.INF */
static const char *start_items[5] = {
	"  At start", "  After a minute", "  After 5 minutes", "  When opened", 0
};
static const char *const start_val[4] = { "now", "60", "300", "opened" };

static int edit_subscription(ACCOUNT *a, CALENDAR *c, int is_new)
{
	static char url[300], name[60], user[80], pass[80];
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
	text(&f[F2], 0, 12, row, 30, name, 40);
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
		char name[40] = "Atari";
		int i, a = -1;
		if (!dlg_ask("New calendar on this Atari", "Name:", name, 30) || !name[0])
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
	text(&f[F1], renamable ? 0 : FF_DISABLED, 10, row, 34, name, 60);
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

/* ---------------- settings ---------------- */

static const char *const week_items[] = { "  Sunday", "  Monday", "  Saturday" };
static const char *const len_items[] = { "  15 minutes", "  30 minutes", "  1 hour", "  90 minutes", "  2 hours" };
static const short len_min[] = { 15, 30, 60, 90, 120 };
static const char *const alert_items[] = { "  None", "  At the time", "  5 minutes before", "  10 minutes before",
					   "  15 minutes before", "  30 minutes before", "  1 hour before" };
static const short alert_min[] = { -1, 0, 5, 10, 15, 30, 60 };
static const char *const hour_items[] = { "  6:00", "  7:00", "  8:00", "  9:00", "  10:00" };
static const char *const heb_items[] = { "  Atari (standard)", "  ISO-8859-8 font", "  PC (code page 862)" };

/* ---------------- settings ----------------
 * As in EMail: a list of sections on the left (the chosen one in a black
 * bar) and that section's settings on the right, under its name. */

enum { S_GENERAL = 1, S_EVENTS, S_ZONES, S_REFRESH, S_CONN };
static const char *const section_names[] = { "General", "Events", "Time zones", "Refresh", "Connection" };

#define SIDE 12			/* the list's width */
#define CX (SIDE + 2)		/* where a section's settings start */
#define VX (CX + 17)		/* and their values */
#define SW (VX + 31)		/* the dialog's width */

static void in(FIELD *f, short page)
{
	f->page = page;
}

static void settings(short first)
{
	enum { L1, F1, L2, F2, F3, F4, L13, F13, F14,
	       L5, F5, L6, F6, F7,
	       L8, F8, F9, L10, F10,
	       L11, F11, F12, F17, L18, F18, H3, L15, F15, L16, F16,
	       C1, C2, N1, L3, C3, L4, C4, N2, C7,
	       BCAN, BOK, N };
	FIELD f[N];
	DLG d;
	static char hz[44], vz[44], gate[64], port[8], past[8], future[8];
	static const char *hz_items[2], *vz_items[2];
	static char hz_lab[50], vz_lab[50];
	short r, i, row;
	int can_falcon = tls_available && get_cookie(0x5F435055L) >= 20;	/* _CPU: 68020 or better */
	str_copy(hz, opt.home_tz, sizeof(hz));
	str_copy(vz, opt.view_tz, sizeof(vz));
	str_copy(gate, opt.gate, sizeof(gate));
	snprintf(port, sizeof(port), "%u", opt.gateport);
	snprintf(past, sizeof(past), "%d", opt.past_days);
	snprintf(future, sizeof(future), "%d", opt.future_days);

	/* General */
	row = 3;
	put(&f[L1], FT_LABEL, 0, CX, row, 0, "Week starts on:");
	put(&f[F1], FT_POPUP, 0, VX, row, 14, 0);
	f[F1].items = week_items;
	f[F1].nitems = 3;
	f[F1].val = opt.weekstart == 1 ? 1 : opt.weekstart == 6 ? 2 : 0;
	row++;
	put(&f[L2], FT_LABEL, 0, CX, row, 0, "Day view from:");
	put(&f[F2], FT_POPUP, 0, VX, row, 10, 0);
	f[F2].items = hour_items;
	f[F2].nitems = 5;
	f[F2].val = opt.daystart >= 6 && opt.daystart <= 10 ? opt.daystart - 6 : 2;
	row++;
	put(&f[F3], FT_CHECK, 0, VX, row, 24, "24-hour clock");
	f[F3].val = opt.h24;
	row++;
	put(&f[F4], FT_CHECK, 0, VX, row, 24, "Show Hebrew dates");
	f[F4].val = opt.hebdates;
	row++;
	put(&f[L13], FT_LABEL, 0, CX, row, 0, "Hebrew in font:");
	put(&f[F13], FT_POPUP, 0, VX, row, 24, 0);
	f[F13].items = heb_items;
	f[F13].nitems = 3;
	f[F13].val = opt.hebfont;
	row++;
	put(&f[F14], FT_CHECK, 0, VX, row, 32, "Start with the Hebrew keyboard");
	f[F14].val = opt.hebrew;
	for (i = L1; i <= F14; i++)
		in(&f[i], S_GENERAL);

	/* Events */
	row = 3;
	put(&f[L5], FT_LABEL, 0, CX, row, 0, "New events last:");
	put(&f[F5], FT_POPUP, 0, VX, row, 16, 0);
	f[F5].items = len_items;
	f[F5].nitems = 5;
	f[F5].val = 2;
	for (i = 0; i < 5; i++)
		if (len_min[i] == opt.duration)
			f[F5].val = i;
	row++;
	put(&f[L6], FT_LABEL, 0, CX, row, 0, "...with alert:");
	put(&f[F6], FT_POPUP, 0, VX, row, 22, 0);
	f[F6].items = alert_items;
	f[F6].nitems = 7;
	for (i = 0; i < 7; i++)
		if (alert_min[i] == opt.alarm_default)
			f[F6].val = i;
	row++;
	put(&f[F7], FT_CHECK, 0, VX, row, 24, "Show alerts");
	f[F7].val = opt.alarms;
	for (i = L5; i <= F7; i++)
		in(&f[i], S_EVENTS);

	/* Time zones */
	row = 3;
	put(&f[L8], FT_LABEL, 0, CX, row, 0, "Atari clock in:");
	put(&f[F8], FT_POPUP, 0, VX, row, 30, 0);
	snprintf(hz_lab, sizeof(hz_lab), "  %s", hz);
	hz_items[0] = hz_lab;
	hz_items[1] = "  Other...";
	f[F8].items = hz_items;
	f[F8].nitems = 2;
	row++;
	put(&f[F9], FT_CHECK, 0, VX, row, 30, "Time zone support");
	f[F9].val = opt.tzsupport;
	row++;
	put(&f[L10], FT_LABEL, 0, CX, row, 0, "Show times in:");
	put(&f[F10], FT_POPUP, 0, VX, row, 30, 0);
	snprintf(vz_lab, sizeof(vz_lab), "  %s", vz);
	vz_items[0] = vz_lab;
	vz_items[1] = "  Other...";
	f[F10].items = vz_items;
	f[F10].nitems = 2;
	for (i = L8; i <= F10; i++)
		in(&f[i], S_ZONES);

	/* Refresh */
	row = 3;
	put(&f[L11], FT_LABEL, 0, CX, row, 0, "Refresh all:");
	put(&f[F11], FT_POPUP, 0, VX, row, 22, 0);
	f[F11].items = refresh_items;
	f[F11].nitems = 5;
	for (i = 0; i < 5; i++)
		if (refresh_min[i] == opt.refresh)
			f[F11].val = i;
	row++;
	put(&f[F12], FT_CHECK, 0, VX, row, 30, "When the window opens");
	f[F12].val = opt.sync_open;
	row++;
	put(&f[F17], FT_CHECK, is_acc ? 0 : FF_DISABLED, VX, row, 30, "Also with the window closed");
	f[F17].val = opt.bgsync;
	row++;
	put(&f[L18], FT_LABEL, is_acc ? 0 : FF_DISABLED, CX, row, 0, "Read calendars:");
	put(&f[F18], FT_POPUP, is_acc ? 0 : FF_DISABLED, VX, row, 22, 0);
	{
		static char other[30];
		long st = bg_parse(opt.start);
		f[F18].items = start_items;
		f[F18].nitems = 4;
		f[F18].val = st == BG_NOW ? 0 : st == BG_OPENED ? 3 : st == 300 ? 2 : 1;
		if (st > 0 && st != 60 && st != 300) {
			snprintf(other, sizeof(other), "  After %ld s", st);
			start_items[4] = other;
			f[F18].nitems = 5;
			f[F18].val = 4;
		}
	}
	row += 2;
	put(&f[H3], FT_LABEL, 0, CX, row, 0, "Keep on this Atari:");
	row++;
	put(&f[L15], FT_LABEL, 0, CX + 2, row, 0, "Days back");
	text(&f[F15], FF_LTR | FF_DIGITS, VX, row, 5, past, 5);
	row++;
	put(&f[L16], FT_LABEL, 0, CX + 2, row, 0, "Days ahead");
	text(&f[F16], FF_LTR | FF_DIGITS, VX, row, 5, future, 5);
	for (i = L11; i <= F16; i++)
		in(&f[i], S_REFRESH);

	/* Connection */
	row = 3;
	put(&f[C1], FT_CHECK, 0, CX, row, 44, "Falcon mode: TLS on this Atari");
	f[C1].val = opt.falcon;
	row++;
	put(&f[C2], FT_CHECK, 0, CX + 3, row, 40, "Check certificates on the DSP");
	f[C2].val = opt.dsp;
	row++;
	put(&f[N1], FT_LABEL, FF_LTR, CX + 3, row, 0, can_falcon ? "Needs CACERT.PEM, ROOTS.DAT in ECAL"
							  : !tls_available ? "Not in the ST edition: ECAL.ACC has it"
							  : "Needs a 68030 (Falcon or TT)");
	row += 2;
	put(&f[L3], FT_LABEL, 0, CX, row, 0, "Pi gateway:");
	text(&f[C3], FF_LTR, CX + 12, row, 20, gate, 60);
	put(&f[L4], FT_LABEL, 0, CX + 34, row, 0, "Port:");
	text(&f[C4], FF_LTR | FF_DIGITS, CX + 40, row, 6, port, 6);
	row++;
	put(&f[N2], FT_LABEL, FF_LTR, CX + 12, row, 0, "gateway/install.sh on the Pi");
	row++;
	put(&f[C7], FT_CHECK, 0, CX, row, 40, "Write a log (ECAL.LOG)");
	f[C7].val = opt.log;
	for (i = C1; i <= C7; i++)
		in(&f[i], S_CONN);

	/* on every section */
	row = 11;
	put(&f[BCAN], FT_BUTTON, FF_CANCEL, SW - 24, row, 10, "Cancel");
	put(&f[BOK], FT_BUTTON, FF_DEFAULT, SW - 12, row, 10, "OK");

	memset(&d, 0, sizeof(d));
	d.title = "Settings";
	d.f = f;
	d.n = N;
	d.w = SW;
	d.h = row + 1;
	d.focus = -1;
	d.pages = section_names;
	d.npages = 5;
	d.side = SIDE;
	d.page = first;
	r = dlg_run(&d);
	if (r != BOK)
		return;

	opt.weekstart = f[F1].val == 1 ? 1 : f[F1].val == 2 ? 6 : 0;
	opt.daystart = f[F2].val + 6;
	opt.h24 = f[F3].val;
	opt.hebdates = f[F4].val;
	opt.hebfont = f[F13].val;
	opt.hebrew = f[F14].val;
	set_hebrew_kbd(opt.hebrew);
	opt.duration = len_min[f[F5].val];
	opt.alarm_default = alert_min[f[F6].val];
	opt.alarms = f[F7].val;
	opt.tzsupport = f[F9].val;
	opt.refresh = refresh_min[f[F11].val];
	opt.sync_open = f[F12].val;
	opt.bgsync = f[F17].val;
	if (f[F18].val < 4)
		str_copy(opt.start, start_val[f[F18].val], sizeof(opt.start));
	/* 0 days back: from today; at most ten years either way */
	opt.past_days = (short)(atoi(past) < 0 ? 0 : atoi(past) > 3650 ? 3650 : atoi(past));
	opt.future_days = (short)(atoi(future) < 7 ? 400 : atoi(future) > 3650 ? 3650 : atoi(future));
	if (f[F8].val == 1) {
		const char *z = zone_pick(opt.home_tz);
		if (z)
			str_copy(opt.home_tz, z, sizeof(opt.home_tz));
	}
	if (f[F10].val == 1) {
		const char *z = zone_pick(opt.view_tz);
		if (z) {
			str_copy(opt.view_tz, z, sizeof(opt.view_tz));
			opt.tzsupport = 1;
		}
	}
	if (!opt.tzsupport)
		str_copy(opt.view_tz, opt.home_tz, sizeof(opt.view_tz));
	str_copy(opt.gate, gate, sizeof(opt.gate));
	opt.gateport = (unsigned short)(atoi(port) > 0 ? atoi(port) : 8180);
	opt.dsp = f[C2].val;
	opt.log = f[C7].val;
	if (f[C1].val != opt.falcon)
		falcon_mode_set(f[C1].val);
	falcon_dsp_init();
	http_close_all();
	store_save_settings();
	set_logging();
	after_change();
}

void dlg_settings(void)
{
	settings(S_GENERAL);
}

/* Settings at its Connection section */
void dlg_connection(void)
{
	settings(S_CONN);
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
	if (m < 1 || m > 12 || dd < 1 || dd > days_in_month(y, m)) {
		alert(1, "[1][That isn't a date.][ OK ]");
		return;
	}
	views_goto(days_from_civil(y, m, dd), cur_view == V_YEAR ? V_MONTH : cur_view);
}

void dlg_find(void)
{
	static char what[60];
	char (*lab)[64];
	static const char *items[200];
	OCCLIST l;
	CTIME now = now_view();
	int i, k;
	if (!dlg_ask("Find events", "Find:", what, 40) || !what[0])
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
	lab = malloc((l.n < 200 ? l.n : 200) * sizeof(*lab));
	if (!lab) {
		occ_free(&l);
		no_memory();
		return;
	}
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
	if (help_key(opt.workdir, "ECAL.HYP", 0, "Keys"))
		dlg_keys();
}

void dlg_keys(void)
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
		{ 13, 0, "^W  ^Q", "close the window" },
		{ 13, 1, "Help", "the user guide" },
		{ 14, 0, "Double-click", "open; on a day: new" },
		{ 14, 1, "Right-click", "more choices" },
		{ 15, 0, "^C", "copy the event" },
		{ 15, 1, "^C ^X ^V", "in a dialog's field" },
		{ 16, 0, "^,", "settings" },
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
	put(&f[n++], FT_BUTTON, FF_DEFAULT | FF_CANCEL, 62, 16, 10, "OK");
	run("Keys", f, n, 73, 17, -1);
}
