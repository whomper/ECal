/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * main.c - Calendar: start-up as a desk accessory or as a program, the
 * event loop, reminders, refreshing, and the commands behind the
 * toolbar's menu.
 *
 * As an accessory (ECAL.ACC in the boot drive's root) it waits in
 * the Desk menu: choosing "ECal" opens the window over whatever
 * program is running. Reminders pop up even while the window is closed.
 * Its files live in X:\ECAL\ on the boot drive. As a program
 * (ECAL.PRG) it keeps them next to itself and ends when the window
 * closes.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "conn.h"
#include "secret.h"
#include "busy.h"
#include "tls.h"
#include "people_ask.h"
#include "../src/http.h"
#include "../src/plat.h"
#include "util.h"
#include "bidi.h"
#include "../src/version.h"
#include "crash.h"
#include "av.h"
#include "dd.h"
#include "aboutbox.h"
#include "mbar.h"
#include "twin.h"
#include "bgwork.h"
#include "../src/alarms.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id, is_acc, ncolors, hebrew_kbd, quit_now;
extern short _is_acc;		/* crt0.S */

static short menu_id = -1;
static int data_ready;
static int settings_ready;	/* ECAL.INF read: at the first opening, or in the background */
static int fonts_ready;		/* the window's font set up (at the first opening only:
				   GDOS fonts loaded at boot would be the program in front's) */
static char workdir[160];	/* ECal's folder: calendars, keys, certificates, log */
static char inf_file[40];	/* ECAL.INF, in the root of the boot drive */
static unsigned long last_refresh, last_minute;
/* the accessory's background refresh (work_step) */
static int bg_next = -1;		/* the next calendar to refresh, -1 none */
static int bg_first = 1;		/* the first refresh after START= still to come */
static int bg_changed;			/* this round: 1 events changed, 2 ECAL.INF to write */
static CTIME alarm_checked;	/* the clock's time the reminders were last looked at */
static long alarm_day;
static long shown_today = -1;

/* ---------------- small things ---------------- */

/* settings changed by a click (a calendar shown or hidden, the window
   moved) are written a few seconds later, when nothing else happens: the
   click shows at once instead of waiting for the disk. Closing the window
   or quitting writes them at once */
static int settings_dirty;
static unsigned long settings_at;

void settings_later(void)
{
	settings_dirty = 1;
	settings_at = pf_ms();
}

static void save_alarms(void);

/* the accessory, its window closed, writes ECAL.INF in the background:
   1 and 2 its text, 3 the file */
static int inf_stage;
static SBUF inf_text;

void settings_flush(void)
{
	if (settings_dirty || inf_stage) {
		settings_dirty = 0;
		inf_stage = 0;
		sb_free(&inf_text);
		store_save_settings();
		save_alarms();		/* ALARMS.IDX's stamp: ECAL.INF changed (in steps) */
	}
}

/* Closing the window (quitting: also the program): each step on the
   info line, so a slow one (a secure connection to say goodbye to, a
   floppy to write to) doesn't look like a hang */
void closing_down(int quitting)
{
	char host[100], m[160];
	busy(1);
	if (settings_dirty) {
		status("Closing: saving your settings...");
		settings_flush();
	}
	if (quitting) {
		http_kept_host(host, sizeof(host));
		if (host[0]) {
			snprintf(m, sizeof(m), "Closing: ending the connection to %s...", host);
			status(m);
		}
		http_close_all();
	}
	if (opt.log) {
		status("Closing: writing ECAL.LOG...");
		conn_log_flush();
	}
	status(quitting ? "Closing ECal..." : "Closing the window...");
	busy(0);
}

/* the info line; while the Hebrew keyboard is on it says so at the end,
   whatever else it shows (EMail's editor line does the same) */
void status(const char *msg)
{
	char s[200];
	crash_note(msg);		/* for CRASH.LOG, should ECal crash */
	if (win_h <= 0)
		return;
	if (hebrew_kbd) {
		snprintf(s, sizeof(s), "%s%sHebrew keyboard (F10)", msg, *msg ? "   " : "");
		msg = s;
	}
	win_status(msg);
}

/* the bee; while it shows, Esc or Undo stops the network (busy.h) */
void busy(int on)
{
	busy_bee(on);
}

/* a password that can't be encrypted goes into ECAL.INF as it is, and the
   user is told, once (the AERA guidelines, 7.2) */
static void warn_plain(const char *keyfile)
{
	alert(1, "[1][ECal can't write its key file, %s, so the passwords in ECAL.INF are kept unencrypted. "
		 "Is the disk locked or full?][ OK ]", keyfile);
}

void set_logging(void)
{
	conn_log_flush();
	if (opt.log)
		path_join(conn_logfile, sizeof(conn_logfile), workdir, "ECAL.LOG");
	else
		conn_logfile[0] = 0;
}

/* Israeli SI-1452 layout by key position (scancode 0x10-0x35), giving
   Atari character codes; 0 = not remapped. From Claude ST and EMail. */
static const u8 hebrew_keys[0x36 - 0x10] = {
	/* 10 q..p */ '/', '\'', 0xD4, 0xD5, 0xC2, 0xCA, 0xC7, 0xD8, 0xDA, 0xD2,
	/* 1a [ ] ret ctrl */ 0, 0, 0, 0,
	/* 1e a..' */ 0xD6, 0xC5, 0xC4, 0xCC, 0xD1, 0xCB, 0xC9, 0xCD, 0xD9, 0xDB, ',',
	/* 29 ` lshift \ */ 0, 0, 0,
	/* 2c z../ */ 0xC8, 0xD0, 0xC3, 0xC6, 0xCF, 0xCE, 0xD3, 0xD7, 0xDC, '.'
};

unsigned char key_char(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char ascii = KEY_ASCII(key);
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_ALT)) && !key_ctrl(kstate, key) &&
	    scan >= 0x10 && scan < 0x36 && hebrew_keys[scan - 0x10])
		ascii = hebrew_keys[scan - 0x10];
	return ascii;
}

static void updated_status(void);

void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	if (data_ready)
		updated_status();
}

void after_change(void)
{
	alarms_changed();	/* the reminders built again, in the background */
	views_select_none();
	views_invalidate();
	win_redraw(0);
}

/* ---------------- Falcon mode ---------------- */

int falcon_mode_set(int on)
{
	if (on) {
		if (!tls_available) {
			alert(1, "[3][This is the ST edition: Falcon mode is in ECAL.ACC.][ OK ]");
			return 0;
		}
		if (get_cookie(0x5F435055L) < 20) {	/* _CPU */
			alert(1, "[3][Falcon mode needs a 68020 or better (Falcon, TT): this Atari is too slow for TLS.][ OK ]");
			return 0;
		}
		if (!pf_exists(conn_cacert)) {
			alert(1, "[3][Falcon mode needs the root certificates: put CACERT.PEM and ROOTS.DAT in ECal's folder.][ OK ]");
			return 0;
		}
	}
	opt.falcon = (short)on;
	http_close_all();
	falcon_dsp_init();
	falcon_dsp_ec_init();
	store_save_settings();
	return 1;
}

/* ---------------- refreshing ---------------- */

static void updated_status(void)
{
	char s[120], lab[16];
	DT d;
	CTIME now = now_utc();
	ct_split(tz_to_local(view_zone(), now), &d);
	tz_label(view_zone(), now, lab, sizeof(lab));
	if (opt.offline)
		snprintf(s, sizeof(s), "Working offline   %s %s", tz_city(view_zone()), lab);
	else
		snprintf(s, sizeof(s), "Updated %02d:%02d   %s %s", d.hour, d.min, tz_city(view_zone()), lab);
	status(s);
}

void status_updated(void)
{
	if (data_ready)
		updated_status();
	else
		status("");
}

static void refresh(int force, int quiet)
{
	int r;
	if (!naccts)
		return;
	if (opt.offline) {
		if (!quiet)
			alert(1, "[1][ECal is working offline.|Switch it off in the menu (the button at the right of the toolbar) to refresh.][ OK ]");
		return;
	}
	busy(1);
	status("Updating...");
	conn_log_hold(1);
	r = sync_all(force);
	conn_log_hold(0);
	busy(0);
	last_refresh = pf_ms();
	bg_first = 0;
	after_change();
	if (r < 0) {
		status(cal_err);
		if (!quiet)
			alert(1, "[1][%s][ OK ]", cal_err);
	} else {
		updated_status();
	}
}

void cmd_refresh(int force)
{
	refresh(force, 0);
}

/* every event fetched again, none of ECal's copy kept: for a copy gone
   wrong (Refresh everything keeps each event whose copy matches) */
