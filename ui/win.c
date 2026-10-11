/*
 * win.c - ECal's window: the toolbar on top (back, Today, forward,
 * the period's name, Day/Week/Month/Year, new event, refresh, menu),
 * the calendar list on the left, the view on the right, the status in
 * the info line.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"

short win_h = -1;
GRECT work, r_tool, r_side, r_view;

#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | INFO | UPARROW | DNARROW | VSLIDE)

static char title[80], info[140];

enum { B_PREV, B_TODAY, B_NEXT, B_DAY, B_WEEK, B_MONTH, B_YEAR, B_NEW, B_REFRESH, B_MENU, NBUT };
static GRECT but[NBUT];
static const char *const blabel[NBUT] = {
	"\x04", "Today", "\x03", "Day", "Week", "Month", "Year", "+", "Refresh", "\xF0"
};
static GRECT r_title;

void win_title(void)
{
	snprintf(title, sizeof(title), " ECal ");
	if (win_h > 0)
		wind_set_str(win_h, WF_NAME, title);
}

/* after a dialog: the AES doesn't always draw the window's frame again
   where a dialog stood over it */
void win_frame_refresh(void)
{
	if (win_h > 0) {
		wind_set_str(win_h, WF_NAME, title);
		wind_set_str(win_h, WF_INFO, info);
	}
}

void win_status(const char *s)
{
	snprintf(info, sizeof(info), " %s", s);
	if (win_h > 0)
		wind_set_str(win_h, WF_INFO, info);
}

void win_layout(void)
{
	short th = ch + 8, sw, x, i, bw;
	if (win_h <= 0)
		return;
	wind_get(win_h, WF_WORKXYWH, &work.x, &work.y, &work.w, &work.h);
	r_tool = work;
	r_tool.h = th;
	sw = opt.sidebar ? 22 * cw : 0;
	if (sw > work.w / 3)
		sw = work.w / 3;
	r_side.x = work.x;
	r_side.y = work.y + th;
	r_side.w = sw;
	r_side.h = work.h - th;
	r_view.x = work.x + sw;
	r_view.y = work.y + th;
	r_view.w = work.w - sw;
	r_view.h = work.h - th;
	/* buttons: back Today forward on the left, the views and the rest on the right */
	x = work.x + 4;
	for (i = B_PREV; i <= B_NEXT; i++) {
		bw = ((short)strlen(blabel[i]) + 2) * cw;
		but[i].x = x;
		but[i].y = work.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x += bw + (i == B_NEXT ? cw : 2);
	}
	r_title.x = x;
	r_title.y = work.y + 4;
	r_title.h = ch;
	x = work.x + work.w - 4;
	for (i = B_MENU; i >= B_DAY; i--) {
		bw = ((short)strlen(blabel[i]) + 2) * cw;
		if (work.w < 70 * cw && i >= B_DAY && i <= B_YEAR)
			bw = 3 * cw;		/* narrow: "D", "W", "M", "Y" */
		x -= bw;
		but[i].x = x;
		but[i].y = work.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x -= (i == B_DAY || i == B_NEW || i == B_MENU) ? cw : 0;
	}
	r_title.w = but[B_DAY].x - cw - r_title.x;
	views_invalidate();
}

void win_sliders(void)
{
	long vis, size, pos;
	if (win_h <= 0)
		return;
	if (cur_view == V_DAY || cur_view == V_WEEK) {
		vis = views_hours_visible();
		if (vis > 24)
			vis = 24;
		size = vis * 1000 / 24;
		pos = vis >= 24 ? 0 : (long)day_scroll * 1000 / (24 - vis);
	} else {
		size = 1000;
		pos = 0;
	}
	wind_set(win_h, WF_VSLSIZE, (short)(size < 30 ? 30 : size), 0, 0, 0);
	wind_set(win_h, WF_VSLIDE, (short)(pos > 1000 ? 1000 : pos), 0, 0, 0);
}

