/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * datepick.c - the month under a date field (decided 24 Nov 2026, as
 * the Mac's Calendar: a click in the event's Starts or Ends date opens it).
 *
 *   Nov 2026      < o >      the month; back, today, ahead
 *   Mo Tu We Th Fr Sa Su     from the first day of the week given
 *   26 27 28 29 30 31  1     other months' days pale
 *    2  3 ...                today on red, the chosen day on black
 *
 * A click on a day takes it and closes; arrows move the chosen day,
 * Return takes it, Esc or a click outside closes. A key that types (a
 * digit, Backspace...) closes it and goes to the field, so typing a date
 * works as before. Drawn over the dialog with the screen under it kept,
 * as popup.c's menus are.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "dialog.h"

/* ---- what it takes from the app: its text in a few looks and the
   colour that marks today. The rest is gemui.h's (fill, frame, hline,
   cw, ch) and ctime.h's, so the file can move to common/ui as it is
   when a second app wants it; then these become the app's hooks ---- */
enum { DP_PLAIN, DP_BOLD, DP_HINT, DP_PALE, DP_PALE_BOLD, DP_ON_DARK, DP_ON_DARK_BOLD };

static void dp_text(short x, short y, const char *s, short n, int look)
{
	static const short fl[] = { TX_WHITE, TX_BOLD | TX_WHITE, TX_GREY | TX_WHITE, TX_PALE | TX_WHITE,
				    TX_PALE | TX_BOLD, TX_INVERSE, TX_INVERSE | TX_BOLD };
	text_at(x, y, s, n, n, (short)(TX_LTR | fl[look]));
}

static short dp_mark(void)	/* today: the app's red, black in black and white */
{
	return red_color();
}

static GRECT pb;		/* the box */
static long first;		/* the first day shown (top left) */
static int ym;			/* the month shown: year * 12 + month - 1 */
static long sel, today;
static int wstart;		/* the first day of the week: 0 Sunday */

#define DP_COLS 20		/* a day is 3 cells, the last without its space */
#define DP_HEAD (ch + 4)

static short row_y(int r)	/* r: -1 the weekdays, 0-5 the weeks */
{
	return pb.y + 2 + DP_HEAD + (r + 1) * (ch + 2);
}

static short in_x(void)
{
	return pb.x + cw;
}

static void set_month(int m)
{
	int y = m / 12, mo = m % 12 + 1;
	long f = days_from_civil(y, mo, 1);
	ym = m;
	first = f - (wday_of(f) - wstart + 7) % 7;
}

static void head(void)
{
	GRECT r;
	char s[40];
	short x = in_x(), y = pb.y + 3;
	r.x = pb.x + 1;
	r.y = pb.y + 1;
	r.w = pb.w - 2;
	r.h = DP_HEAD;
	fill(&r, 0);
	snprintf(s, sizeof(s), "%s %d", month_short[ym % 12], ym / 12);
	dp_text(x, y, s, (short)strlen(s), DP_BOLD);
	/* back, today (a dot), ahead: the last five cells */
	dp_text(x + (DP_COLS - 5) * cw, y, "\x04", 1, DP_PLAIN);
	dp_text(x + (DP_COLS - 1) * cw, y, "\x03", 1, DP_PLAIN);
	r.w = cw / 2 + 1;
	r.h = r.w;
	r.x = x + (DP_COLS - 3) * cw + (cw - r.w) / 2;
	r.y = y + (ch - r.h) / 2;
	fill(&r, 1);
	hline(pb.x + 3, pb.x + pb.w - 4, pb.y + DP_HEAD + 1);
}

static void days(void)
{
	GRECT r;
	char wd[DP_COLS + 1];
	short k, x0 = in_x();
	int yy, mm, dd, dim, m = ym % 12 + 1;
	r.x = pb.x + 1;
	r.y = row_y(-1);
	r.w = pb.w - 2;
	r.h = pb.y + pb.h - 1 - r.y;
	fill(&r, 0);
	for (k = 0; k < 7; k++) {
		const char *n = wday_short[(k + wstart) % 7];
		wd[k * 3] = n[0];
		wd[k * 3 + 1] = n[1];
		if (k < 6)
			wd[k * 3 + 2] = ' ';
	}
	dp_text(x0, row_y(-1), wd, DP_COLS, DP_HINT);
	civil_from_days(first, &yy, &mm, &dd);
	dim = days_in_month(yy, mm);
	for (k = 0; k < 42; k++) {
		long day = first + k;
		short x = x0 + (k % 7) * 3 * cw, y = row_y(k / 7);
		int look;
		char s[2];
		if (k) {
			if (++dd > dim) {
				dd = 1;
				if (++mm > 12) {
					mm = 1;
					yy++;
				}
				dim = days_in_month(yy, mm);
			}
		}
		if (day == sel && day != today && dp_mark() == 1) {
			/* in black and white today is black too: the chosen day
			   in a ring, so the two tell apart */
			GRECT b;
			b.x = x - 2;
			b.y = y - 1;
			b.w = 2 * cw + 4;
			b.h = ch + 2;
			frame(&b, 1);
			look = mm != m ? DP_PALE_BOLD : DP_BOLD;
		} else if (day == today || day == sel) {
			GRECT b;
			b.x = x - 1;
			b.y = y - 1;
			b.w = 2 * cw + 2;
			b.h = ch + 2;
			fill(&b, day == today ? dp_mark() : 1);
			look = day == today ? DP_ON_DARK_BOLD : DP_ON_DARK;
			if (day == today && day == sel) {
				/* chosen and today: red, in a black ring */
				frame(&b, 1);
			}
		} else {
			look = mm != m ? DP_PALE : DP_PLAIN;
		}
		s[0] = (char)(dd >= 10 ? '0' + dd / 10 : ' ');
		s[1] = (char)('0' + dd % 10);
		dp_text(x, y, s, 2, look);
	}
}

