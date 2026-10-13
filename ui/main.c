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
#include "../src/conn.h"
#include "../src/tls.h"
#include "../src/http.h"
#include "../src/plat.h"
#include "../src/util.h"
#include "../src/bidi.h"
#include "../src/version.h"

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id, is_acc, ncolors, hebrew_kbd, quit_now;
extern short _is_acc;		/* crt0.S */

static short menu_id = -1;
static int data_ready;
static char workdir[160];	/* ECal's folder: calendars, keys, certificates, log */
static char inf_file[40];	/* ECAL.INF, in the root of the boot drive */
static unsigned long last_refresh, last_minute;
static CTIME alarm_checked;
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

void settings_flush(void)
{
	if (settings_dirty) {
		settings_dirty = 0;
		store_save_settings();
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
	if (win_h <= 0)
		return;
	if (hebrew_kbd) {
		snprintf(s, sizeof(s), "%s%sHebrew keyboard (F10)", msg, *msg ? "   " : "");
		msg = s;
	}
	win_status(msg);
}

void busy(int on)
{
	graf_mouse(on ? BUSYBEE : ARROW, 0);
}

/* keep the system breathing while we wait for the network */
static void idle(void)
{
	evnt_timer_(5);
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
	if (hebrew_kbd && !(kstate & (K_LSHIFT | K_RSHIFT | K_CTRL | K_ALT)) &&
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
			alert(1, "[3][Falcon mode needs the root certificates: put CACERT.PEM and ROOTS.DAT in the ECAL folder.][ OK ]");
			return 0;
		}
	}
	opt.falcon = (short)on;
	http_close_all();
	falcon_dsp_init();
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
	r = sync_all(force);
	busy(0);
	last_refresh = pf_ms();
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

static void check_alarms(void)
{
	OCCLIST l;
	CTIME now = now_view();
	int i;
	if (!opt.alarms)
		return;
	if (alarm_checked == 0 || now - alarm_checked > 2 * MIN_PER_DAY || now < alarm_checked) {
		alarm_checked = now;		/* first look: nothing from the past */
		return;
	}
	if (now == alarm_checked)
		return;
	memset(&l, 0, sizeof(l));
	occ_collect(&l, now - MIN_PER_DAY, now + 8 * MIN_PER_DAY);
	for (i = 0; i < l.n; i++) {
		EVENT *e = l.o[i].ev;
		CTIME when;
		if (e->alarm == -1 || !cals[e->cal].alarms)
			continue;
		when = l.o[i].start - e->alarm;
		if (when > alarm_checked && when <= now) {
			char t[16], line[100], title[70];
			DT d;
			long days = ct_days(l.o[i].start) - ct_days(now);
			short b;
			ct_split(l.o[i].start, &d);
			fmt_time(d.hour, d.min, opt.h24, t, sizeof(t));
			str_copy(title, e->summary ? e->summary : "Event", sizeof(title));
			if (e->allday)
				snprintf(line, sizeof(line), "%s", days == 0 ? "today" : days == 1 ? "tomorrow" : "all day");
			else if (days == 0)
				snprintf(line, sizeof(line), "today at %s", t);
			else if (days == 1)
				snprintf(line, sizeof(line), "tomorrow at %s", t);
			else
				snprintf(line, sizeof(line), "%s %d %s, %s", wday_short[d.wday], d.day, month_short[d.mon - 1], t);
			Bconout(2, 7);
			b = alert(1, "[1][%s|%s%s%s][ OK |Show]", title, line, e->location ? "|" : "",
				  e->location ? e->location : "");
			if (b == 2) {
				if (win_h <= 0)
					win_open();
				views_goto(ct_days(l.o[i].start), V_DAY);
			}
		}
	}
	occ_free(&l);
	alarm_checked = now;
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

void cmd_import(int cal)
{
	char path[200], name[20] = "", dir[200];
	short button = 0;
	int n;
	snprintf(dir, sizeof(dir), "%s\\*.ICS", workdir);
	if (!fsel_exinput(dir, name, &button, "Import which .ics file?") || !button || !name[0])
		return;
	{
		char *slash = strrchr(dir, '\\');
		if (slash)
			slash[1] = 0;
		snprintf(path, sizeof(path), "%s%s", dir, name);
	}
	busy(1);
	if (!cal_import(cal, path, &n)) {
		busy(0);
		alert(1, "[1][Import stopped: %s|(%d events imported)][ OK ]", cal_err, n);
	} else {
		busy(0);
		alert(1, "[1][%d events imported into|%s.][ OK ]", n, cals[cal].name);
	}
	after_change();
}

void cmd_menu(short x, short y)
{
	enum { M_ADD, M_NEW, M_S1, M_GOTO, M_FIND, M_TODAY, M_S2, M_SIDE, M_HEB, M_KBD, M_S5,
	       M_OFFL, M_FALC, M_LOG, M_S3, M_SET, M_FONT, M_KEYS, M_ABOUT, M_S4, M_QUIT, M_N };
	const char *items[M_N];
	short k, n = is_acc ? M_S4 : M_N;
	items[M_ADD] = "  Add account or calendar...";
	items[M_NEW] = "  New event            N";
	items[M_S1] = "-";
	items[M_GOTO] = "  Go to date...       ^G";
	items[M_FIND] = "  Find...             ^F";
	items[M_TODAY] = "  Today                T";
	items[M_S2] = "-";
	items[M_SIDE] = opt.sidebar ? "\x08 Calendar list       ^S" : "  Calendar list       ^S";
	items[M_HEB] = opt.hebdates ? "\x08 Hebrew dates" : "  Hebrew dates";
	items[M_KBD] = hebrew_kbd ? "\x08 Hebrew keyboard     F10" : "  Hebrew keyboard     F10";
	items[M_S5] = "-";
	/* as EMail's Options menu */
	items[M_OFFL] = opt.offline ? "\x08 Work offline" : "  Work offline";
	items[M_FALC] = opt.falcon ? "\x08 Falcon mode (TLS)" : "  Falcon mode (TLS)";
	items[M_LOG] = opt.log ? "\x08 Write a log" : "  Write a log";
	items[M_S3] = "-";
	items[M_SET] = "  Settings...";
	items[M_FONT] = "  Font...";
	items[M_KEYS] = "  Keys...            Help";
	items[M_ABOUT] = "  About ECal...";
	items[M_S4] = "-";
	items[M_QUIT] = "  Quit                ^Q";
	k = win_popup(x, y, items, n);
	switch (k) {
	case M_ADD: dlg_add_account(); break;
	case M_NEW: cmd_new_event(); break;
	case M_GOTO: dlg_goto(); break;
	case M_FIND: dlg_find(); break;
	case M_TODAY: views_today(); break;
	case M_SIDE:
		opt.sidebar = !opt.sidebar;
		settings_later();
		win_layout();
		win_redraw(0);
		break;
	case M_HEB:
		opt.hebdates = !opt.hebdates;
		settings_later();
		after_change();
		break;
	case M_KBD: set_hebrew_kbd(!hebrew_kbd); break;
	case M_OFFL:
		opt.offline = !opt.offline;
		if (opt.offline)
			http_close_all();
		store_save_settings();
		updated_status();
		break;
	case M_FALC:
		if (falcon_mode_set(!opt.falcon)) {
			http_close_all();
			store_save_settings();
		}
		break;
	case M_LOG:
		opt.log = !opt.log;
		set_logging();
		store_save_settings();
		break;
	case M_SET: dlg_settings(); break;
	case M_FONT: font_menu(); break;
	case M_KEYS: dlg_keys(); break;
	case M_ABOUT: dlg_about(); break;
	case M_QUIT:
		quit_now = 1;
		win_close();
		break;
	}
}

/* ---------------- start-up ---------------- */

/* ECAL.INF lives in the root of the boot drive, where an accessory can
   always find it; FOLDER= in it says where the rest of ECal's files are.
   The program writes the folder it was started from; the accessory takes
   it from there, and asks when it is missing (locate_folder). */

/* FOLDER= from a settings file, "" when it has none */
static void inf_folder(const char *inf, char *out, int size)
{
	/* read without malloc: the accessory calls this before its heap is
	   taken. ECAL.PRG and ECAL.ACC write FOLDER= near the top */
	static char t[4096];
	char *k;
	int h = pf_open(inf, PF_READ);
	long n;
	out[0] = 0;
	if (h < 0)
		return;
	n = pf_read(h, t, sizeof(t) - 1);
	pf_close(h);
	t[n > 0 ? n : 0] = 0;
	for (k = t; (k = (char *)str_istr(k, "folder=")); k += 7)
		if (k == t || k[-1] == '\n') {
			char *e = k + 7 + strcspn(k + 7, "\r\n");
			*e = 0;
			str_copy(out, k + 7, size);
			break;
		}
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
		inf_folder(inf_file, workdir, sizeof(workdir));
		if (!pf_exists(inf_file)) {
			/* settings from before ECAL.INF moved to the root */
			snprintf(workdir, sizeof(workdir), "%c:\\ECAL", 'A' + boot);
			path_join(old, sizeof(old), workdir, "ECAL.INF");
			if (!pf_exists(old))
				workdir[0] = 0;
		}
		if (workdir[0] && !pf_exists(workdir))
			workdir[0] = 0;		/* gone: ask at the first opening */
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
	if (pf_exists(inf_file) || !workdir[0])
		return;
	path_join(old, sizeof(old), workdir, "ECAL.INF");
	if (!strcasecmp(old, inf_file) || !(t = pf_load(old, &len)))
		return;
	pf_save(inf_file, t, len);
	free(t);
}

/* the accessory without its folder: the user shows it where it is */
static void locate_folder(void)
{
	char path[200], name[20] = "", why[200];
	short button = 0, boot = boot_drive();
	inf_folder(inf_file, why, sizeof(why));
	for (;;) {
		short k = why[0] ? alert(1, "[2][ECal's folder is not at|%s.|Show me where it is?][Locate...|%c:\\ECAL]",
					 why, 'A' + boot)
				 : alert(1, "[2][Where is ECal's folder, with|its calendars, keys and|certificates?][Locate...|%c:\\ECAL]",
					 'A' + boot);
		char *e;
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
			return;
		}
	}
	snprintf(workdir, sizeof(workdir), "%c:\\ECAL", 'A' + boot);
	pf_mkdir(workdir);
}

/* an accessory takes its memory once, now: later it would belong to
   whichever program is in front. HEAPKB=n in ECAL.INF overrides */
static void acc_heap(void)
{
	static char buf[2048];
	char inf[200];
	long avail = (long)Malloc(-1L), alt = (long)Mxalloc(-1L, 1), want, h, n;
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
	str_copy(inf, inf_file, sizeof(inf));
	h = Fopen(inf, 0);
	if (h >= 0) {
		n = Fread((short)h, sizeof(buf) - 1, buf);
		Fclose((short)h);
		if (n > 0) {
			char *k;
			buf[n] = 0;
			k = (char *)str_istr(buf, "heapkb=");
			if (k && atol(k + 7) >= 64)
				want = atol(k + 7) * 1024;
		}
	}
	if (want > avail / 2)
		want = avail / 2;
	if (want < 96L * 1024)
		want = avail - 32L * 1024;
	if (want < 32L * 1024)
		return;
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
	if ((long)p > 0)
		heap_add(p, (size_t)want);
}

/* the settings, at the first opening: quick, so the window can open at once */
static int settings_ready;
static int sync_pending;	/* the first refresh, once the window is drawn */

static void load_settings(void)
{
	if (!workdir[0])
		locate_folder();
	move_inf();
	store_init(workdir, inf_file);
	if (strcasecmp(opt.folder, workdir))
		store_save_settings();	/* FOLDER= as it is now */
	path_join(conn_cacert, sizeof(conn_cacert), workdir, "CACERT.PEM");
	path_join(tls_roots_cache, sizeof(tls_roots_cache), workdir, "ROOTS.DAT");
	path_join(tls_seed_path, sizeof(tls_seed_path), workdir, "SEED.DAT");
	tls_tz_minutes = tz_offset(home_zone(), now_utc());
	set_logging();
	net_init();
	pf_idle = idle;
	cal_status = status;
	tls_note = status;
	falcon_dsp_init();
	cur_view = opt.view >= V_DAY && opt.view <= V_YEAR ? opt.view : V_MONTH;
	cur_day = ct_days(now_view());
	shown_today = cur_day;
	day_scroll = opt.daystart;
	/* passwords typed into ECAL.INF by hand: encrypt them right away */
	if (store_plain_passwords && !store_lost_passwords)
		store_save_settings();
	hebrew_kbd = opt.hebrew;
	font_apply();
	{
		/* where TOS put us: TT/Fast RAM starts at 16 MB */
		char m[100];
		void *h = malloc(16);
		snprintf(m, sizeof(m), "ECal %s: program at %08lx (%s RAM), memory at %08lx (%s RAM)",
			 cal_version, (unsigned long)load_settings,
			 (unsigned long)load_settings >= 0x01000000UL ? "TT" : "ST",
			 (unsigned long)h, (unsigned long)h >= 0x01000000UL ? "TT" : "ST");
		free(h);
		conn_log("ECal", "--", m, (long)strlen(m));
		conn_log_flush();
	}
	settings_ready = 1;
}

/* the calendars from the copies on disk, with the window already open */
static int bg_loaded;		/* calendars read so far (the accessory, after boot) */

static void load_calendars(void)
{
	busy(1);
	status("Reading your calendars...");
	for (; bg_loaded < ncals; bg_loaded++)
		cal_load(&cals[bg_loaded]);
	status("");
	views_invalidate();
	busy(0);
	data_ready = 1;
	if (store_lost_passwords)
		alert(1, "[1][ECAL.KEY is missing or new: type the passwords again (right-click an account).][ OK ]");
}

/* First the window, then today from the copies on disk, and only then,
   from the event loop once everything is drawn, the refresh */
static void open_window(void)
{
	int first = win_h <= 0;
	if (!settings_ready)
		load_settings();
	win_open();
	if (win_h <= 0 || !first)
		return;
	if (!data_ready) {
		win_redraw(0);		/* the window, before the disk is read */
		load_calendars();
	}
	after_change();			/* today's events, from what is kept here */
	updated_status();
	if (!naccts) {
		alert(1, "[1][Welcome to ECal!||Let's add your iCloud or other calendar.][ OK ]");
		dlg_add_account();
	} else if (opt.sync_open && !opt.offline) {
		sync_pending = 1;	/* as EMail's first check: problems in an alert */
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

/* The accessory, its window closed. A few seconds after boot it reads
   the calendars kept on disk, one per timer tick so the program in front
   keeps going between them, and so it can remind of events without
   having been opened. With BGSYNC it also refreshes them when they are
   due, again one calendar per tick, quietly. */
static unsigned long bg_start;
static int bg_next = -1;		/* the next calendar to refresh, -1 none */

static void background(void)
{
	unsigned long now = pf_ms();
	if (!is_acc || win_h > 0)
		return;
	if (!data_ready) {
		if (now - bg_start < 8000UL)
			return;		/* let the desktop come up first */
		if (!settings_ready) {
			if (!workdir[0])
				return;	/* the folder is asked for at the first opening */
			load_settings();
			return;
		}
		if (bg_loaded < ncals) {
			cal_load(&cals[bg_loaded++]);
			return;
		}
		views_invalidate();
		data_ready = 1;
		return;
	}
	if (!opt.bgsync || opt.offline || !naccts)
		return;
	if (bg_next < 0) {
		int i, due = opt.refresh > 0 && now - last_refresh > (unsigned long)opt.refresh * 60000UL;
		for (i = 0; i < ncals && !due; i++) {
			ACCOUNT *a = &accts[cals[i].acct];
			due = a->refresh > 0 && cals[i].synced && now_utc() - cals[i].synced >= a->refresh &&
			      now - last_refresh > 60000UL;
		}
		if (due)
			bg_next = 0;
		return;
	}
	if (bg_next < ncals) {
		/* errors stay in the account (shown at the next opening) */
		sync_calendar(&cals[bg_next++], 0);
		return;
	}
	bg_next = -1;
	last_refresh = now;
	http_close_all();
	conn_log_flush();
	settings_later();
	views_invalidate();
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

int main(void)
{
	short work_out[57], msg[8], d;
	AEVENT ev;

	is_acc = _is_acc;
	{
		extern int mem_unaligned_ok;
		mem_unaligned_ok = get_cookie(0x5F435055L) >= 20;	/* _CPU */
	}
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
	bg_start = pf_ms();
	if (is_acc)
		menu_id = menu_register(ap_id, "  ECalendar");
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
	find_workdir();
	st_ram_init();
	dlg_after = win_frame_refresh;
	if (is_acc)
		acc_heap();
	else
		open_window();

	while (!quit_now) {
		short flags = MU_MESAG | MU_TIMER, which, hovering;
		unsigned long wait = sync_pending ? 150 : 1000;
		if (win_h > 0)
			flags |= MU_KEYBD | MU_BUTTON | MU_M1;
		/* the mouse moving away from where it was last seen */
		evnt_set_m1(1, hover_x, hover_y, 1, 1);
		hovering = win_h > 0 && !tip_tried && hover_x >= 0;
		if (hovering && wait > 100)
			wait = 100;
		/* a click comes at once; where a double click means something,
		   click_count() waits for the second one before anything is done */
		/* a refresh waiting: shortly, once the redraws are done */
		which = evnt_multi_(flags, is_acc ? 0x102 : 0x101, 3, 0, wait, msg, &ev);
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
			switch (msg[0]) {
			case AC_OPEN:
				if (msg[4] == menu_id)
					open_window();
				break;
			case AC_CLOSE:
				/* the program in front ended: the AES took our window */
				if (msg[3] == menu_id) {
					win_h = -1;
					settings_flush();
				}
				break;
			case AP_TERM:
				if (!is_acc)
					quit_now = 1;
				win_close();
				break;
			default:
				win_message(msg);
			}
		}
		if ((which & MU_KEYBD) && win_h > 0) {
			short scan = KEY_SCAN(ev.kreturn);
			if (scan == 0x44)
				set_hebrew_kbd(!hebrew_kbd);
			else if (scan == 0x62)
				dlg_keys();
			else
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
		if ((which & MU_TIMER) && settings_dirty && pf_ms() - settings_at >= 3000UL)
			settings_flush();
		if (which & MU_TIMER)
			background();
		if ((which & MU_TIMER) && sync_pending && win_h > 0) {
			sync_pending = 0;
			refresh(0, 0);
		}
		if ((which & MU_TIMER) && data_ready) {
			unsigned long now = pf_ms();
			long t = ct_days(now_view());
			if (now - last_minute >= 30000UL) {
				last_minute = now;
				check_alarms();
				if (win_h > 0) {
					if (t != shown_today) {
						/* midnight: today moved */
						if (cur_day == shown_today)
							cur_day = t;
						shown_today = t;
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
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