static void cmd_download_again(void)
{
	if (alert(1, "[2][Download every calendar again?|ECal's copy is replaced by the|servers'. Calendars on this|Atari stay as they are.][Download|Cancel]") == 1)
		refresh(SYNC_AGAIN, 0);
}

/* subscriptions with their own interval, and "refresh all every..." */
static void auto_refresh(void)
{
	unsigned long now = pf_ms();
	int i, due = 0;
	if (!naccts || opt.offline)
		return;
	if (opt.refresh > 0 && now - last_refresh > (unsigned long)opt.refresh * 60000UL)
		due = 1;
	for (i = 0; i < ncals && !due; i++) {
		ACCOUNT *a = &accts[cals[i].acct];
		if (a->refresh > 0 && cals[i].synced &&
		    now_utc() - cals[i].synced >= a->refresh && now - last_refresh > 60000UL) {
			busy(1);
			status("Updating...");
			sync_calendar(&cals[i], 0);
			busy(0);
			store_save_settings();
			after_change();
			updated_status();
			last_refresh = now;
			return;
		}
	}
	if (due)
		refresh(0, 1);
}

/* ---------------- reminders ---------------- */

static void open_window(void);

static void show_alarm(const ALARM *a, void *ud)
{
	char t[16], line[100];
	DT d;
	long days = ct_days(a->start) - ct_days(now_clock());
	short b;
	(void)ud;
	ct_split(a->start, &d);
	fmt_time(d.hour, d.min, opt.h24, t, sizeof(t));
	if (a->allday)
		snprintf(line, sizeof(line), "%s", days == 0 ? "today" : days == 1 ? "tomorrow" : "all day");
	else if (days == 0)
		snprintf(line, sizeof(line), "today at %s", t);
	else if (days == 1)
		snprintf(line, sizeof(line), "tomorrow at %s", t);
	else
		snprintf(line, sizeof(line), "%s %d %s, %s", wday_short[d.wday], d.day, month_short[d.mon - 1], t);
	Bconout(2, 7);
	/* OK last: Esc only dismisses it (guideline 3.5) */
	b = alert(2, "[1][%s|%s%s%s][Show| OK ]", a->title, line, a->place[0] ? "|" : "", a->place);
	if (b == 1) {
		if (win_h <= 0)
			open_window();
		if (win_h > 0)
			views_goto(ct_days(a->start), V_DAY);
	}
}

/* the reminders whose time came since the last look: a clock
   comparison with the queue (src/alarms.c), built in the background */
static void check_alarms(void)
{
	CTIME now = now_clock(), from;
	if (!opt.alarms)
		return;
	if (alarm_checked == 0 || now - alarm_checked > 2 * MIN_PER_DAY || now < alarm_checked) {
		alarm_checked = now;		/* first look: nothing from the past */
		return;
	}
	if (now == alarm_checked)
		return;
	from = alarm_checked;
	alarm_checked = now;		/* before the alerts: they wait for the user */
	alarms_due(from, now, show_alarm, 0);
}

/* ALARMS.IDX next to the caches: the accessory's reminders at boot.
   Written in steps: each file's stamp is a directory search */
static const char *idx_files[MAXCAL + 1];
static int idx_n, idx_i = -1;		/* idx_i: the next file to stamp; -1 none */
static unsigned long idx_stamp;
/* what ALARMS.IDX on the disk says (guidelines 6.5: not written again
   when nothing in it would change): its queue and settings, and our
   writes to the files it was made from (store_gen) by then */
static int idx_known;
static unsigned long idx_q, idx_gen, idx_q_new, idx_gen_new;

static unsigned long idx_contents(void)
{
	return alarms_hash() ^ ((unsigned long)opt.alarms << 1) ^ ((unsigned long)opt.h24 << 2);
}

static void save_alarms(void)
{
	if (!workdir[0] || !settings_ready || !alarm_built)
		return;
	idx_q_new = idx_contents();
	idx_gen_new = store_gen;
	if (idx_known && idx_q_new == idx_q && idx_gen_new == idx_gen)
		return;		/* as it is on the disk */
	idx_n = store_files(idx_files, MAXCAL + 1);
	idx_i = 0;
	idx_stamp = ALARM_STAMP0;
}

static int save_alarms_pending(void)
{
	return idx_i >= 0;
}

/* ALARMS.IDX waits for a refresh's end, and for ECAL.INF it leads to (the
   ctags): its stamp has them all, so written before them it would be
   written again after (guidelines 6.5) */
static int idx_wait(void)
{
	return sync_busy() || bg_next >= 0 || settings_dirty || inf_stage;	/* (to its end step: the ctags) */
}

static void save_alarms_step(void)
{
	char p[200];
	if (idx_i == 0 && store_known(&idx_stamp)) {
		idx_i = idx_n;	/* the files' last look stands: none of its own */
		return;
	}
	if (idx_i < idx_n) {
		idx_stamp = alarms_stamp_add(idx_stamp, idx_files[idx_i++]);
		if (idx_i == idx_n)
			store_adopt(idx_stamp);	/* a look, as store_changed's */
		return;
	}
	if (idx_i == idx_n) {
		path_join(p, sizeof(p), workdir, "ALARMS.IDX");
		alarms_save_begin(p, idx_files, idx_n, idx_stamp, opt.alarms, opt.h24);
		idx_i++;
		return;
	}
	switch (alarms_save_step()) {
	case 0:
		idx_known = 1;	/* written */
		idx_q = idx_q_new;
		idx_gen = idx_gen_new;
		/* fall through */
	case -1:
		idx_i = -1;
	}
}

static void alarms_boot(void)
{
	char p[200];
	int on = 1, h24 = opt.h24;
	if (!workdir[0])
		return;
	path_join(p, sizeof(p), workdir, "ALARMS.IDX");
	if (alarms_load(p, now_clock(), &on, &h24)) {
		opt.alarms = (short)on;
		opt.h24 = (short)h24;
	}
}

/* ---------------- commands ---------------- */

void cmd_new_event(void)
{
	editor_new(cur_day, -1, 0);
}

void cmd_delete_selected(void)
{
	EVENT *e = sel_occ.ev;
	EVENT *m;
	CTIME occ;
	int how = SAVE_ALL, ok;
	if (!e)
		return;
	if (!cal_writable(&cals[e->cal])) {
		alert(1, "[1][This event is in a calendar you can't change.][ OK ]");
		return;
	}
	m = ev_master(e);
	occ = sel_occ.orig;	/* the start the series gave it, even if it was moved */
	if (m && m->rr) {
		short b = alert(1, "[2][Delete only this occurrence of|\"%s\",|or this and all future ones?][Only this|All future|Cancel]",
				e->summary ? e->summary : "");
		if (b == 3)
			return;
		how = b == 1 ? SAVE_THIS : SAVE_FUTURE;
	} else if (alert(2, "[2][Delete \"%s\"?][Delete|Cancel]", e->summary ? e->summary : "New Event") != 1) {
		return;
	}
	busy(1);
	ok = ev_delete(e, how, occ);
	busy(0);
	if (!ok)
		alert(1, "[1][Couldn't delete it:|%s][ OK ]", cal_err);
	after_change();
}

static void import_file(int cal, const char *path);

void cmd_import(int cal)
{
	char path[200], name[20] = "", dir[200];
	short button = 0;
	snprintf(dir, sizeof(dir), "%s\\*.ICS", workdir);
	if (!fsel_exinput(dir, name, &button, "Import which .ics file?") || !button || !name[0])
		return;
	{
		char *slash = strrchr(dir, '\\');
		if (slash)
			slash[1] = 0;
		snprintf(path, sizeof(path), "%s%s", dir, name);
	}
	import_file(cal, path);
}

/* the events in an .ics file into the calendar */
static void import_file(int cal, const char *path)
{
	int n;
	busy(1);
	if (!cal_import(cal, path, &n)) {
		busy(0);
		alert(1, "[1][Import stopped: %s|(%d events imported)][ OK ]", cal_err, n);
	} else {
		busy(0);
		alert(1, "[1][%d event%s imported into|%s.][ OK ]", n, n == 1 ? "" : "s", cals[cal].name);
	}
	after_change();
}

/* EContact's window (github.com/whomper/EContact), where invitees are
   picked from: an empty VA_START opens it (common/ui/av.c) */
static void open_contacts(void)
{
	if (!av_send("ECONTACT", "") && !av_send("ECONTST ", ""))
		alert(1, "[1][EContact isn't loaded: put ECONTACT.ACC|in the root of the boot drive|and restart.][ OK ]");
}

