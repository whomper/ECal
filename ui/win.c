/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * win.c - ECal's window: the toolbar on top (back, Today, forward,
 * the period's name, Day/Week/Month/Year, new event, refresh, menu),
 * the calendar list on the left, the view on the right, the status in
 * the info line.
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "bidi.h"
#include "iconify.h"
#include "av.h"
#include "plat.h"
#include <tos.h>

short win_h = -1;
short win_iconified;	/* shown as ECal's icon (WM_ICONIFY) */
GRECT work, r_tool, r_side, r_view;

#define KIND (NAME | CLOSER | FULLER | MOVER | SIZER | INFO | UPARROW | DNARROW | VSLIDE)

static char title[80], info[140];

/* with the iconifier when the AES can iconify (common/ui/iconify.c) */
static short kind(void)
{
	return icon_kind(KIND);
}

enum { B_SIDE, B_PREV, B_TODAY, B_NEXT, B_DAY, B_WEEK, B_MONTH, B_YEAR, B_NEW, B_REFRESH, B_MENU, NBUT };
static GRECT but[NBUT];
static const char *const blabel[NBUT] = {
	" ", "\x04", "Today", "\x03", "Day", "Week", "Month", "Year", "+", "Refresh", "\xF0"
};
static GRECT r_title;

void win_title(void)
{
	if (win_iconified)
		icon_today(title, sizeof(title));	/* the icon's title: today */
	else
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

/* GEM draws the info line left to right as it is given: Hebrew in it
   (an event's title) goes in visual order, and into the font's letters */
void win_status(const char *s)
{
	char logical[sizeof(info)];
	short n;
	snprintf(logical, sizeof(logical), " %s", s);
	n = (short)strlen(logical);
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (bidi_has_rtl(logical, n)) {
		bidi_visual(logical, n, 0, info);
		info[n] = 0;
		heb_font_map(info, n, opt.hebfont);
	} else {
		str_copy(info, logical, sizeof(info));
	}
	if (win_h > 0)
		wind_set_str(win_h, WF_INFO, info);
}

void win_layout(void)
{
	short th = ch + 8, sw, x, i, bw;
	if (win_h <= 0 || win_iconified)
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
	/* buttons: the calendar list, back Today forward on the left, the
	   views and the rest on the right */
	x = work.x + 4;
	for (i = B_SIDE; i <= B_NEXT; i++) {
		bw = ((short)strlen(blabel[i]) + 2) * cw;
		but[i].x = x;
		but[i].y = work.y + 3;
		but[i].w = bw;
		but[i].h = ch + 2;
		x += bw + (i == B_NEXT || i == B_SIDE ? cw : 2);
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

static long sl_size = -1, sl_pos = -1;	/* as last set */

void win_sliders(void)
{
	long size, pos;
	if (win_h <= 0)
		return;
	views_slider(&size, &pos);
	/* only when they change: the AES draws them each time */
	if (size < 30)
		size = 30;
	if (pos > 1000)
		pos = 1000;
	if (size != sl_size)
		wind_set(win_h, WF_VSLSIZE, (short)size, 0, 0, 0);
	if (pos != sl_pos)
		wind_set(win_h, WF_VSLIDE, (short)pos, 0, 0, 0);
	sl_size = size;
	sl_pos = pos;
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
	if (i == B_SIDE) {
		/* a window with its list on the left, the list filled while shown */
		GRECT w = r, pane;
		w.x += cw / 2;
		w.y += 3;
		w.w -= cw;
		w.h -= 6;
		frame(&w, 1);
		pane = w;
		pane.w = w.w / 3;
		if (opt.sidebar)
			fill(&pane, 1);
		line_col(pane.x + pane.w, w.y, pane.x + pane.w, w.y + w.h - 1, 1);
		return;
	}
	if (i == B_PREV || i == B_NEXT) {
		/* an arrow: a solid head and a shaft, drawn, so it looks the
		   same whatever the font (a GDOS font's arrow glyphs are tiny) */
		short hh = (r.h - 6) / 2, cy = r.y + r.h / 2, tw, sw, sh, c, x0, dir = i == B_PREV ? -1 : 1;
		if (hh < 2)
			hh = 2;
		tw = (short)(hh * 2 * cw / ch);	/* as wide as high, on tall pixels too */
		if (tw < 3)
			tw = 3;
		sw = tw;
		sh = hh / 3;
		x0 = r.x + r.w / 2 + dir * (tw + sw) / 2;	/* the tip */
		for (c = 0; c <= tw; c++) {
			short half = (short)((long)hh * c / tw);
			line_col(x0 - dir * c, cy - half, x0 - dir * c, cy + half, 1);
		}
		for (c = tw; c <= tw + sw; c++)
			line_col(x0 - dir * c, cy - sh, x0 - dir * c, cy + sh, 1);
		return;
	}
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

/* ---- drawing off the screen ----
 * A view is many small fills, lines and texts; drawn straight onto the
 * screen, a slow machine shows them appearing one by one. So the window
 * is drawn into a screen-sized buffer and copied to the screen at once.
 * Two ways to draw there:
 *  - an off-screen bitmap (EdDI v_opnbm: NVDI 3 and later, fVDI...): a
 *    VDI workstation of its own over our buffer; the drawing code's
 *    vdi_h is that one while it draws. Safe under any AES.
 *  - with TOS's own VDI, which draws at the logical screen base: the
 *    buffer is made the logical screen (Setscreen) while drawing, the
 *    shown screen staying. Only under single-tasking TOS on the machine's
 *    own video, nothing else being able to draw meanwhile.
 * Either is used only when a test at start-up finds a line drawn that
 * way really in the buffer; otherwise the window is drawn as before. */
static void *obuf;		/* the buffer, NULL: draw straight on the screen */
static MFDB obuf_mfdb;
static short bm_h;		/* the off-screen bitmap's workstation, 0: none */
static const char *drawing = "straight on the screen";

const char *win_drawing(void)
{
	return drawing;
}

static long xbios_setscreen(void *log)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w #-1,-(%%sp)\n\tmove.l #-1,-(%%sp)\n\tmove.l %1,-(%%sp)\n\t"
			 "move.w #5,-(%%sp)\n\ttrap #14\n\tlea 12(%%sp),%%sp"
			 : "=r"(r) : "r"(log) : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
	return r;
}

static void *xbios_base(short fn)	/* 2 Physbase, 3 Logbase */
{
	return (void *)trap14_w(fn);
}

/* EdDI's off-screen bitmaps (VDI 100 and 101, sub-opcode 1), not in
   common/'s bindings */
static short bc[12], bin[20], bout[57], bpin[4], bpout[12];
static void *bpb[5] = { bc, bin, bpin, bout, bpout };

static void vdi_bm(void)
{
	__asm__ volatile("move.l %0,%%d1\n\tmove.w #115,%%d0\n\ttrap #2"
			 : : "g"((long)bpb) : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc");
}

static short v_opnbm(short screen, MFDB *m)
{
	short i;
	memset(bc, 0, sizeof(bc));
	for (i = 0; i < 10; i++)
		bin[i] = 1;
	bin[10] = 2;			/* raster coordinates */
	bin[11] = m->fd_w - 1;
	bin[12] = m->fd_h - 1;
	for (i = 13; i < 20; i++)
		bin[i] = 0;		/* the screen's pixel size and format */
	bc[0] = 100;
	bc[3] = 20;
	bc[5] = 1;
	bc[6] = screen;
	bc[7] = (short)((unsigned long)m >> 16);
	bc[8] = (short)((unsigned long)m & 0xffff);
	vdi_bm();
	return bc[6] != screen ? bc[6] : 0;
}

static void v_clsbm(short h)
{
	memset(bc, 0, sizeof(bc));
	bc[0] = 101;
	bc[5] = 1;
	bc[6] = h;
	vdi_bm();
}

/* a short black line at the top left, drawn with handle h: does it land
   in the buffer b? */
static int lands_in(unsigned char *b, short h, int setscreen)
{
	void *screen = xbios_base(3);
	short keep = vdi_h, i, changed = 0;
	GRECT r;
	for (i = 0; i < 64; i++)
		b[i] = 0x55;
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	if (setscreen)
		xbios_setscreen(b);
	vdi_h = h;
	draw_forget();
	r.x = 0;
	r.y = 0;
	r.w = 16;
	r.h = 1;
	clip_on(&r);
	fill(&r, 1);
	clip_off();
	vdi_h = keep;
	draw_forget();
	if (setscreen)
		xbios_setscreen(screen);
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	for (i = 0; i < 2 * planes && i < 64; i++)
		changed |= b[i] != 0x55;
	return changed;
}

void win_buffer_init(void)
{
	long size = (long)((scr_w + 15) / 16) * 2 * planes * scr_h;
	unsigned char *p, *b;
	int multitasking = get_cookie(0x4D694E54L) || get_cookie(0x4D616758L);	/* MiNT, MagiC */
	if (planes < 1 || size > 640L * 1024)
		return;
	p = Mxalloc(size + 256, 0);			/* ST RAM, where the blitter reaches */
	if ((long)p == -32 || (long)p == 0)		/* no Mxalloc: plain Malloc is ST RAM */
		p = (unsigned char *)trap1_wl(0x48, size + 256);
	if ((long)p <= 0)
		return;
	b = (unsigned char *)(((unsigned long)p + 255) & ~255UL);
	memset(&obuf_mfdb, 0, sizeof(obuf_mfdb));
	obuf_mfdb.fd_addr = b;
	obuf_mfdb.fd_w = (scr_w + 15) & ~15;
	obuf_mfdb.fd_h = scr_h;
	obuf_mfdb.fd_wdwidth = (scr_w + 15) / 16;
	obuf_mfdb.fd_nplanes = planes;
	if (get_cookie(0x45644449L)) {			/* EdDI */
		MFDB m = obuf_mfdb;
		short h = v_opnbm(vdi_h, &m);
		if (h > 0 && lands_in(b, h, 0)) {
			if (font_gdos_loaded())
				vst_load_fonts(h, 0);
			bm_h = h;
			obuf = b;
			drawing = "off the screen, in an EdDI bitmap";
			return;
		}
		if (h > 0)
			v_clsbm(h);
	}
	if (!multitasking && xbios_base(3) == xbios_base(2) && lands_in(b, vdi_h, 1)) {
		obuf = b;
		drawing = "off the screen, through the logical screen";
		return;
	}
	trap1_wl(0x49, (long)p);			/* Mfree */
}

/* the GDOS fonts loaded on the window's workstation since: on the
   bitmap too */
void win_buffer_fonts(void)
{
	if (bm_h && font_gdos_loaded())
		vst_load_fonts(bm_h, 0);
}

void win_redraw(GRECT *area)
{
	GRECT box, full;
	if (win_h <= 0)
		return;
	if (win_iconified) {
		icon_window(area);	/* over the small window, not the old work area */
		return;
	}
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	full = work;
	if (area && !rect_and(&full, area)) {
		graf_mouse(M_ON, 0);
		wind_update(END_UPDATE);
		return;
	}
	if (obuf && (bm_h || xbios_base(3) == xbios_base(2))) {
		/* all of it off the screen, then each visible piece copied */
		MFDB scr;
		memset(&scr, 0, sizeof(scr));
		if (bm_h) {
			short keep = vdi_h;
			vdi_h = bm_h;
			draw_forget();
			font_reselect();	/* the window's font, on the bitmap too */
			draw_all(&full);
			vdi_h = keep;
			draw_forget();
		} else {
			void *screen = xbios_base(3);
			xbios_setscreen(obuf);
			draw_all(&full);
			xbios_setscreen(screen);
		}
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h) {
			GRECT d = box;
			if (rect_and(&d, &full)) {
				short pxy[8];
				pxy[0] = pxy[4] = d.x;
				pxy[1] = pxy[5] = d.y;
				pxy[2] = pxy[6] = d.x + d.w - 1;
				pxy[3] = pxy[7] = d.y + d.h - 1;
				/* NVDI clips raster copies too (TOS's VDI doesn't):
				   the piece's own, not one left by a dialog */
				clip_on(&d);
				vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &scr);	/* S_ONLY */
			}
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
		clip_off();
	} else {
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h) {
			GRECT d = box;
			if (rect_and(&d, &full))
				draw_all(&d);
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
	}
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	win_sliders();
}

short win_popup(short x, short y, const char *const *labels, short n)
{
	short k = popup(x, y, labels, n);
	win_redraw(&popup_area);
	return k;
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
	win_iconified = 0;
	win_h = wind_create(kind(), desk_x, desk_y, desk_w, desk_h);
	if (win_h <= 0) {
		win_h = -1;
		alert(1, "[3][No window left for ECal:|close one and try again.][ OK ]");
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
	sl_size = sl_pos = -1;	/* a new window's sliders */
	if (is_acc)		/* the desktop drops files on it (VA_DRAGACCWIND) */
		av_window(win_h, 1);
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
		settings_later();
	}
}

/* a toolbar button: acts on the first click (the title alone takes a double) */
int win_instant(short mx, short my)
{
	return rect_hit(&r_tool, mx, my) && !rect_hit(&r_title, mx, my);
}

void win_close(void)
{
	if (win_h <= 0)
		return;
	if (!win_iconified)	/* the place was kept when it was iconified */
		remember_place();
	closing_down(quit_now || !is_acc);
	if (is_acc)
		av_window(win_h, 0);
	wind_close(win_h);
	wind_delete(win_h);
	win_h = -1;
}

/* the content of a (in the view) moves up by dy pixels, down when dy <
   0: copied on the screen when all of a shows, then only the strip that
   comes in is drawn; otherwise a is drawn again */
void win_scroll(const GRECT *a, short dy)
{
	GRECT box, strip = *a;
	short ady = dy < 0 ? -dy : dy, whole = 0;
	if (win_h <= 0 || win_iconified)
		return;
	wind_update(BEG_UPDATE);
	if (ady < a->h) {
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h && !whole) {
			whole = box.x <= a->x && box.y <= a->y && box.x + box.w >= a->x + a->w &&
				box.y + box.h >= a->y + a->h;
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
	}
	if (whole) {
		MFDB scr;
		short pxy[8];
		memset(&scr, 0, sizeof(scr));
		pxy[0] = pxy[4] = a->x;
		pxy[2] = pxy[6] = a->x + a->w - 1;
		pxy[1] = a->y + (dy > 0 ? ady : 0);
		pxy[3] = pxy[1] + a->h - ady - 1;
		pxy[5] = a->y + (dy > 0 ? 0 : ady);
		pxy[7] = pxy[5] + a->h - ady - 1;
		graf_mouse(M_OFF, 0);
		vro_cpyfm(vdi_h, 3, pxy, &scr, &scr);	/* S_ONLY */
		graf_mouse(M_ON, 0);
		strip.h = ady;
		if (dy > 0)
			strip.y = a->y + a->h - ady;
	}
	wind_update(END_UPDATE);
	win_redraw(&strip);
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
	case WM_ICONIFY:
	case WM_ALLICONIFY:
		if (win_iconified)
			break;
		remember_place();
		win_iconified = 1;
		wind_set(win_h, WF_ICONIFY, msg[4], msg[5], msg[6], msg[7]);
		win_title();
		break;
	case WM_UNICONIFY:
		if (!win_iconified)
			break;
		wind_set(win_h, WF_UNICONIFY, msg[4], msg[5], msg[6], msg[7]);
		win_iconified = 0;
		win_title();
		win_layout();
		win_redraw(0);
		break;
	case WM_CLOSED:
		if (!is_acc)
			quit_now = 1;
		win_close();
		break;
	case WM_BOTTOMED:
		wind_set(win_h, WF_BOTTOM, 0, 0, 0, 0);
		break;
	case WM_FULLED: {
		short x, y, w, h, fx, fy, fw, fh;
		if (win_iconified)
			break;
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
		if (win_iconified) {	/* the icon goes where it is put, as it is */
			wind_set(win_h, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			break;
		}
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
		if (msg[4] <= 3)
			views_arrow(msg[4]);
		break;
	case WM_VSLID:
		views_slide(msg[4]);
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
		case B_SIDE:
			opt.sidebar = !opt.sidebar;
			settings_later();
			win_layout();
			win_redraw(0);
			break;
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
	if (key_ctrl(kstate, key)) {
		switch (scan) {
		case 0x31: cmd_new_event(); return 1;		/* ^N */
		case 0x13: cmd_refresh(kstate & (K_LSHIFT | K_RSHIFT)); return 1;	/* ^R */
		case 0x21: dlg_find(); return 1;			/* ^F */
		case 0x22: dlg_goto(); return 1;			/* ^G */
		case 0x11:					/* ^W */
		case 0x10:					/* ^Q */
			if (!is_acc)
				quit_now = 1;
			win_close();
			return 1;
		case 0x14: views_today(); return 1;		/* ^T */
		case 0x2e: views_copy(); return 1;		/* ^C: the event chosen */
		case 0x33: dlg_settings(); return 1;		/* ^, */
		case 0x1f:					/* Shift+^S; ^S is Save */
			if (!(kstate & (K_LSHIFT | K_RSHIFT)))
				return 0;
			opt.sidebar = !opt.sidebar;
			settings_later();
			win_layout();
			win_redraw(0);
			return 1;
		}
	}
	if (!(kstate & K_ALT) && !key_ctrl(kstate, key)) {
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