static void draw_button(int i)
{
	GRECT r = but[i];
	short on = (i >= B_DAY && i <= B_YEAR && cur_view == i - B_DAY);
	const char *l = blabel[i];
	short n = (short)strlen(l);
	char one[2];
	if (r.w == 3 * cw && i >= B_DAY && i <= B_YEAR) {
		one[0] = l[0];
		one[1] = 0;
		l = one;
		n = 1;
	}
	fill(&r, on ? 1 : 0);
	frame(&r, 1);
	text_at(r.x + (r.w - n * cw) / 2, r.y + 1, l, n, n, TX_LTR | (on ? TX_INVERSE : 0) |
		(i == B_TODAY ? TX_BOLD : 0));
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	char t[60];
	int i;
	if (!rect_and(&r, clip))
		return;
	fill(&r, 0);
	for (i = 0; i < NBUT; i++)
		draw_button(i);
	views_title(t, sizeof(t));
	if ((short)strlen(t) > r_title.w / cw && (cur_view == V_DAY || cur_view == V_WEEK)) {
		/* the short form when the toolbar is narrow */
		DT d, e;
		long first = cur_day;
		if (cur_view == V_WEEK)
			first -= (wday_of(cur_day) - opt.weekstart + 7) % 7;
		ct_split(first * MIN_PER_DAY, &d);
		ct_split((first + 6) * MIN_PER_DAY, &e);
		if (cur_view == V_DAY)
			snprintf(t, sizeof(t), "%s %d %s %d", wday_short[d.wday], d.day, month_short[d.mon - 1], d.year);
		else if (d.mon == e.mon)
			snprintf(t, sizeof(t), "%d-%d %s %d", d.day, e.day, month_short[d.mon - 1], e.year);
		else
			snprintf(t, sizeof(t), "%d %s-%d %s", d.day, month_short[d.mon - 1], e.day, month_short[e.mon - 1]);
	}
	text_at(r_title.x, r_title.y, t, (long)strlen(t), r_title.w / cw, TX_BOLD | TX_LTR);
	hline(work.x, work.x + work.w - 1, work.y + r_tool.h - 1);
}

static void draw_all(GRECT *clip)
{
	GRECT r;
	clip_on(clip);
	draw_toolbar(clip);
	r = r_side;
	if (r.w && rect_and(&r, clip)) {
		side_draw(clip);
		clip_on(clip);
		vline(r_side.x + r_side.w - 1, r_side.y, r_side.y + r_side.h - 1);
	}
	r = r_view;
	if (rect_and(&r, clip))
		views_draw(&r);
	clip_off();
}