/* the commands of the menu bar (the program) and the toolbar's menu */
enum { C_NONE, C_ABOUT, C_NEW, C_ADD, C_REFRESH, C_REFRESH_ALL, C_AGAIN, C_QUIT, C_COPY, C_DAY, C_WEEK,
       C_MONTH, C_YEAR, C_TODAY, C_GOTO, C_FIND, C_SIDE, C_HEB, C_SET, C_FONT, C_OFFL, C_FALC,
       C_LOG, C_KBD, C_HELP, C_KEYS, C_CONTACTS };

static const MBAR_ITEM m_desk[] = {
	{ "About ECal...", 0, C_ABOUT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_file[] = {
	{ "New event", "^N", C_NEW },
	{ "Add account or calendar...", 0, C_ADD },
	{ "Contacts...", 0, C_CONTACTS },
	{ "-", 0, 0 },
	{ "Refresh", "^R", C_REFRESH },
	{ "Refresh everything", "\x01^R", C_REFRESH_ALL },
	{ "Download everything again...", 0, C_AGAIN },
	{ "-", 0, 0 },
	{ "Quit", "^Q", C_QUIT },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_edit[] = {
	{ "Copy event", "^C", C_COPY },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_view[] = {
	{ "Day", "D", C_DAY },
	{ "Week", "W", C_WEEK },
	{ "Month", "M", C_MONTH },
	{ "Year", "Y", C_YEAR },
	{ "-", 0, 0 },
	{ "Today", "^T", C_TODAY },
	{ "Go to date...", "^G", C_GOTO },
	{ "Find...", "^F", C_FIND },
	{ "-", 0, 0 },
	{ "Calendar list", "\x01^S", C_SIDE },
	{ "Hebrew dates", 0, C_HEB },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_opts[] = {
	{ "Settings...", "^,", C_SET },
	{ "Font...", 0, C_FONT },
	{ "-", 0, 0 },
	{ "Work offline", 0, C_OFFL },
	{ "Falcon mode (TLS)", 0, C_FALC },
	{ "Write a log", 0, C_LOG },
	{ "Hebrew keyboard", "F10", C_KBD },
	{ 0, 0, 0 }
};
static const MBAR_ITEM m_help[] = {
	{ "ECal help", "Help", C_HELP },
	{ "Keys...", 0, C_KEYS },
	{ 0, 0, 0 }
};
static const MBAR_TITLE bar[] = {
	{ " ECal ", m_desk },
	{ " File ", m_file },
	{ " Edit ", m_edit },
	{ " View ", m_view },
	{ " Options ", m_opts },
	{ " Help ", m_help },
	{ 0, 0 }
};
static MBAR mb;
static int has_menu;

static void check(int cmd, int on)
{
	short o = mbar_object(&mb, cmd);
	if (o > 0)
		menu_icheck(mb.tree, o, on ? 1 : 0);
}

/* the menu bar's ticks, as things are now */
static void menu_update(void)
{
	if (!has_menu)
		return;
	check(C_DAY, cur_view == V_DAY);
	check(C_WEEK, cur_view == V_WEEK);
	check(C_MONTH, cur_view == V_MONTH);
	check(C_YEAR, cur_view == V_YEAR);
	check(C_SIDE, opt.sidebar);
	check(C_HEB, opt.hebdates);
	check(C_OFFL, opt.offline);
	check(C_FALC, opt.falcon);
	check(C_LOG, opt.log);
	check(C_KBD, hebrew_kbd);
}

static void command(int c)
{
	switch (c) {
	case C_ABOUT: dlg_about(); break;
	case C_NEW: cmd_new_event(); break;
	case C_ADD: dlg_add_account(); break;
	case C_REFRESH: cmd_refresh(0); break;
	case C_REFRESH_ALL: cmd_refresh(SYNC_ALL); break;
	case C_AGAIN: cmd_download_again(); break;
	case C_COPY: views_copy(); break;
	case C_DAY: views_goto(cur_day, V_DAY); break;
	case C_WEEK: views_goto(cur_day, V_WEEK); break;
	case C_MONTH: views_goto(cur_day, V_MONTH); break;
	case C_YEAR: views_goto(cur_day, V_YEAR); break;
	case C_TODAY: views_today(); break;
	case C_GOTO: dlg_goto(); break;
	case C_FIND: dlg_find(); break;
	case C_SIDE:
		opt.sidebar = !opt.sidebar;
		settings_later();
		win_layout();
		win_redraw(0);
		break;
	case C_HEB:
		opt.hebdates = !opt.hebdates;
		settings_later();
		after_change();
		break;
	case C_KBD: set_hebrew_kbd(!hebrew_kbd); break;
	case C_OFFL:
		opt.offline = !opt.offline;
		if (opt.offline)
			http_close_all();
		store_save_settings();
		updated_status();
		break;
	case C_FALC:
		if (falcon_mode_set(!opt.falcon)) {
			http_close_all();
			store_save_settings();
		}
		break;
	case C_LOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	case C_SET: dlg_settings(); break;
	case C_FONT: font_menu(); break;
	case C_HELP: help_open(); break;
	case C_KEYS: dlg_keys(); break;
	case C_CONTACTS: open_contacts(); break;
	case C_QUIT:
		quit_now = 1;
		win_close();
		break;
	}
	menu_update();
}

/* the toolbar's menu: the menu bar's commands, for the accessory, which
   has none */
void cmd_menu(short x, short y)
{
	static const struct {
		const char *text;
		short cmd;
	} m[] = {
		{ "  Add account or calendar...", C_ADD },
		{ "  New event            N", C_NEW },
		{ "-", 0 },
		{ "  Go to date...       ^G", C_GOTO },
		{ "  Find...             ^F", C_FIND },
		{ "  Today                T", C_TODAY },
		{ "  Contacts...", C_CONTACTS },
		{ "-", 0 },
		{ "  Download everything again...", C_AGAIN },
		{ "-", 0 },
		{ "  Calendar list      \x01^S", C_SIDE },
		{ "  Hebrew dates", C_HEB },
		{ "  Hebrew keyboard     F10", C_KBD },
		{ "-", 0 },
		/* as EMail's Options menu */
		{ "  Work offline", C_OFFL },
		{ "  Falcon mode (TLS)", C_FALC },
		{ "  Write a log", C_LOG },
		{ "-", 0 },
		{ "  Settings...         ^,", C_SET },
		{ "  Font...", C_FONT },
		{ "  Help...            Help", C_HELP },
		{ "  About ECal...", C_ABOUT },
		{ "-", 0 },
		{ "  Quit                ^Q", C_QUIT },
	};
	enum { N = sizeof(m) / sizeof(m[0]) };
	char ticked[N][40];
	const char *items[N];
	short i, k, n = is_acc ? N - 2 : N;
	for (i = 0; i < N; i++) {
		int on = (m[i].cmd == C_SIDE && opt.sidebar) || (m[i].cmd == C_HEB && opt.hebdates) ||
			 (m[i].cmd == C_KBD && hebrew_kbd) || (m[i].cmd == C_OFFL && opt.offline) ||
			 (m[i].cmd == C_FALC && opt.falcon) || (m[i].cmd == C_LOG && opt.log);
		items[i] = m[i].text;
		if (on) {
			str_copy(ticked[i], m[i].text, sizeof(ticked[i]));
			ticked[i][0] = '\x08';		/* the tick */
			items[i] = ticked[i];
		}
	}
	k = win_popup(x, y, items, n);
	if (k >= 0 && k < N)
		command(m[k].cmd);
}

/* ---------------- start-up ---------------- */

/* ECAL.INF lives in the root of the boot drive, where an accessory can
   always find it; FOLDER= in it says where the rest of ECal's files are.
   The program writes the folder it was started from; the accessory takes
   it from there, and asks when it is missing (locate_folder). */

/* ECAL.INF as the start found it, read once (guidelines 6.5: the
   settings file once per start) and without malloc, since the accessory
   asks it things before its heap is taken: FOLDER=, HEAPKB=, START=,
   FONT_ID=. The settings are then read from the same copy, when the file
   fitted and hasn't changed since */
static char inf_boot[8192];
static long inf_boot_len = -2;		/* -2: not read yet; -1: none, or used up */
static unsigned long inf_boot_stamp;	/* its date and size then */
static long inf_boot_size;

static const char *inf_start(void)
{
	if (inf_boot_len == -2) {
		int h;
		inf_boot_len = -1;
		if ((h = pf_open(inf_file, PF_READ)) >= 0) {
			long n;
			inf_boot_stamp = pf_handle_info(h, &inf_boot_size);	/* no look */
			n = pf_read(h, inf_boot, sizeof(inf_boot) - 1);
			pf_close(h);
			if (n >= 0) {
				inf_boot[n] = 0;
				inf_boot_len = n;
			}
		}
	}
	return inf_boot_len >= 0 ? inf_boot : "";
}

/* KEY= from ECAL.INF ("" when it has none). ECAL.PRG and ECAL.ACC write
   FOLDER= and START= near the top */
static void inf_value(const char *key, char *out, int size)
{
	const char *t = inf_start(), *k;
	int kl = (int)strlen(key);
	out[0] = 0;
	for (k = t; (k = str_istr(k, key)); k += kl)
		if (k == t || k[-1] == '\n') {
			int n = (int)strcspn(k + kl, "\r\n");
			str_copy(out, k + kl, n + 1 < size ? n + 1 : size);
			break;
		}
}

static void inf_folder(char *out, int size)
{
	inf_value("folder=", out, size);
	{
		int n = (int)strlen(out);
		while (n > 3 && (out[n - 1] == '\\' || out[n - 1] == ' '))
			out[--n] = 0;
	}
}

static void find_workdir(void)
{
	char path[160], old[200];
	short drv = (short)Dgetdrv(), boot = boot_drive();
	snprintf(inf_file, sizeof(inf_file), "%c:\\ECAL.INF", 'A' + boot);
	if (is_acc) {
		inf_folder(workdir, sizeof(workdir));
		if (!inf_boot_stamp) {
			/* settings from before ECAL.INF moved to the root */
			snprintf(workdir, sizeof(workdir), "%c:\\ECAL", 'A' + boot);
			path_join(old, sizeof(old), workdir, "ECAL.INF");
			if (!pf_exists(old))
				workdir[0] = 0;
		}
		if (workdir[0] && strlen(workdir) > 3 && !pf_exists(workdir))
			workdir[0] = 0;		/* gone: ask at the first opening (a
						   drive's root has no entry to find) */
		return;
	}
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(workdir, sizeof(workdir), "%c:%s", 'A' + drv, path);
	if (strlen(workdir) > 3 && workdir[strlen(workdir) - 1] == '\\')
		workdir[strlen(workdir) - 1] = 0;
}

/* the old place's ECAL.INF to the root, the first time */
static void move_inf(void)
{
	char old[200], *t;
	long len;
	if (inf_boot_stamp || !workdir[0])
		return;		/* (the start found it in the root) */
	path_join(old, sizeof(old), workdir, "ECAL.INF");
	if (!strcasecmp(old, inf_file) || !(t = pf_load(old, &len)))
		return;
	pf_save(inf_file, t, len);
	free(t);
}

/* the accessory without its folder: the user shows it where it is;
   0 if they cancel (nothing made, asked again at the next opening) */
static int locate_folder(void)
{
	char path[200], name[20] = "", why[200];
	short button = 0, boot = boot_drive();
	inf_folder(why, sizeof(why));
	for (;;) {
		short k = why[0] ? alert(1, "[2][ECal's folder is not at|%s.|Show me where it is?][Locate...|Use %c:\\ECAL|Cancel]",
					 why, 'A' + boot)
				 : alert(1, "[2][Where is ECal's folder, with|its calendars, keys and|certificates?][Locate...|Use %c:\\ECAL|Cancel]",
					 'A' + boot);
		char *e;
		if (k == 3)
			return 0;	/* Cancel, Esc (guideline 3.5) */
		if (k != 1)
			break;
		snprintf(path, sizeof(path), "%c:\\*.*", 'A' + boot);
		if (!fsel_exinput(path, name, &button, "ECal's folder: pick any file in it") || !button)
			continue;
		e = strrchr(path, '\\');
		if (e)
			*e = 0;
		if (strlen(path) == 2)
			strcat(path, "\\");
		if (pf_exists(path) || strlen(path) == 3) {
			str_copy(workdir, path, sizeof(workdir));
			return 1;
		}
	}
	snprintf(workdir, sizeof(workdir), "%c:\\ECAL", 'A' + boot);
	pf_mkdir(workdir);
	return 1;
}

/* an accessory takes its memory once, now: later it would belong to
   whichever program is in front. HEAPKB=n in ECAL.INF overrides */
static long boot_free, boot_taken;	/* KB at boot, for the log */

static void acc_heap(void)
{
	char kb[12];
	long avail = (long)Malloc(-1L), alt = (long)Mxalloc(-1L, 1), want;
	void *p;
	want = tls_available && get_cookie(0x5F435055L) >= 30 ? 1536L * 1024 : 256L * 1024;
	/* TT/Fast RAM (a TT, an accelerated Falcon): the heap goes there, and
	   there is room for more */
	if (alt == -32 || alt < 0)
		alt = 0;
	if (alt >= 256L * 1024) {
		avail = alt;
		if (want < 4096L * 1024)
			want = 4096L * 1024;
	}
	inf_value("heapkb=", kb, sizeof(kb));
	if (atol(kb) >= 64)
		want = atol(kb) * 1024;
	boot_free = avail / 1024;
	if (want > avail / 2)
		want = avail / 2;
	/* little memory (a 1 MB ST with a network): all but what the desktop
	   needs to start (EmuDesk: 32 KB); none at all rather than less for
	   it, and never more later: the heap is fixed either way */
	if (want < 96L * 1024)
		want = avail - 32L * 1024;
	if (want < 8L * 1024) {
		heap_add(0, 0);
		return;
	}
	p = Mxalloc(want, alt ? 1 : 3);
	if (alt && (long)p <= 0) {
		/* no luck in TT RAM after all: ST RAM, at its own size */
		long st = (long)Malloc(-1L) / 2;
		if (want > st)
			want = st;
		p = 0;
	}
	if ((long)p == -32 || (long)p <= 0)
		p = Malloc(want);
	if ((long)p > 0) {
		heap_add(p, (size_t)want);
		boot_taken = want / 1024;
	} else {
		heap_add(0, 0);
	}
}

static void files_check(void);

static int sync_pending;	/* the first refresh, once the window is drawn */
static void net_tick(void);
static void connecting(int begins);

/* the settings in three steps (the accessory reads them in the
   background): ECAL.INF and the keys, the files' stamps, the rest */
static int settings_stage;
static int mark_begun;		/* stage 1 looks at the files itself */

static int settings_step(void)
{
	if (settings_stage == 0) {
		if (!workdir[0] && !locate_folder())
			return 0;	/* cancelled: no settings, no window */
		secret_warn = warn_plain;	/* before anything is saved */
		move_inf();
		store_init_begin(workdir, inf_file);
		settings_stage = 1;
		mark_begun = 0;
		return 1;
	}
	if (settings_stage == 1) {
		/* the files as we found them, before they are read: a file a
		   step, or the stamp ALARMS.IDX's check took of them at boot */
		const char *f[3];
		int n = store_files(f, 3);
		unsigned long h, inf;
		long size;
		if (!mark_begun && alarms_check_stamp(f, n, &h)) {
			store_mark_known(h);
			inf = alarms_check_first(&size);
		} else {
			mark_begun = 1;
			if (store_mark_step())
				return 1;
			if (store_known(&h))
				alarms_verify_with(f, n, h);	/* the check needs no looks of its own */
			inf = store_inf_seen(&size);
		}
		/* the copy read at the start, if the file is as it was then */
		if (inf_boot_len >= 0 && inf_boot_len < (long)sizeof(inf_boot) - 1 &&
		    inf == inf_boot_stamp && size == inf_boot_size)
			store_init_text(inf_boot);
		inf_boot_len = -1;	/* later readings (ECAL.PRG changed it) go to the file */
		settings_stage = 3;
		return 1;
	}
	if (settings_stage == 3) {
		/* ECAL.INF, a few lines a step */
		if (store_init_step())
			return 1;
		if (strcasecmp(opt.folder, workdir))
			store_save_settings();	/* FOLDER= as it is now */
		settings_stage = 2;
		return 1;
	}
	if (settings_stage == 2) {
		path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
		path_join(tls_roots_cache, sizeof(tls_roots_cache), workdir, "ROOTS.DAT");
		path_join(tls_seed_path, sizeof(tls_seed_path), workdir, "SEED.DAT");
		/* CACERT.PEM read a piece at a time, never open across the
		   network's waits (guidelines 2.2); ROOTS.DAT then checked by
		   its date and size, from one directory look */
		tls_read_at = pf_read_at;
		tls_file_stamp = pf_stamp;
		tls_file_info = pf_info;
		pf_size_open = pf_handle_size;	/* pf_load: the file opened, no look first */
		tls_tz_minutes = tz_offset(home_zone(), now_utc());
		set_logging();
		settings_stage = 4;
		return 1;
	}
	if (settings_stage == 4) {
		net_init();
		settings_stage = 5;
		return 1;
	}
	settings_stage = 0;
	pf_idle = busy_idle;
	conn_tick = net_tick;
	http_connecting = connecting;
	cal_status = status;
	tls_note = status;
	falcon_dsp_init();
	falcon_dsp_ec_init();
	cur_view = opt.view >= V_DAY && opt.view <= V_YEAR ? opt.view : V_MONTH;
	cur_day = ct_days(now_view());
	shown_today = cur_day;
	views_scroll_to(opt.daystart);
	/* passwords typed into ECAL.INF by hand: encrypt them right away */
	if (store_plain_passwords && !store_lost_passwords)
		store_save_settings();
	hebrew_kbd = opt.hebrew;
	{
		/* where TOS put us: TT/Fast RAM starts at 16 MB */
		char m[100];
		void *h = malloc(16);
		snprintf(m, sizeof(m), "ECal %s: program at %08lx (%s RAM), memory at %08lx (%s RAM)",
			 cal_version, (unsigned long)settings_step,
			 (unsigned long)settings_step >= 0x01000000UL ? "TT" : "ST",
			 (unsigned long)h, (unsigned long)h >= 0x01000000UL ? "TT" : "ST");
		free(h);
		conn_log("ECal", "--", m, (long)strlen(m));
		if (is_acc) {
			snprintf(m, sizeof(m), "at boot: %ld KB free, the accessory took %ld KB", boot_free, boot_taken);
			conn_log("ECal", "--", m, (long)strlen(m));
		}
		snprintf(m, sizeof(m), "the window is drawn %s", win_drawing());
		conn_log("ECal", "--", m, (long)strlen(m));
		if (*tos_clock())
			conn_log("ECal", "--", tos_clock(), (long)strlen(tos_clock()));
		conn_log_flush();
	}
	settings_ready = 1;
	return 0;
}

static void load_settings(void)
{
	while (settings_step())
		;
}

/* the calendars from the copies on disk, with the window already open */
static int bg_loaded;		/* calendars read so far (the accessory, after boot) */

static int lost_told;

/* one step of reading the calendars: 0 when all are read */
static int load_step(void)
{
	if (cal_loading()) {
		cal_load_step();
		return 1;
	}
	if (bg_loaded < ncals) {
		cal_load_begin(&cals[bg_loaded++]);
		return 1;
	}
	if (!data_ready) {
		views_invalidate();
		data_ready = 1;
		lost_told = 0;
		alarms_changed();
	}
	return 0;
}

/* messages while ECal waits for the network (busy.c): our window's
   redraws at once, the rest kept for the main loop's next turn (a
   background step, a refresh: not the moment to open the window) */
static short busy_kept[8][8];
static int nbusy_kept;

static void busy_msg(const short *msg)
{
	if (msg[0] == WM_REDRAW && win_h > 0 && msg[3] == win_h) {
		win_message((short *)msg);
		return;
	}
	if (nbusy_kept < 8)
		memcpy(busy_kept[nbusy_kept++], msg, sizeof(busy_kept[0]));
}

static void busy_resend(void)
{
	int i;
	for (i = 0; i < nbusy_kept; i++)
		appl_write(ap_id, 16, busy_kept[i]);
	nbusy_kept = 0;
}

/* events read but not kept for want of memory (a 1 MB ST): said once,
   with the window open */
static void tell_lost(void)
{
	int i, n = 0, some = 0;
	char which[80] = "";
	if (lost_told || win_h <= 0)
		return;
	lost_told = 1;
	for (i = 0; i < ncals; i++)
		if (cals[i].lost) {
			if (cals[i].lost < 0)
				some = 1;
			else
				n += cals[i].lost;
			if (!which[0])
				str_copy(which, cals[i].name, sizeof(which));
		}
	if (!n && !some)
		return;
	if (some)
		alert(1, "[1][Memory is full: not all of|%s and maybe others could be|read. Give ECal more memory|(HEAPKB= in ECAL.INF) or keep|fewer days (Settings).][ OK ]", which);
	else
		alert(1, "[1][Memory is full: %d event%s could|not be read (in %s and maybe|others). Give ECal more memory|(HEAPKB= in ECAL.INF) or keep|fewer days (Settings).][ OK ]", n, n == 1 ? "" : "s", which);
}

static void load_calendars(void)
{
	busy(1);
	status("Reading your calendars...");
	while (load_step())
		;
	status("");
	busy(0);
	if (store_lost_passwords)
		alert(1, "[1][ECAL.KEY is missing or new: type the passwords again (right-click an account).][ OK ]");
}

/* First the window, then today from the copies on disk, and only then,
   from the event loop once everything is drawn, the refresh */
static void open_window(void)
{
	if (!screen_check("ECal", scr_w))	/* not in ST low (the accessory) */
		return;
	int first = win_h <= 0;
	bg_opened();			/* the user is here: work may go on */
	if (first && sync_busy()) {
		sync_stop();		/* a background refresh half way: the window's own finishes it */
		bg_next = -1;
		conn_log_hold(0);	/* a round given up: its log written out */
		views_select_none();
		views_invalidate();
	}
	if (first)
		files_check();
	if (!settings_ready)
		load_settings();
	if (!settings_ready)
		return;			/* the folder question cancelled */
	if (!fonts_ready) {
		font_apply();
		fonts_ready = 1;
	}
	win_open();
	if (win_h <= 0 || !first)
		return;
	if (!data_ready) {
		win_redraw(0);		/* the window, before the disk is read */
		load_calendars();
	}
	after_change();			/* today's events, from what is kept here */
	updated_status();
	tell_lost();
	if (!naccts) {
		alert(1, "[1][Welcome to ECal!||Let's add your iCloud or other calendar.][ OK ]");
		dlg_add_account();
	} else if (opt.sync_open && !opt.offline) {
		sync_pending = 1;	/* as EMail's first check: problems in an alert */
	}
}

/* the calendar an .ics file from another program goes into: the only
   one that can take it, else the one picked; -1 for none */
static int cal_for_import(void)
{
	const char *items[48];
	short idx[48], n = 0, i, r;
	for (i = 0; i < ncals && n < 48; i++)
		if (cal_writable(&cals[i])) {
			items[n] = cals[i].name;
			idx[n++] = i;
		}
	if (!n) {
		alert(1, "[1][Add a calendar first: Add account|or calendar... in the menu (On|My Atari needs no server).][ OK ]");
		return -1;
	}
	if (n == 1)
		return idx[0];
	r = dlg_pick("Import into which calendar?", items, n, 0);
	return r >= 0 && r < n ? idx[r] : -1;
}

/* VA_START (common/ui/av.c), from the desktop or another program:
   nothing opens the window; .ics files are imported, a webcal: address
   subscribed to */
static void av_start(const char *s)
{
	char path[200];
	const char *p = s;
	open_window();
	if (win_h <= 0)
		return;
	if (!strncasecmp(s, "webcal", 6)) {
		dlg_subscribe(s);
		return;
	}
	while ((p = av_arg(p, path, sizeof(path))) != 0) {
		long l = (long)strlen(path);
		int cal;
		if (l < 4 || strcasecmp(path + l - 4, ".ICS")) {
			alert(1, "[1][ECal takes calendars (.ics|files, webcal: addresses):|%s isn't one.][ OK ]", path);
			continue;
		}
		cal = cal_for_import();
		if (cal < 0)
			return;
		import_file(cal, path);
	}
}

static void wait_release(void)
{
	short mx, my, mb, ks;
	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 3))
			return;
		evnt_timer_(10);
	}
}

/* one click or two. GEM's own count holds every click back until the
   double-click time has passed; here only clicks that can be doubles
   wait (events, days, calendar names), and they wait before any work is
   done, so the second click isn't missed. The tick boxes, the toolbar
   and the right button act at once */
static short click_count(short mx, short my, short button)
{
	unsigned long t0 = pf_ms();
	short x, y, b, k, released = 0;
	if ((button & 2) || win_instant(mx, my) || side_instant(mx, my))
		return 1;
	/* the button itself, watched every few milliseconds: waiting for
	   GEM's next click event missed a quick second click that was over
	   before we asked for it */
	while (pf_ms() - t0 < 300UL) {
		graf_mkstate(&x, &y, &b, &k);
		if (!(b & 1))
			released = 1;
		else if (released)
			return (x - mx < 6 && mx - x < 6 && y - my < 6 && my - y < 6) ? 2 : 1;
		evnt_timer_(5);
	}
	return 1;
}

/* the tooltip over an event: shown when the mouse rests on one, gone
   when it moves, a key is pressed or the button clicked */
static short tip_on, tip_tried, hover_x = -1, hover_y = -1;
static unsigned long hover_at;
static GRECT tip_r;

static void tip_hide(void)
{
	if (!tip_on)
		return;
	tip_on = 0;
	win_redraw(&tip_r);
}

static void tip_show(void)
{
	char title[80], when[60];
	short t, dd, n1, n2, n;
	GRECT r;
	tip_tried = 1;
	if (win_h <= 0 || wind_find(hover_x, hover_y) != win_h)
		return;
	wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
	if (t != win_h || !views_tip(hover_x, hover_y, title, sizeof(title), when, sizeof(when)))
		return;
	n1 = (short)strlen(title);
	n2 = (short)strlen(when);
	if (n1 > 40)
		n1 = 40;
	n = n1 > n2 ? n1 : n2;
	r.w = (n + 2) * cw;
	r.h = 2 * ch + 6;
	r.x = hover_x + cw;
	r.y = hover_y + ch + 2;
	/* inside the window: to the left or above the mouse at the edges */
	if (r.x + r.w > work.x + work.w)
		r.x = hover_x - r.w - cw / 2;
	if (r.x < work.x)
		r.x = work.x;
	if (r.y + r.h > work.y + work.h)
		r.y = hover_y - r.h - 2;
	if (r.y < work.y)
		r.y = work.y;
	tip_r = r;
	tip_r.w++;
	tip_r.h++;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	clip_on(&work);
	fill(&r, 0);
	frame(&r, 1);
	text_at(r.x + cw, r.y + 3, title, n1, n1, TX_BOLD | (line_rtl(title, n1) ? TX_RIGHT : 0));
	text_at(r.x + cw, r.y + 3 + ch, when, n2, n2, TX_LTR);
	clip_off();
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	tip_on = 1;
}

/* The accessory, its window closed: see work_step() */
static unsigned long step_max[8];	/* the longest step seen of each kind of work */
static unsigned long slice_ms = BG_SLICE_030;	/* bgwork's slice (BG_SLICE_000 on a 68000) */

/* The accessory stands aside while ECAL.PRG runs (twin.h; the AERA
   guidelines, 2.3): it doesn't sync, show reminders or open its window,
   and hands AC_OPEN and VA_START to the program. What the program
   changed in ECAL.INF and the caches meanwhile is read again before
   anything is written. */
static int prg_was;

static void files_reload(void)
{
	static const char m[] = "the files changed since ECal last wrote them: reading them again";
	conn_log("ECal", "--", m, (long)(sizeof(m) - 1));
	sync_stop();
	http_close_all();
	views_select_none();
	cal_load_stop();
	store_release();
	views_invalidate();
	settings_ready = 0;
	settings_stage = 0;
	data_ready = 0;
	bg_loaded = 0;
	bg_next = -1;
	conn_log_hold(0);	/* a round given up: its log written out */
	find_workdir();			/* FOLDER= may name another folder now */
}

static void files_check(void)
{
	if (is_acc && settings_ready && store_changed())
		files_reload();
}

static int prg_runs(void)
{
	static unsigned long asked;
	unsigned long now = pf_ms();
	if (!is_acc)
		return 0;
	if (asked && now - asked < 1000UL)
		return prg_was;		/* asked the AES (appl_find) a moment ago */
	asked = now ? now : 1;
	if (twin_prg() >= 0) {
		prg_was = 1;
		return 1;
	}
	if (prg_was) {
		prg_was = 0;
		files_check();
	}
	return 0;
}

/* a background refresh due (BGSYNC, the accessory with its window closed) */
static int bg_sync_due(void)
{
	unsigned long now = pf_ms();
	int i, due;
	if (!is_acc || win_h > 0 || !opt.bgsync || opt.offline || !naccts || !data_ready)
		return 0;
	if (bg_next >= 0 || bg_first)
		return 1;
	due = opt.refresh > 0 && now - last_refresh > (unsigned long)opt.refresh * 60000UL;
	for (i = 0; i < ncals && !due; i++) {
		ACCOUNT *a = &accts[cals[i].acct];
		due = a->refresh > 0 && cals[i].synced && now_utc() - cals[i].synced >= a->refresh &&
		      now - last_refresh > 60000UL;
	}
	return due;
}

/* Work done in the background, in slices between event calls
   (common/ui/bgwork.c): the settings, then the calendars an event at a
   time, then the reminders an event at a time, then (BGSYNC) a refresh
   a calendar at a time. The accessory starts it START= after boot, or
   when it is opened; reminders come from ALARMS.IDX meanwhile. */
static int work_pending(void)
{
	/* (while ECAL.PRG runs, bg_may_work() says no: the work is its) */
	if (!settings_ready)
		return workdir[0] != 0;	/* the folder is asked for at the first opening */
	return !data_ready || alarms_pending() || save_alarms_pending() || inf_stage || bg_sync_due();
}

/* what the next step does: 1 settings, 2 calendars, 3 reminders, 4 ALARMS.IDX, 5 refresh,
   6 refresh waiting for the server (timed apart: a step of it is the network's time),
   7 writing ECAL.INF */
static int work_kind(void)
{
	if (!settings_ready)
		return 1;
	if (!data_ready || cal_loading())
		return 2;
	if (alarms_pending())
		return 3;
	if (save_alarms_pending() && !idx_wait())
		return 4;
	if (inf_stage)
		return 7;
	if (sync_busy() ? sync_net_step() : bg_next >= 0 && bg_next < ncals)
		return 6;
	return 5;
}

/* a long answer coming in with the window closed: the machine handed
   back now and then while it does (the waits for it already do) */
static void net_tick(void)
{
	static unsigned long last;
	unsigned long now = pf_ms();
	if (!is_acc || win_h > 0 || now - last < slice_ms / 2)
		return;
	last = now;
	evnt_timer_(0);
}

/* a step that waits for the server; the opening of a connection in it
   (with Falcon mode, the TLS handshake on the Atari) is timed apart,
   up to an event call once it is open */
static void net_tag(void)
{
	aes_profile_tag("work: refresh (network)");
}

static void connecting(int begins)
{
	if (!is_acc || win_h > 0)
		return;
	if (begins) {
		aes_profile_tag("work: refresh (new connection)");
	} else {
		evnt_timer_(0);
		aes_profile_tag("work: refresh (network)");
	}
}

static void work_step(void)
{
	if (!settings_ready) {
		aes_profile_tag(settings_stage == 0 || settings_stage == 3 ? "work: settings (ECAL.INF)" :
				settings_stage == 1 ? "work: settings (stamps)" :
				settings_stage == 2 ? "work: settings (log)" :
				settings_stage == 4 ? "work: settings (network)" : "work: settings (the rest)");
		settings_step();
		return;
	}
	aes_profile_tag("work: calendars");
	if (load_step())
		return;
	if (alarms_pending()) {
		aes_profile_tag(alarms_sorting() ? "work: reminders (sorting)" : "work: reminders");
		if (!alarms_step())
			save_alarms();	/* the next steps write it */
		return;
	}
	if (save_alarms_pending() && !idx_wait()) {
		/* once the refresh is over, not after each calendar */
		aes_profile_tag("work: ALARMS.IDX");
		save_alarms_step();
		return;
	}
	if (inf_stage) {
		aes_profile_tag(inf_stage <= 2 ? "work: ECAL.INF (text)" : inf_stage == 3 ? "work: ECAL.INF (write)" :
				"work: ECAL.INF (stamps)");
		if (inf_stage == 1) {
			sb_init(&inf_text);
			store_settings_text(&inf_text, 0);
			inf_stage = 2;
		} else if (inf_stage == 2) {
			store_settings_text(&inf_text, 1);
			inf_stage = 3;
		} else {
			store_write_text(&inf_text);	/* (the files' look takes it as ours later) */
			inf_stage = 0;
			save_alarms();	/* its stamp has ECAL.INF's */
		}
		return;
	}
	aes_profile_tag("work: refresh");
	if (!bg_sync_due())
		return;
	if (bg_next < 0) {
		/* before anything is written: has ECAL.PRG changed the
		   files? A file a step */
		int changed, i;
		aes_profile_tag("work: refresh (files)");
		if (store_changed_step(&changed))
			return;
		if (changed) {
			files_reload();
			return;
		}
		for (i = 0; i < naccts; i++)
			accts[i].err[0] = 0;	/* this round's shown at the next opening */
		bg_first = 0;
		bg_changed = 0;
		bg_next = 0;
		conn_log_hold(1);	/* the log written with its buffer, not a line at a time (6.5) */
		return;
	}
	if (sync_busy()) {
		/* a CalDAV calendar, in steps (calstore.c) */
		if (sync_net_step()) {
			net_tag();
		} else {
#ifdef AES_PROFILE
			static char tag[40];
			snprintf(tag, sizeof(tag), "work: refresh (%s)", sync_phase_name());
			aes_profile_tag(tag);
#endif
		}
		if (!sync_step()) {
			bg_changed |= sync_changes;
			sync_changes = 0;
		}
		return;
	}
	if (bg_next < ncals) {
		CALENDAR *c = &cals[bg_next++];
		if (sync_begin(c, 0))
			return;
		/* a subscription: fetched in one step, read again in steps
		   (load_step); errors stay in the account */
		net_tag();
		cal_defer_load = 1;
		if (sync_calendar(c, 0) > 0)
			bg_changed = 3;
		cal_defer_load = 0;
		if (cal_loading())
			data_ready = 0;
		return;
	}
	bg_next = -1;
	last_refresh = pf_ms();
	http_close_all();
	conn_log_hold(0);	/* (writes it out) */
	if (bg_changed & 2)
		settings_later();	/* the ctags */
	if (bg_changed & 1) {
		views_select_none();	/* (it may have been one of the events replaced) */
		views_invalidate();
		alarms_changed();	/* the reminders, then ALARMS.IDX, in steps */
	}
}

/* after a click that was acted on at once: a second press near it
   before the double-click time is up (counted from the first press) */
static int second_click(short mx, short my, unsigned long t0)
{
	short x, y, b, k, released = 0;
	while (pf_ms() - t0 < 350UL) {
		graf_mkstate(&x, &y, &b, &k);
		if (!(b & 1))
			released = 1;
		else if (released)
			return x - mx < 6 && mx - x < 6 && y - my < 6 && my - y < 6;
		evnt_timer_(5);
	}
	return 0;
}

/* alerts (common/ui/alert.c): Hebrew letters where the font has them */
static void alert_heb(char *line, long n)
{
	if (opt.hebfont != HEB_ATARI)
		heb_font_map(line, n, opt.hebfont);
}

/* ECal (the program) crashed and CRASH.LOG is written
   (common/atari/crash.c): say so and leave GEM tidy. Only the AES and
   VDI: the heap may be what went wrong */
static void crash_quit(const char *what)
{
	char a[200];
	graf_mouse(ARROW, 0);
	snprintf(a, sizeof(a), "[3][ECal has crashed|(%s).|What happened is in|CRASH.LOG, next to ECAL.PRG.][ Quit ]", what);
	form_alert(1, a);
	if (win_h > 0) {
		wind_close(win_h);
		wind_delete(win_h);
	}
	v_clsvwk(vdi_h);
	appl_exit();
}


#ifdef STACK_PROBE
/* make STACKPROBE=1: the stack painted at start, and on the console
   (natfeats) the least of it never touched so far, STACKFREE bytes, as
   the rigs drive ECal (how big the ST edition's can be: ST_STACK) */
static unsigned long *stk_lo, stk_best;
static void stk_paint(void)
{
	/* the stack is crt0.S's BSS right after _is_acc */
	unsigned long *p = (unsigned long *)(((unsigned long)(&_is_acc + 1) + 3) & ~3UL), here;
	stk_lo = p;
	here = (unsigned long)&p - 512;
	while ((unsigned long)p < here)
		*p++ = 0x5AA5C33CUL;
}
static void stk_check(void)
{
	unsigned long *p = stk_lo, untouched;
	while (*p == 0x5AA5C33CUL)
		p++;
	untouched = (unsigned long)p - (unsigned long)stk_lo;
	if (!stk_best || untouched + 64 < stk_best) {
		char b[40];
		stk_best = untouched;
		snprintf(b, sizeof(b), "STACKFREE %lu\n", untouched);
		nf_debug(b);
	}
}
#endif
int main(void)
{
	short work_out[57], msg[8], d;
	AEVENT ev;

	is_acc = _is_acc;
#ifdef STACK_PROBE
	stk_paint();
#endif
	{
		extern int mem_unaligned_ok;
		mem_unaligned_ok = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	}
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	twin_init("ECAL|ECALST");
	if (!is_acc && twin_handover(av_command())) {
		appl_exit();		/* ECAL.ACC runs, and has it */
		return 0;
	}
	if (is_acc)
		menu_id = menu_register(ap_id, "  ECal");
	vdi_h = graf_handle(&gl_wchar, &gl_hchar, &d, &d);
	cw = gl_wchar;
	ch = gl_hchar;
	vdi_h = v_opnvwk_(vdi_h, work_out);
	scr_w = work_out[0] + 1;
	scr_h = work_out[1] + 1;
	ncolors = work_out[13];
	vq_extnd(vdi_h, 1, work_out);
	planes = work_out[4];
	if (ncolors <= 0 || planes > 8)
		ncolors = 256;
	vst_alignment(vdi_h, 0, 5);
	wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);
	store_watch = is_acc;
	find_workdir();
	crash_install(workdir, "ECal", cal_version);	/* the program only */
	crash_cleanup = crash_quit;
	alert_hebrew = alert_heb;
	crash_report = conn_log_append;	/* the report in ECAL.LOG too */
	/* names from EContact for invitations (the guidelines, 4.1): its
	   memory before the accessory's heap is taken */
	people_init(64, 0, dlg_names_came, editor_people_picked);
	busy_message = busy_msg;
	if (is_acc) {
		/* GDOS fonts now or not at all (font.c): under NVDI always, else as ECAL.INF says */
		char f[8];
		inf_value("font_id=", f, sizeof(f));
		font_boot(f[0] && atoi(f) != 1);
	}
	st_ram_init();
	win_buffer_init();
	help_init();
	av_init("ECAL|ECALST");		/* the desktop's AV server, if one runs yet */
	dlg_after = win_frame_refresh;
	if (!is_acc && !screen_check("ECal", scr_w)) {	/* not in ST low */
		v_clsvwk(vdi_h);
		appl_exit();
		return 1;
	}
	{
		/* when work may start: the accessory START= after boot (ECAL.INF,
		   read before its heap is taken), the program at once */
		char st[16];
		inf_value("start=", st, sizeof(st));
		bg_init(is_acc ? bg_parse(st) : BG_NOW);
		slice_ms = get_cookie(0x5F435055L) >= 20 ? BG_SLICE_030 : BG_SLICE_000;	/* _CPU */
		cal_piece = get_cookie(0x5F435055L) >= 20 ? 32768L : 16384L;	/* guidelines 6.5 */
		cal_gather = cal_piece;
		if (!is_acc) {
			/* the program's heap grows (TT RAM first, common/atari/libc.c):
			   a refresh gathers up to an eighth of what is free, 512 KB at
			   most, and writes it in one go */
			long fr = (long)Mxalloc(-1L, 3);
			if (fr == -32 || fr < 0)
				fr = (long)Malloc(-1L);
			fr /= 8;
			if (fr > 512L * 1024)
				fr = 512L * 1024;
			if (fr > cal_gather)
				cal_gather = fr;
		}
		if (!is_acc)
			bg_opened();
	}
	if (is_acc) {
		acc_heap();
		if (boot_taken < 512)
			conn_log_hold_bytes = 0;	/* a small fixed heap: the log keeps its 4 KB through a refresh */
	} else {
		/* the menu bar (the accessory has the toolbar's menu) */
		if (mbar_build(&mb, bar, gl_wchar, gl_hchar, scr_w, scr_h) == 0)
			has_menu = menu_bar(mb.tree, 1) != 0;
		if (av_command()[0])
			av_start(av_command());	/* an .ics the desktop opens with ECal */
		else
			open_window();
		menu_update();		/* the settings are read now */
	}
	alarms_boot();			/* reminders at once, the calendars read later */
#ifdef AES_PROFILE
	nf_debug("AESPROF ECal: timing the stretches between event calls\n");
#endif
	crash_offer("ECal", "next to ECAL.PRG");

	while (!quit_now) {
		short flags = MU_MESAG | MU_TIMER, which, hovering;
		unsigned long wait = sync_pending ? 150 : 1000;
		if (win_h > 0)
			flags |= MU_KEYBD | MU_BUTTON;
		if (win_h > 0 && !win_iconified)
			flags |= MU_M1;
		/* the mouse moving away from where it was last seen */
		evnt_set_m1(1, hover_x, hover_y, 1, 1);
		hovering = win_h > 0 && !win_iconified && !tip_tried && hover_x >= 0;
		if (hovering && wait > 100)
			wait = 100;
		wait = bg_timer(work_pending(), wait);
		/* a click comes at once; where a double click means something,
		   click_count() waits for the second one before anything is done */
		/* a refresh waiting: shortly, once the redraws are done */
		conn_stop = 0;		/* a stop is for one operation */
		busy_resend();		/* messages that came meanwhile */
#ifdef STACK_PROBE
		stk_check();
#endif
		people_poll();		/* questions to EContact */
		/* a key typed while busy comes first, as if typed now */
		if (win_h > 0 && busy_key(&ev))
			which = MU_KEYBD;
		else
			which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, wait, msg, &ev);
		if (win_iconified)	/* an icon takes no keys or clicks (taken, not left for later) */
			which &= ~(MU_KEYBD | MU_BUTTON);
		if (which & (MU_M1 | MU_KEYBD | MU_BUTTON | MU_MESAG)) {
			short k;
			if (which & MU_M1)
				graf_mkstate(&hover_x, &hover_y, &k, &k);
			hover_at = pf_ms();
			tip_tried = (which & (MU_KEYBD | MU_BUTTON)) != 0;
			tip_hide();
		} else if (hovering && pf_ms() - hover_at >= 700UL) {
			tip_show();
		}
		if (which & MU_MESAG) {
			const char *va;
			switch (msg[0]) {
			case AC_OPEN:
				if (msg[4] == menu_id) {
					if (twin_pass(""))	/* ECAL.PRG runs: its window to the front */
						break;
					av_init("ECAL|ECALST");	/* a desktop started since */
					open_window();
				}
				break;
			case MN_SELECTED:
				if (has_menu) {
					command(mbar_command(&mb, msg[4]));
					menu_tnormal(mb.tree, msg[3], 1);
				}
				break;
			case AC_CLOSE:
				twin_reset();		/* another program in front */
				/* the program in front ended: the AES took our window */
				if (msg[3] == menu_id) {
					av_window(win_h, 0);
					win_h = -1;
					settings_flush();
				}
				break;
			case AP_TERM:
				if (!is_acc)
					quit_now = 1;
				win_close();
				break;
			default: {
				DROP d;
				/* dropped: .ics files as VA_START's; text, a new event */
				switch (dd_message(msg, DD_WANT_FILES | DD_WANT_TEXT, &d)) {
				case DD_FILES: av_start(d.data); break;
				case DD_TEXT:
					open_window();
					if (win_h > 0)
						editor_new_titled(cur_day, d.data);
					break;
				case DD_NONE:
					switch (av_message(msg, &va)) {
					case AV_START:
						if (!(is_acc && twin_pass(va)))
							av_start(va);
						break;
					case AV_PEOPLE:
						people_message(va, msg[1]);	/* EContact's answer */
						break;
					case AV_DONE:
						break;
					default:
						win_message(msg);
					}
				}
				dd_done(&d);
			}
			}
		}
		if ((which & MU_KEYBD) && win_h > 0) {
			short scan = KEY_SCAN(ev.kreturn);
			if (scan == 0x44)
				set_hebrew_kbd(!hebrew_kbd);
			else if (scan == 0x62)
				help_open();
			else if (!crash_test(scan))	/* test builds: F1-F4 crash */
				win_key(ev.kstate, ev.kreturn);
		}
		if ((which & MU_BUTTON) && win_h > 0) {
			short h = wind_find(ev.mx, ev.my), t, dd;
			wind_get(0, WF_TOP, &t, &dd, &dd, &dd);
			if (h == win_h) {
				if (h != t)
					wind_set(h, WF_TOP, 0, 0, 0, 0);
				else if (!(ev.mbutton & 2) && rect_hit(&r_view, ev.mx, ev.my)) {
					/* the calendar itself: the click acts at once (a
					   selection is harmless), a second one opens */
					static unsigned long last_t;
					static short last_x, last_y;
					unsigned long t0 = pf_ms();
					short n = 1;
					if (is_acc ? ev.breturn >= 2 :
					    t0 - last_t < 450UL && ev.mx - last_x < 6 && last_x - ev.mx < 6 &&
					    ev.my - last_y < 6 && last_y - ev.my < 6)
						n = 2;	/* counted by the AES, or two events close together */
					win_mouse(ev.mx, ev.my, ev.mbutton, n, ev.kstate);
					last_t = n == 2 ? 0 : t0;
					last_x = ev.mx;
					last_y = ev.my;
					if (n == 1 && !is_acc && second_click(ev.mx, ev.my, t0)) {
						win_mouse(ev.mx, ev.my, ev.mbutton, 2, ev.kstate);
						last_t = 0;
					}
				} else {
					short n = click_count(ev.mx, ev.my, ev.mbutton);
					/* an accessory gets the first click only once the AES
					   has finished counting: its count is the one to use */
					if (is_acc && ev.breturn >= 2 && !(ev.mbutton & 2) &&
					    !win_instant(ev.mx, ev.my) && !side_instant(ev.mx, ev.my))
						n = 2;
					win_mouse(ev.mx, ev.my, ev.mbutton, n, ev.kstate);
				}
			}
			wait_release();
		}
		if (which & (MU_KEYBD | MU_BUTTON | MU_MESAG))
			menu_update();
		if ((which & MU_TIMER) && settings_dirty && pf_ms() - settings_at >= 3000UL) {
			if (is_acc && win_h <= 0) {
				settings_dirty = 0;
				inf_stage = 1;	/* in the background (work_step) */
			} else {
				aes_profile_tag("settings: ECAL.INF");
				settings_flush();
			}
		}
		aes_profile_tag("events");
		bg_tick();
		if (which & MU_TIMER) {
			prg_runs();		/* ECAL.PRG ended: what it changed is read again */
			alarms_verify_step();	/* ALARMS.IDX's files at boot: one a tick until checked */
			if (alarms_verified) {
				/* right as it is: not written again for the same */
				alarms_verified = 0;
				idx_known = 1;
				idx_q = idx_contents();
				idx_gen = store_gen;
			}
		}
		if (work_pending() && bg_may_work()) {
			/* steps while the slice has room for one more half as long
			   again as the longest of its kind so far (one can be
			   longer than any before it), and a tick of the 200 Hz
			   clock that times them to spare: one begun at its end
			   would overrun */
			unsigned long s0 = pf_ms(), t;
			int k, n = 0;
			bg_begin();
			do {
				aes_profile_tag("work");
				k = work_kind();
				t = pf_ms();
				work_step();
				t = pf_ms() - t;
#ifdef AES_PROFILE
				if (t >= 30) {
					char b[48];
					snprintf(b, sizeof(b), "STEPMS %lu kind %d\n", t, k);
					nf_debug(b);
				}
#endif
				if (t > step_max[k])
					step_max[k] = t;
				n++;
				/* a kind not timed yet only begins a slice */
				k = work_kind();
			} while (work_pending() && bg_more() && step_max[k] &&
				 pf_ms() - s0 + step_max[k] * 3 / 2 + 5 <= slice_ms);
		}
		if ((which & MU_TIMER) && sync_pending && win_h > 0) {
			sync_pending = 0;
			refresh(0, 0);
		}
		if (which & MU_TIMER) {
			unsigned long now = pf_ms();
			if (now - last_minute >= 30000UL) {
				long t, day = ct_days(now_clock());
				last_minute = now;
				aes_profile_tag("reminder check");
				if (prg_was)		/* ECAL.PRG shows them */
					alarm_checked = now_clock();
				else
					check_alarms();
				if (day != alarm_day) {
					if (alarm_day)
						alarms_changed();	/* a new day: the queue reaches a day further */
					alarm_day = day;
				}
				if (data_ready && win_h > 0) {
					t = ct_days(now_view());
					if (t != shown_today) {
						/* midnight: today moved */
						if (cur_day == shown_today)
							cur_day = t;
						shown_today = t;
						if (win_iconified)
							win_title();	/* its date */
						after_change();
					} else if (cur_view == V_DAY || cur_view == V_WEEK) {
						win_redraw(&r_view);	/* the red "now" line moves */
					}
					auto_refresh();
				}
			}
		}
	}
	win_close();			/* its steps on the info line, if still open */
	settings_flush();
	http_close_all();
	conn_log_flush();
	crash_remove();
	av_exit();
	if (has_menu)
		menu_bar(mb.tree, 0);
	mbar_free(&mb);
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
