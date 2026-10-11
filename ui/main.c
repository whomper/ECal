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

short vdi_h, cw, ch, scr_w, scr_h, desk_x, desk_y, desk_w, desk_h, planes;
short gl_wchar, gl_hchar, ap_id, is_acc, ncolors, hebrew_kbd, quit_now;
extern short _is_acc;		/* crt0.S */

static short menu_id = -1;
static int data_ready;
static char workdir[160];
static unsigned long last_refresh, last_minute;
static CTIME alarm_checked;
static long shown_today = -1;

/* ---------------- small things ---------------- */

void status(const char *msg)
{
	if (win_h > 0)
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

void set_hebrew_kbd(short on)
{
	hebrew_kbd = on;
	status(on ? "Hebrew keyboard (F10 to switch back)" : "");
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
	snprintf(s, sizeof(s), "Updated %02d:%02d   %s %s", d.hour, d.min, tz_city(view_zone()), lab);
	status(s);
}

static void refresh(int force, int quiet)
{
	int r;
	if (!naccts)
		return;
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
	if (!naccts)
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
	enum { M_ADD, M_NEW, M_S1, M_GOTO, M_FIND, M_TODAY, M_S2, M_SIDE, M_HEB, M_KBD, M_S3,
	       M_SET, M_KEYS, M_ABOUT, M_S4, M_QUIT, M_N };
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
	items[M_S3] = "-";
	items[M_SET] = "  Settings...";
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
		store_save_settings();
		win_layout();
		win_redraw(0);
		break;
	case M_HEB:
		opt.hebdates = !opt.hebdates;
		store_save_settings();
		after_change();
		break;
	case M_KBD: set_hebrew_kbd(!hebrew_kbd); break;
	case M_SET: dlg_settings(); break;
	case M_KEYS: dlg_keys(); break;
	case M_ABOUT: dlg_about(); break;
	case M_QUIT:
		win_close();
		quit_now = 1;
		break;
	}
}

/* ---------------- start-up ---------------- */

/* where our files are: next to the program, or X:\ECAL on the boot drive */
static void find_workdir(void)
{
	char path[160];
	short drv = (short)Dgetdrv();
	if (is_acc) {
		snprintf(workdir, sizeof(workdir), "%c:\\ECAL", 'A' + drv);
		pf_mkdir(workdir);
		return;
	}
	path[0] = 0;
	Dgetpath(path, 0);
	snprintf(workdir, sizeof(workdir), "%c:%s", 'A' + drv, path);
	if (workdir[strlen(workdir) - 1] == '\\')
		workdir[strlen(workdir) - 1] = 0;
}

/* an accessory takes its memory once, now: later it would belong to
   whichever program is in front. HEAPKB=n in ECAL.INF overrides */
static void acc_heap(void)
{
	static char buf[2048];
	char inf[200];
	long avail = (long)Malloc(-1L), want, h, n;
	void *p;
	want = tls_available && get_cookie(0x5F435055L) >= 30 ? 1536L * 1024 : 256L * 1024;
	snprintf(inf, sizeof(inf), "%s\\ECAL.INF", workdir);
	h = Fopen(inf, 0);
	if (h >= 0) {
		n = Fread((short)h, sizeof(buf) - 1, buf);
		Fclose((short)h);
		if (n > 0) {
			char *k;
			buf[n] = 0;
			k = strstr(buf, "heapkb=");
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
	p = Mxalloc(want, 3);
	if ((long)p == -32 || (long)p <= 0)
		p = Malloc(want);
	if ((long)p > 0)
		heap_add(p, (size_t)want);
}

static void load_data(void)
{
	char p[200];
	busy(1);
	store_init(workdir);
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
	cal_load_all();
	cur_view = opt.view >= V_DAY && opt.view <= V_YEAR ? opt.view : V_MONTH;
	cur_day = ct_days(now_view());
	shown_today = cur_day;
	day_scroll = opt.daystart;
	if (store_lost_passwords)
		alert(1, "[1][ECAL.KEY is missing or new: type the passwords again (right-click an account).][ OK ]");
	busy(0);
	data_ready = 1;
	(void)p;
}

static void open_window(void)
{
	int first = win_h <= 0;
	if (!data_ready)
		load_data();
	win_open();
	if (win_h <= 0 || !first)
		return;
	updated_status();
	if (!naccts) {
		alert(1, "[1][Welcome to ECal!||Let's add your iCloud or other calendar.][ OK ]");
		dlg_add_account();
	} else if (opt.sync_open) {
		refresh(0, 1);
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

int main(void)
{
	short work_out[57], msg[8], d;
	AEVENT ev;

	is_acc = _is_acc;
	ap_id = appl_init();
	if (ap_id < 0)
		return 1;
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
	find_workdir();
	dlg_after = win_frame_refresh;
	if (is_acc)
		acc_heap();
	else
		open_window();

	evnt_set_m1(1, 0, 0, 1, 1);
	while (!quit_now) {
		short flags = MU_MESAG | MU_TIMER, which;
		if (win_h > 0)
			flags |= MU_KEYBD | MU_BUTTON;
		/* GEM counts the clicks (as in EMail and our dialogs): timing them
		   here missed double clicks whenever the first one's redraw was slow */
		which = evnt_multi_(flags, 0x102, 3, 0, 1000, msg, &ev);
		if (which & MU_MESAG) {
			switch (msg[0]) {
			case AC_OPEN:
				if (msg[4] == menu_id)
					open_window();
				break;
			case AC_CLOSE:
				/* the program in front ended: the AES took our window */
				if (msg[3] == menu_id)
					win_h = -1;
				break;
			case AP_TERM:
				if (is_acc)
					win_close();
				else
					quit_now = 1;
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
				else
					win_mouse(ev.mx, ev.my, ev.mbutton, ev.breturn >= 2 ? 2 : 1, ev.kstate);
			}
			wait_release();
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
	http_close_all();
	conn_log_flush();
	store_save_settings();
	win_close();
	v_clsvwk(vdi_h);
	appl_exit();
	return 0;
}