void win_redraw(GRECT *area)
{
	GRECT box, full;
	if (win_h <= 0)
		return;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	full = work;
	if (area && !rect_and(&full, area)) {
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box;
		if (rect_and(&d, &full))
			draw_all(&d);
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	win_sliders();
}

/* where the window goes so its work area starts on a 16-pixel boundary:
   VDI draws byte-aligned text and fills faster */
static short snap_x(short x)
{
	short wx, wy, ww, wh;
	wind_calc(WC_WORK, KIND, x, 100, 400, 200, &wx, &wy, &ww, &wh);
	x -= (wx & 15);
	if (x < desk_x)
		x += 16;
	return x;
}

void win_open(void)
{
	short x, y, w, h;
	if (win_h > 0) {
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		return;
	}
	win_h = wind_create(KIND, desk_x, desk_y, desk_w, desk_h);
	if (win_h <= 0) {
		win_h = -1;
		alert(1, "[3][No window left for Calendar:|close one and try again.][ OK ]");
		return;
	}
	win_title();
	wind_set_str(win_h, WF_INFO, info);
	x = opt.win_x;
	y = opt.win_y;
	w = opt.win_w;
	h = opt.win_h;
	if (w < 50 * cw || h < 16 * ch || x < desk_x || y < desk_y ||
	    x + w > desk_x + desk_w + 8 || y + h > desk_y + desk_h + 8) {
		x = desk_x + (desk_w > 80 * cw ? desk_w / 20 : 0);
		y = desk_y + (desk_h > 30 * ch ? ch : 0);
		w = desk_w - 2 * (x - desk_x);
		h = desk_h - 2 * (y - desk_y);
	}
	{
		/* snapped right, it must still end on the screen */
		short sx = snap_x(x);
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		x = sx;
	}
	wind_open(win_h, x, y, w, h);
	win_layout();
	win_sliders();
}

static void remember_place(void)
{
	short x, y, w, h;
	if (win_h <= 0)
		return;
	wind_get(win_h, WF_CURRXYWH, &x, &y, &w, &h);
	if (x != opt.win_x || y != opt.win_y || w != opt.win_w || h != opt.win_h) {
		opt.win_x = x;
		opt.win_y = y;
		opt.win_w = w;
		opt.win_h = h;
		store_save_settings();
	}
}

void win_close(void)
{
	if (win_h <= 0)
		return;
	remember_place();
	wind_close(win_h);
	wind_delete(win_h);
	win_h = -1;
}

void win_message(short *msg)
{
	GRECT r;
	if (msg[3] != win_h || win_h <= 0)
		return;
	switch (msg[0]) {
	case WM_REDRAW:
		r.x = msg[4];
		r.y = msg[5];
		r.w = msg[6];
		r.h = msg[7];
		win_redraw(&r);
		break;
	case WM_TOPPED:
		wind_set(win_h, WF_TOP, 0, 0, 0, 0);
		break;
	case WM_CLOSED:
		win_close();
		if (!is_acc)
			quit_now = 1;
		break;
	case WM_FULLED: {
		short x, y, w, h, fx, fy, fw, fh;
		wind_get(win_h, WF_CURRXYWH, &x, &y, &w, &h);
		wind_get(win_h, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (x == fx && y == fy && w == fw && h == fh)
			wind_get(win_h, WF_PREVXYWH, &fx, &fy, &fw, &fh);
		wind_set(win_h, WF_CURRXYWH, fx, fy, fw, fh);
		win_layout();
		win_redraw(0);
		remember_place();
		break;
	}
	case WM_SIZED:
	case WM_MOVED: {
		short w = msg[6] < 40 * cw ? 40 * cw : msg[6], h = msg[7] < 14 * ch ? 14 * ch : msg[7];
		short sx = snap_x(msg[4]);
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		wind_set(win_h, WF_CURRXYWH, sx, msg[5], w, h);
		win_layout();
		if (msg[0] == WM_SIZED)
			win_redraw(0);
		remember_place();
		break;
	}
	case WM_ARROWED:
		/* 0 page up, 1 page down, 2 line up, 3 line down */
		if (cur_view == V_DAY || cur_view == V_WEEK) {
			short d = msg[4] == 0 ? -6 : msg[4] == 1 ? 6 : msg[4] == 2 ? -1 : msg[4] == 3 ? 1 : 0;
			long vis = views_hours_visible();
			short ns = day_scroll + d;
			if (ns > 24 - vis)
				ns = (short)(24 - vis);
			if (ns < 0)
				ns = 0;
			if (ns != day_scroll) {
				day_scroll = ns;
				win_redraw(&r_view);
			}
		} else if (msg[4] <= 3) {
			views_step(msg[4] == 0 || msg[4] == 2 ? -1 : 1);
		}
		break;
	case WM_VSLID:
		if (cur_view == V_DAY || cur_view == V_WEEK) {
			long vis = views_hours_visible();
			short ns = vis >= 24 ? 0 : (short)((long)msg[4] * (24 - vis) / 1000);
			if (ns != day_scroll) {
				day_scroll = ns;
				win_redraw(&r_view);
			}
		}
		break;
	}
}

int win_mouse(short mx, short my, short button, short clicks, short kstate)
{
	int i;
	if (win_h <= 0 || !rect_hit(&work, mx, my))
		return 0;
	if (rect_hit(&r_tool, mx, my)) {
		for (i = 0; i < NBUT; i++)
			if (rect_hit(&but[i], mx, my))
				break;
		switch (i) {
		case B_PREV: views_step(-1); break;
		case B_NEXT: views_step(1); break;
		case B_TODAY: views_today(); break;
		case B_DAY: case B_WEEK: case B_MONTH: case B_YEAR:
			views_goto(cur_day, (short)(i - B_DAY));
			break;
		case B_NEW: cmd_new_event(); break;
		case B_REFRESH: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); break;
		case B_MENU: cmd_menu(but[B_MENU].x, but[B_MENU].y + but[B_MENU].h); break;
		default:
			if (rect_hit(&r_title, mx, my) && clicks == 2)
				dlg_goto();
		}
		return 1;
	}
	if (r_side.w && rect_hit(&r_side, mx, my))
		return side_click(mx, my, clicks, button);
	if (rect_hit(&r_view, mx, my))
		return views_click(mx, my, clicks, kstate, button);
	return 0;
}

int win_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	unsigned char a = KEY_ASCII(key);
	if (kstate & K_CTRL) {
		switch (scan) {
		case 0x31: cmd_new_event(); return 1;		/* ^N */
		case 0x13: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); return 1;	/* ^R */
		case 0x21: dlg_find(); return 1;			/* ^F */
		case 0x22: dlg_goto(); return 1;			/* ^G */
		case 0x11:					/* ^W */
		case 0x10:					/* ^Q */
			win_close();
			if (!is_acc)
				quit_now = 1;
			return 1;
		case 0x14: views_today(); return 1;		/* ^T */
		case 0x1f: opt.sidebar = !opt.sidebar; win_layout(); win_redraw(0); return 1;	/* ^S */
		}
	}
	if (!(kstate & (K_CTRL | K_ALT))) {
		switch (a) {
		case 't': case 'T': views_today(); return 1;
		case 'd': case 'D': views_goto(cur_day, V_DAY); return 1;
		case 'w': case 'W': views_goto(cur_day, V_WEEK); return 1;
		case 'm': case 'M': views_goto(cur_day, V_MONTH); return 1;
		case 'y': case 'Y': views_goto(cur_day, V_YEAR); return 1;
		case 'n': case 'N': cmd_new_event(); return 1;
		}
	}
	return views_key(kstate, key);
}
