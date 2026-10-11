/*
 * sidebar.c - the calendar list on the left, grouped by account as on
 * the Mac (a tick box in each calendar's colour shows or hides it), and
 * a small month below it to jump around in.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"

typedef struct {
	short y;
	short acct;		/* account header: its index */
	short cal;		/* calendar row: its index, else -1 */
} SROW;
#define MAXROWS (MAXACCT + MAXCAL + 2)
static SROW srow[MAXROWS];
static short nrows, row_h;
static GRECT mini;		/* the small month */
static long mini_first;		/* the day in its first cell */

static void side_layout(void)
{
	short y = r_side.y + ch / 2, a, c;
	row_h = ch + 2;
	nrows = 0;
	for (a = 0; a < naccts && nrows < MAXROWS; a++) {
		srow[nrows].y = y;
		srow[nrows].acct = a;
		srow[nrows].cal = -1;
		nrows++;
		y += row_h;
		for (c = 0; c < ncals && nrows < MAXROWS; c++) {
			if (cals[c].acct != a)
				continue;
			srow[nrows].y = y;
			srow[nrows].acct = a;
			srow[nrows].cal = c;
			nrows++;
			y += row_h;
		}
		y += ch / 2;
	}
	/* the small month at the bottom, if there is room */
	mini.w = 21 * cw;
	mini.h = 8 * ch + 4;
	mini.x = r_side.x + (r_side.w - mini.w) / 2;
	mini.y = r_side.y + r_side.h - mini.h - ch / 2;
	if (mini.y < y || r_side.w < mini.w)
		mini.h = 0;
}

static void draw_mini(void)
{
	int y, m, d;
	long first, t = ct_days(now_view());
	short k;
	char s[40];
	civil_from_days(cur_day, &y, &m, &d);
	first = days_from_civil(y, m, 1);
	mini_first = first - (wday_of(first) - opt.weekstart + 7) % 7;
	snprintf(s, sizeof(s), "%s %d", month_names[m - 1], y);
	text_at(mini.x, mini.y, s, (long)strlen(s), 21, TX_BOLD | TX_LTR | TX_CENTER);
	for (k = 0; k < 7; k++)
		text_at(mini.x + k * 3 * cw, mini.y + ch + 2, wday_short[(k + opt.weekstart) % 7], 2, 2, TX_LTR | TX_LIGHT);
	{
		/* runs of same-looking days in one piece of text (see views.c) */
		char run[24];
		short rl = 0, rx = 0, ry = 0, rflags = -1;
		for (k = 0; k <= 42; k++) {
			long day = mini_first + k;
			int yy, mm, dd;
			short x = mini.x + (k % 7) * 3 * cw, yp = mini.y + (k / 7 + 2) * ch + 4, flags = 0;
			if (k < 42) {
				civil_from_days(day, &yy, &mm, &dd);
				if (day == t || day == cur_day) {
					GRECT b;
					b.x = x - 1;
					b.y = yp;
					b.w = 2 * cw + 2;
					b.h = ch;
					fill(&b, day == t ? red_color() : 1);
					flags = TX_INVERSE | (day == t ? TX_BOLD : 0);
				} else {
					flags = mm != m ? TX_LIGHT : 0;
				}
			}
			if (rl && (k == 42 || flags != rflags || k % 7 == 0 || (rflags & TX_INVERSE))) {
				text_at(rx, ry, run, rl, rl, TX_LTR | rflags);
				rl = 0;
			}
			if (k == 42)
				break;
			if (!rl) {
				rx = x;
				ry = yp;
				rflags = flags;
			} else {
				run[rl++] = ' ';
			}
			run[rl++] = (char)(dd >= 10 ? '0' + dd / 10 : ' ');
			run[rl++] = (char)('0' + dd % 10);
		}
	}
}

void side_draw(GRECT *clip)
{
	GRECT r = r_side;
	short i, cols = r_side.w / cw - 1;
	if (!rect_and(&r, clip))
		return;
	side_layout();
	clip_on(&r);
	fill(&r, 0);
	if (!naccts) {
		text_at(r_side.x + cw / 2, r_side.y + ch, "No calendars yet:", 17, cols, TX_LTR | TX_LIGHT);
		text_at(r_side.x + cw / 2, r_side.y + 2 * ch, "\xF0 > Add account...", 18, cols, TX_LTR | TX_LIGHT);
	}
	for (i = 0; i < nrows; i++) {
		SROW *s = &srow[i];
		if (s->cal < 0) {
			ACCOUNT *a = &accts[s->acct];
			text_at(r_side.x + cw / 2, s->y, a->name, (long)strlen(a->name), cols - 1, TX_BOLD | TX_RIGHT | TX_LIGHT);
			if (a->err[0])
				text_at(r_side.x + r_side.w - 2 * cw, s->y, "!", 1, 1, TX_BOLD | TX_LTR);
		} else {
			CALENDAR *c = &cals[s->cal];
			PEN p;
			GRECT box;
			pen_of(s->cal, &p);
			box.x = r_side.x + cw;
			box.y = s->y + 2;
			box.w = cw + 2;
			box.h = ch - 3;
			if (c->visible) {
				fill(&box, p.color);
			} else {
				fill(&box, 0);
				frame(&box, p.color);
			}
			text_at(r_side.x + 3 * cw, s->y, c->name, (long)strlen(c->name), cols - 3,
				TX_RIGHT | (c->visible ? 0 : TX_LIGHT));
		}
	}
	if (mini.h)
		draw_mini();
}