static void draw_box(void)
{
	GRECT s = pb;
	s.x += 2;
	s.y += 2;
	s.w += 1;
	s.h += 1;
	fill(&s, 1);
	fill(&pb, 0);
	frame(&pb, 1);
	head();
	days();
}

/* where (mx, my) is: a day (its number), or 1 back, 2 today, 3 ahead,
   0 in the box elsewhere, -1 outside */
static long hit(short mx, short my, int *what)
{
	short x0 = in_x(), c;
	*what = -1;
	if (mx < pb.x || mx >= pb.x + pb.w || my < pb.y || my >= pb.y + pb.h)
		return 0;
	*what = 0;
	c = (short)((mx - x0) / cw);
	if (my < pb.y + 2 + DP_HEAD) {
		/* the back arrow in cell 15, the dot in 17, the ahead arrow in 19 */
		if (c >= DP_COLS - 6 && c <= DP_COLS - 5)
			*what = 1;
		else if (c >= DP_COLS - 4 && c <= DP_COLS - 2)
			*what = 2;
		else if (c >= DP_COLS - 1)
			*what = 3;
		return 0;
	}
	if (my >= row_y(0) && mx >= x0 - cw / 2 && c < DP_COLS + 1) {
		short r = (short)((my - row_y(0)) / (ch + 2)), col = (short)((mx - x0 + cw / 2) / (3 * cw));
		if (r < 6 && col < 7) {
			*what = 4;
			return first + r * 7 + col;
		}
	}
	return 0;
}

static void show(long day)
{
	int y, m, d;
	civil_from_days(day, &y, &m, &d);
	sel = day;
	if (y * 12 + m - 1 != ym || day < first || day >= first + 42) {
		set_month(y * 12 + m - 1);
		graf_mouse(M_OFF, 0);
		head();
	} else
		graf_mouse(M_OFF, 0);
	days();
	graf_mouse(M_ON, 0);
}

int date_pick(short x, short y, short alt_y, long *day, long now, int weekstart, short *kstate, short *key)
{
	short m[8], mx, my, mb, ks, h, res = 0;
	AEVENT e;
	void *under;
	int yy, mo, dd;

	font_sys_push();
	today = now;
	wstart = weekstart;
	sel = *day;
	civil_from_days(sel, &yy, &mo, &dd);
	set_month(yy * 12 + mo - 1);
	pb.w = (DP_COLS + 2) * cw;
	pb.h = 2 + DP_HEAD + 7 * (ch + 2) + 3;
	h = pb.h + 3;
	if (x + pb.w + 3 > desk_x + desk_w)
		x = desk_x + desk_w - pb.w - 3;
	if (x < desk_x)
		x = desk_x;
	x &= ~7;		/* its text on a byte (the guidelines' Speed) */
	if (y + h > desk_y + desk_h)
		y = alt_y - h > desk_y ? alt_y - h : desk_y + desk_h - h;
	if (y < desk_y)
		y = desk_y;
	pb.x = x;
	pb.y = y;

	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL */
	under = dlg_under(pb.x, pb.y, pb.w + 3, h);
	form_dial(FMD_START, pb.x, pb.y, pb.w + 3, h);
	graf_mouse(M_OFF, 0);
	draw_box();
	graf_mouse(M_ON, 0);
	do
		graf_mkstate(&mx, &my, &mb, &ks);
	while (mb & 3);

	for (;;) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x101, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);
			if (as == 0x1b || sc == 0x61)		/* Esc, Undo */
				break;
			if (sc == 0x1c || sc == 0x72) {		/* Return, Enter */
				*day = sel;
				res = 1;
				break;
			}
			if (sc == 0x4b || sc == 0x4d)		/* a day back, ahead */
				show(sel + (sc == 0x4b ? -1 : 1));
			else if (sc == 0x48 || sc == 0x50)	/* a week */
				show(sel + (sc == 0x48 ? -7 : 7));
			else {
				/* a key that types: to the field, the picker gone */
				*kstate = e.kstate;
				*key = (short)e.kreturn;
				res = 2;
				break;
			}
		}
		if (e.which & MU_BUTTON) {
			int what;
			long d = hit(e.mx, e.my, &what);
			if (what < 0)
				break;			/* outside: closed, nothing taken */
			if (what == 4) {
				*day = d;
				res = 1;
				break;
			}
			if (what == 1 || what == 3) {
				set_month(ym + (what == 1 ? -1 : 1));
				graf_mouse(M_OFF, 0);
				head();
				days();
				graf_mouse(M_ON, 0);
			} else if (what == 2) {
				show(today);
			}
			do
				graf_mkstate(&mx, &my, &mb, &ks);
			while (mb & 3);
		}
	}
	if (under)
		dlg_under_back(under);
	wind_update(2);		/* END_MCTRL */
	wind_update(END_UPDATE);
	do
		graf_mkstate(&mx, &my, &mb, &ks);
	while (mb & 3);
	if (!under)
		form_dial(FMD_FINISH, pb.x, pb.y, pb.w + 3, h);
	font_sys_pop();
	return under ? res : res | 4;
}