static void cal_menu(short mx, short my, int c)
{
	const char *items[8];
	short act[8], n = 0, k;
	int kind = accts[cals[c].acct].kind;
	items[n] = "  Show only this calendar";
	act[n++] = 1;
	items[n] = "  Show all calendars";
	act[n++] = 2;
	items[n] = "-";
	act[n++] = 0;
	items[n] = "  Get info...";
	act[n++] = 3;
	if (kind != AK_LOCAL) {
		items[n] = "  Refresh";
		act[n++] = 4;
	}
	if (cal_writable(&cals[c])) {
		items[n] = "  Import .ics file...";
		act[n++] = 5;
	}
	if (kind != AK_CALDAV) {
		items[n] = kind == AK_SUBSCR ? "  Unsubscribe..." : "  Delete calendar...";
		act[n++] = 6;
	}
	k = popup(mx, my, items, n);
	if (k < 0)
		return;
	switch (act[k]) {
	case 1: {
		int i;
		for (i = 0; i < ncals; i++)
			cals[i].visible = i == c;
		break;
	}
	case 2: {
		int i;
		for (i = 0; i < ncals; i++)
			cals[i].visible = 1;
		break;
	}
	case 3: dlg_calendar(c); break;
	case 4:
		busy(1);
		if (sync_calendar(&cals[c], 1) < 0)
			alert(1, "[1][%s][ OK ]", cal_err);
		busy(0);
		break;
	case 5: cmd_import(c); break;
	case 6:
		if (alert(2, "[2][%s \"%s\"?][%s|Cancel]", kind == AK_SUBSCR ? "Unsubscribe from" : "Delete the calendar",
			  cals[c].name, kind == AK_SUBSCR ? "Unsubscribe" : "Delete") == 1) {
			int a = cals[c].acct, i, left = 0;
			views_select_none();
			cal_delete(c);
			for (i = 0; i < ncals; i++)
				left += cals[i].acct == a;
			if (!left && accts[a].kind == AK_SUBSCR)
				acct_delete(a);
		}
		break;
	}
	store_save_settings();
	after_change();
}

static void acct_menu(short mx, short my, int a)
{
	static const char *const items[] = { "  Refresh", "  Account settings...", "-", "  Delete account..." };
	static const char *const local_items[] = { "  New calendar...", "-", "  Delete account..." };
	short k;
	if (accts[a].kind == AK_LOCAL) {
		k = popup(mx, my, local_items, 3);
		if (k == 0) {
			char name[40] = "";
			if (dlg_ask("New calendar", "Name:", name, 30) && name[0]) {
				cal_add(a, name);
				store_save_settings();
				after_change();
			}
		} else if (k == 2) {
			k = 3;
		} else {
			return;
		}
	} else {
		k = popup(mx, my, items, 4);
	}
	switch (k) {
	case 0:
		busy(1);
		if (sync_account(a, 1) < 0)
			alert(1, "[1][%s][ OK ]", cal_err);
		busy(0);
		after_change();
		break;
	case 1:
		dlg_account(a);
		after_change();
		break;
	case 3:
		if (alert(2, "[2][Delete the account \"%s\"|and its calendars from this|Atari? (Nothing is deleted|on the server.)][Delete|Cancel]",
			  accts[a].name) == 1) {
			views_select_none();
			acct_delete(a);
			store_save_settings();
			after_change();
		}
		break;
	}
}

int side_click(short mx, short my, short clicks, short button)
{
	short i;
	side_layout();
	if (mini.h && rect_hit(&mini, mx, my)) {
		short k = (my - mini.y - 2 * ch - 4) / ch * 7 + (mx - mini.x) / (3 * cw);
		if (my >= mini.y + 2 * ch + 4 && k >= 0 && k < 42) {
			views_select_none();
			views_goto(mini_first + k, clicks == 2 ? V_DAY : cur_view);
		} else if (my < mini.y + ch) {
			views_goto(cur_day, V_MONTH);
		}
		return 1;
	}
	for (i = 0; i < nrows; i++) {
		SROW *s = &srow[i];
		if (my < s->y || my >= s->y + row_h)
			continue;
		if (s->cal < 0) {
			if (button & 2)
				acct_menu(mx, my, s->acct);
			else if (clicks == 2 && accts[s->acct].kind != AK_LOCAL) {
				dlg_account(s->acct);
				after_change();
			}
			return 1;
		}
		if (button & 2) {
			cal_menu(mx, my, s->cal);
			return 1;
		}
		if (clicks == 2) {
			dlg_calendar(s->cal);
			after_change();
			return 1;
		}
		cals[s->cal].visible = !cals[s->cal].visible;
		if (sel_occ.ev && sel_occ.ev->cal == s->cal)
			views_select_none();
		store_save_settings();
		views_invalidate();
		win_redraw(0);
		return 1;
	}
	return 1;
}
