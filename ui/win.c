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
#include "conn.h"
#include "bidi.h"
#include "iconify.h"
#include "av.h"
#include "plat.h"
#include "text.h"
#include "click.h"
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
/* the window's smallest, the same when it opens and when it is sized
   (D38, W10): content-bound, the toolbar's buttons at their narrowest
   (18 cells on the left, 30 on the right with "D" "W" "M" "Y") with the
   margins and the Hebrew keyboard's badge, and a few rows of the view */
#define MIN_COLS 52
#define MIN_ROWS 14

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
static int info_due;	/* info made, not yet given to the AES (win_status_later) */

static void info_make(const char *s)
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
}

void win_status(const char *s)
{
	info_make(s);
	info_due = 0;
	if (win_h > 0)
		wind_set_str(win_h, WF_INFO, info);
}

/* win_status in the next turn, not the one answering a key or click: the
   AES draws the info line again each time it is set (60 ms of a Tab's
   100 on an ST). A win_status meanwhile wins */
void win_status_later(const char *s)
{
	info_make(s);
	info_due = win_h > 0;
}

/* the view drawn in slices (below, win_view_later) */
static struct {
	int on, inside, ready;
	GRECT tile[200];		/* the view's pieces (views_tiles) */
	short nt, ti;			/* how many, the next to draw */
	unsigned long t0;		/* begun when */
	unsigned long prep_ms, draw_ms;	/* its time in the events (collect, layout) and in drawing */
	short take, rows;		/* pieces of a row, or whole rows, to draw together next */
	unsigned long row_ms, piece_ms;	/* the dearest row and piece drawn yet */
	int mini;			/* the small month still to draw, after it */
	long mini_from;			/* the day it marked, -1 for all of it */
	int later;			/* pieces of the view still to draw (win_redraw_later) */
	GRECT later_r[4];
	short st_n, st_i;		/* a new window's toolbar and list, a piece a slice */
	int show;			/* each piece of the view copied when drawn (a new window's) */
	unsigned long copy_ms;		/* the last copy of the whole view's time */
	GRECT st_r[8];
} vj;
static int start_fresh;		/* the window just opened: its first redraw in slices */

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
	/* the right end clear for the Hebrew keyboard's badge while it is on */
	x = work.x + work.w - 4 - (hebrew_kbd ? 20 : 0);
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
	if (vj.on)
		vj.ready = 0;		/* a view being drawn: from the top, in its new place */
}

static long sl_size = -1, sl_pos = -1;	/* as last set */
static long sl_want_size, sl_want_pos;	/* as they should be */
static int sl_due;			/* not yet set so */

/* the sliders as the view is now: set in the turns after (sliders_step),
   one a turn, as the AES draws each again (two took an ST 85 to 120 ms,
   on top of what the turn drew) */
void win_sliders(void)
{
	long size, pos;
	if (win_h <= 0 || vj.on)
		return;		/* (a view being drawn in slices: when it is done) */
	views_slider(&size, &pos);
	if (size < 30)
		size = 30;
	if (pos > 1000)
		pos = 1000;
	sl_want_size = size;
	sl_want_pos = pos;
	/* only when they change: the AES draws them each time */
	sl_due = size != sl_size || pos != sl_pos;
}

/* one slider set as win_sliders asked: 1 while the other is still due */
static int sliders_step(void)
{
	if (sl_due && win_h > 0) {
		if (sl_want_size != sl_size) {
			wind_set(win_h, WF_VSLSIZE, (short)sl_want_size, 0, 0, 0);
			sl_size = sl_want_size;
		} else if (sl_want_pos != sl_pos) {
			wind_set(win_h, WF_VSLIDE, (short)sl_want_pos, 0, 0, 0);
			sl_pos = sl_want_pos;
		}
	}
	sl_due = win_h > 0 && (sl_want_size != sl_size || sl_want_pos != sl_pos);
	return sl_due;
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
	text_at((r.x + (r.w - n * cw) / 2 + 3) & ~7, r.y + 1, l, n, n, TX_LTR | (on ? TX_INVERSE : TX_WHITE) |
		(i == B_TODAY ? TX_BOLD : 0));	/* (inside its frame, clear of it) */
}

static void draw_toolbar(GRECT *clip)
{
	GRECT r = r_tool;
	char t[60];
	int i;
	if (!rect_and(&r, clip))
		return;
	fill(&r, 0);
	for (i = 0; i < NBUT; i++) {
		GRECT b = but[i];
		if (rect_and(&b, clip))	/* (the title alone: no buttons, each a text and frame) */
			draw_button(i);
	}
	views_title(t, sizeof(t));
	if ((short)strlen(t) > r_title.w / cw && cur_view != V_YEAR) {
		/* shorter forms when the toolbar is narrow: the first that fits,
		   short month names, then without the year ("18-24 Oct") */
		DT d, e;
		long first = cur_day;
		char f[3][40];
		int k;
		if (cur_view == V_WEEK)
			first -= (wday_of(cur_day) - opt.weekstart + 7) % 7;
		ct_split(first * MIN_PER_DAY, &d);
		ct_split((first + 6) * MIN_PER_DAY, &e);
		if (cur_view == V_DAY) {
			snprintf(f[0], sizeof(f[0]), "%s %d %s %d", wday_short[d.wday], d.day, month_short[d.mon - 1], d.year);
			snprintf(f[1], sizeof(f[1]), "%s %d %s", wday_short[d.wday], d.day, month_short[d.mon - 1]);
			snprintf(f[2], sizeof(f[2]), "%d %s", d.day, month_short[d.mon - 1]);
		} else if (cur_view == V_MONTH) {
			snprintf(f[0], sizeof(f[0]), "%s %d", month_short[d.mon - 1], d.year);
			snprintf(f[1], sizeof(f[1]), "%s", month_short[d.mon - 1]);
			f[2][0] = 0;
		} else if (d.mon == e.mon) {
			snprintf(f[0], sizeof(f[0]), "%d-%d %s %d", d.day, e.day, month_short[d.mon - 1], e.year);
			snprintf(f[1], sizeof(f[1]), "%d-%d %s", d.day, e.day, month_short[d.mon - 1]);
			f[2][0] = 0;
		} else {
			snprintf(f[0], sizeof(f[0]), "%d %s-%d %s %d", d.day, month_short[d.mon - 1], e.day,
				 month_short[e.mon - 1], e.year);
			snprintf(f[1], sizeof(f[1]), "%d %s-%d %s", d.day, month_short[d.mon - 1], e.day,
				 month_short[e.mon - 1]);
			f[2][0] = 0;
		}
		for (k = 0; k < 3 && f[k][0]; k++) {
			str_copy(t, f[k], sizeof(t));
			if ((short)strlen(t) <= r_title.w / cw)
				break;
		}
	}
	text_at(r_title.x, r_title.y, t, (long)strlen(t), r_title.w / cw, TX_BOLD | TX_LTR | TX_WHITE);
	hebrew_badge_right(vdi_h, r_tool.x, r_tool.y, r_tool.w, r_tool.h - 1, 1);	/* while it is on */
	hline(work.x, work.x + work.w - 1, work.y + r_tool.h - 1);
}

#ifdef AES_PROFILE
static unsigned long ms_tool, ms_side;	/* the last draw_all's, for REDRAWMS */
#endif


static void draw_all(GRECT *clip)
{
	GRECT r;
#ifdef AES_PROFILE
	unsigned long t0 = pf_ms();
#endif
	text_bytes = 1;		/* the window's text on bytes (draw.c) */
	clip_on(clip);
	draw_toolbar(clip);
#ifdef AES_PROFILE
	ms_tool = pf_ms() - t0;
	t0 = pf_ms();
#endif
	r = r_side;
	if (r.w && rect_and(&r, clip)) {
		side_draw(clip);
		clip_on(clip);
		vline(r_side.x + r_side.w - 1, r_side.y, r_side.y + r_side.h - 1);
	}
#ifdef AES_PROFILE
	ms_side = pf_ms() - t0;
#endif
	r = r_view;
	if (rect_and(&r, clip))
		views_draw(&r);
	clip_off();
	text_bytes = 0;
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

static int off_screen(void)
{
	return obuf && (bm_h || xbios_base(3) == xbios_base(2));
}

/* the screen under a dialog, kept in the buffer while it is up (below,
   win_save_under); anything drawn in the buffer meanwhile ends it */
static int under_kept;

/* full drawn into the buffer (off_screen()) */
static void off_draw(GRECT *full)
{
	under_kept = 0;
	if (bm_h) {
		short keep = vdi_h;
		vdi_h = bm_h;
		draw_forget();
		font_reselect();	/* the window's font, on the bitmap too */
		draw_all(full);
		vdi_h = keep;
		draw_forget();
	} else {
		/* (the mouse hidden meanwhile: TOS draws it where the logical
		   screen is, the buffer while it is that) */
		void *screen = xbios_base(3);
		if (vj.inside)
			graf_mouse(M_OFF, 0);
		xbios_setscreen(obuf);
		draw_all(full);
		xbios_setscreen(screen);
		if (vj.inside)
			graf_mouse(M_ON, 0);
	}
}

/* The screen under r kept in the buffer, where a dialog drawn over the
   screen (TOS: it holds the screen, nothing else draws) is about to go:
   when it closes the same pixels go back, no window drawn again (an
   ST's Settings 1.1 s less). 0 where there is no buffer, or the view
   is half drawn in it */
/* r widened to whole words across (the copy without shifts at its
   edges), and cut to the screen: a dialog as tall as the desktop, with
   the 2 pixels its shadow takes, reached past the buffer's last line */
static void under_rect(const GRECT *r, short *pxy)
{
	short x1 = (short)(r->x & ~15), x2 = (short)((r->x + r->w + 15) & ~15) - 1;
	short y1 = r->y, y2 = r->y + r->h - 1;
	if (x1 < 0)
		x1 = 0;
	if (x2 > scr_w - 1)
		x2 = (short)(scr_w - 1);
	if (y1 < 0)
		y1 = 0;
	if (y2 > scr_h - 1)
		y2 = (short)(scr_h - 1);
	pxy[0] = pxy[4] = x1;
	pxy[1] = pxy[5] = y1;
	pxy[2] = pxy[6] = x2;
	pxy[3] = pxy[7] = y2;
}

/* the screen is the buffer's own layout, at Logbase: TOS's VDI in a
   bit-plane mode, the buffer swapped in as the logical screen to draw
   (win_buffer_init). There a copy or a clear is a memcpy or a memset a
   line, several times faster than TOS's raster copy on an ST (Settings'
   box: 65 ms to keep the screen under it by vro_cpyfm) */
static int plain_screen(void)
{
	return obuf && !bm_h && planes <= 8 && xbios_base(3) == xbios_base(2);
}

/* where the 16 pixels from x of line y start; a line on is LINE_BYTES
   further (the pointers stepped a line at a time: a 68000 multiplies
   longs in a library call) */
#define LINE_BYTES ((long)obuf_mfdb.fd_wdwidth * 2 * planes)
static unsigned char *at(void *base, short x, short y)
{
	return (unsigned char *)base + (long)y * LINE_BYTES + (long)(x >> 4) * 2 * planes;
}

/* the words across x1..x2 (whole words) of lines y1..y2, from one to the other */
static void copy_lines(int to_buf, short x1, short y1, short x2, short y2)
{
	unsigned char *s = at(xbios_base(3), x1, y1), *b = at(obuf, x1, y1);
	long n = (long)((x2 + 1 - x1) >> 4) * 2 * planes, step = LINE_BYTES;
	short y;
	for (y = y1; y <= y2; y++, s += step, b += step) {
		if (to_buf)
			memcpy(b, s, n);
		else
			memcpy(s, b, n);
	}
}

static void under_copy(const GRECT *r, int to_buf)
{
	MFDB scr;
	short pxy[8];
	under_rect(r, pxy);
	graf_mouse(M_OFF, 0);
	if (plain_screen()) {
		copy_lines(to_buf, pxy[0], pxy[1], pxy[2], pxy[3]);
	} else {
		memset(&scr, 0, sizeof(scr));
		clip_off();
		if (to_buf)
			vro_cpyfm(vdi_h, 3, pxy, &scr, &obuf_mfdb);	/* S_ONLY */
		else
			vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &scr);
	}
	graf_mouse(M_ON, 0);
}

/* The screen under r kept in the buffer, where a dialog drawn over the
   screen (TOS: it holds the screen, nothing else draws) is about to go:
   when it closes the same pixels go back, no window drawn again (an
   ST's Settings 1.1 s less). 0 where there is no buffer, or the view
   is half drawn in it */
int win_save_under(const GRECT *r)
{
	if (!obuf || vj.on)
		return 0;
	under_copy(r, 1);
	under_kept = 1;
	return 1;
}

/* the pixels kept under r back on the screen: 0 if they were not kept,
   or something drew in the buffer since */
int win_restore_under(const GRECT *r)
{
	if (!under_kept)
		return 0;
	under_kept = 0;
	under_copy(r, 0);
	return 1;
}

/* r (within the clip) in colour 0, the bits cleared: by memory where the
   screen is plain (an ST's dialog box: 55 ms by vr_recfl), else a fill.
   The mouse is off */
void win_clear(GRECT *r)
{
	short x1 = r->x, x2 = r->x + r->w - 1, y, k;
	unsigned short lm, rm;
	long g1, g2, step;
	size_t mid = 0;
	unsigned char *line;
	void *scr;
	if (!plain_screen() || r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0 ||
	    x2 > scr_w - 1 || r->y + r->h > scr_h) {
		fill(r, 0);	/* (the VDI clips; this writes the memory) */
		return;
	}
	scr = xbios_base(3);
	g1 = x1 >> 4;
	g2 = x2 >> 4;
	lm = (unsigned short)(0xFFFFu >> (x1 & 15));
	rm = (unsigned short)(0xFFFFu << (15 - (x2 & 15)));
	line = at(scr, x1, r->y);
	step = LINE_BYTES;
	mid = (size_t)((g2 - g1 - 1) * 2 * planes);
	for (y = r->y; y < r->y + r->h; y++, line += step) {
		unsigned short *p = (unsigned short *)line, *q;
		if (g1 == g2) {
			for (k = 0; k < planes; k++)
				p[k] &= (unsigned short)~(lm & rm);
			continue;
		}
		for (k = 0; k < planes; k++)
			p[k] &= (unsigned short)~lm;
		if (g2 - g1 > 1)
			memset(p + planes, 0, mid);
		q = p + (g2 - g1) * planes;
		for (k = 0; k < planes; k++)
			q[k] &= (unsigned short)~rm;
	}
}

/* the buffer's full copied to each visible piece of the window */
static void off_copy(GRECT *full)
{
	GRECT box;
	MFDB scr;
	memset(&scr, 0, sizeof(scr));
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		GRECT d = box;
		if (rect_and(&d, full)) {
			short pxy[8];
			pxy[0] = pxy[4] = d.x;
			pxy[1] = pxy[5] = d.y;
			pxy[2] = pxy[6] = d.x + d.w - 1;
			pxy[3] = pxy[7] = d.y + d.h - 1;
			/* the VDI clips raster copies to the last clip,
			   TOS's as NVDI's: the piece's own */
			clip_on(&d);
			vro_cpyfm(vdi_h, 3, pxy, &obuf_mfdb, &scr);	/* S_ONLY */
		}
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	clip_off();
}

/* ---- the view drawn in slices ----
 * A new page (or a calendar read) is drawn off the screen a band at a
 * time between event calls, the old picture staying until the new one
 * is whole and copied at once (the guidelines' Speed: a longer job runs
 * in slices, a key answered within 100 ms). A page key meanwhile begins
 * it again for the newest page; anything else that draws in the view
 * (a selection, a scroll, a redraw) has it finished first. Where there
 * is no buffer to draw in, the view is drawn at once, as before. */

/* a piece of the window drawn in the turns after, before the view's
   pieces (with a new window's toolbar and list, win_start) */
static void redraw_soon(const GRECT *a)
{
	if (vj.st_i >= vj.st_n)
		vj.st_i = vj.st_n = 0;
	if (vj.st_n < (short)(sizeof(vj.st_r) / sizeof(vj.st_r[0])))
		vj.st_r[vj.st_n++] = *a;
	else
		win_redraw((GRECT *)a);
}

/* a page moved: its title, and with buttons the view buttons, drawn
   at once, or in the slices after for a new view (the rest of the
   toolbar is as it was) */
void win_redraw_title(int buttons)
{
	GRECT a = r_title;
	a.y = r_tool.y;
	a.h = r_tool.h;
	if (buttons && vj.on) {
		/* a new view, drawn in slices: the buttons, then the title,
		   each in a slice of its own (both in the key's turn: 105 ms
		   on an ST) */
		GRECT b = a;
		b.x = but[B_DAY].x;
		b.w = but[B_YEAR].x + but[B_YEAR].w - b.x;
		a.w = b.x - a.x;
		redraw_soon(&b);
		redraw_soon(&a);
		return;
	}
	if (buttons)		/* (one redraw for both: each is an AES round and a copy) */
		a.w = but[B_YEAR].x + but[B_YEAR].w - a.x;
	win_redraw(&a);
}

/* the small month on the left drawn again in the next turn (the chosen
   day moved from day0): not in the turn that answers the click */
void win_mini_later(long day0)
{
	if (win_h <= 0)
		return;
	vj.mini_from = vj.mini ? -1 : day0;	/* (moved twice: all of it) */
	vj.mini = 1;
}

/* a piece of the view drawn again in the next turn: what a click leaves
   behind (the day no longer chosen), after what it brings */
void win_redraw_later(const GRECT *a)
{
	GRECT *l;
	if (win_h <= 0)
		return;
	if (vj.later < 4) {
		vj.later_r[vj.later++] = *a;
		return;
	}
	l = &vj.later_r[3];	/* (more: the last grows over them) */
	{
		short x2 = l->x + l->w, y2 = l->y + l->h;
		if (a->x + a->w > x2) x2 = a->x + a->w;
		if (a->y + a->h > y2) y2 = a->y + a->h;
		if (a->x < l->x) l->x = a->x;
		if (a->y < l->y) l->y = a->y;
		l->w = x2 - l->x;
		l->h = y2 - l->y;
	}
}

int win_view_busy(void)
{
	return vj.on || vj.mini || vj.later || sl_due || vj.st_i < vj.st_n || info_due;
}

void win_view_later(void)
{
	if (win_h <= 0 || win_iconified || !off_screen()) {
		vj.on = 0;
		win_redraw(&r_view);
		if (win_h > 0)
			side_redraw_mini();
		return;
	}
	if (!vj.on) {
		vj.t0 = pf_ms();
		vj.prep_ms = vj.draw_ms = 0;
	}
	vj.on = 1;
	vj.ready = 0;
	vj.later = 0;		/* (in the view drawn whole) */
}

/* a slice of ms or so: 1 while there is more */
#ifdef AES_PROFILE
#ifndef VIEWSLICE_MS
#define VIEWSLICE_MS 90
#endif
static void slice_said(const char *what, unsigned long t0)
{
	if (pf_ms() - t0 >= VIEWSLICE_MS) {
		char b[80];
		snprintf(b, sizeof(b), "VIEWSLICE %lu ms %s\n", pf_ms() - t0, what);
		nf_debug(b);
	}
}
#else
#define slice_said(w, t)
#endif

int win_view_step(unsigned long ms)
{
	unsigned long t0 = pf_ms();
	int drawn;
	if (info_due) {
		/* the info line a key or click set (win_status_later) */
		info_due = 0;
		if (win_h > 0)
			wind_set_str(win_h, WF_INFO, info);
		slice_said("the info line", t0);
		return win_view_busy();
	}
	if (vj.st_i < vj.st_n) {
		/* a new window: its toolbar and list a piece at a time, before
		   the view (win_start); a new view's buttons and title */
		GRECT a = vj.st_r[vj.st_i++];
		win_redraw(&a);
		slice_said("the window's start", t0);
		return 1;
	}
	if (!vj.on && vj.later) {
		/* one a slice, in the order asked (Tab: the event chosen,
		   then the one it left; 45 ms each on an ST) */
		GRECT a = vj.later_r[0];
		int i;
		vj.later--;
		for (i = 0; i < vj.later; i++)
			vj.later_r[i] = vj.later_r[i + 1];
		win_redraw(&a);
		slice_said("a piece left for later", t0);
		return win_view_busy();
	}
	if (!vj.on) {
		if (vj.mini) {
			if (vj.mini_from >= 0) {
				vj.mini = 0;
				side_redraw_mini_from(vj.mini_from);
			} else {
				/* (the range it marks moved) a half a slice */
				side_redraw_mini_half(vj.mini - 1);
				vj.mini = vj.mini == 1 ? 2 : 0;
			}
			slice_said("the small month", t0);
			return vj.mini || sl_due;
		}
		sliders_step();
		slice_said("a slider", t0);
		return sl_due;
	}
	if (win_h <= 0 || win_iconified || !off_screen()) {
		vj.on = 0;
		return 0;
	}
	while (!vj.ready) {
		unsigned long used = pf_ms() - t0, p0 = pf_ms();
		if (used >= ms)
			return 1;
		int more = views_prepare(ms - used);
		vj.prep_ms += pf_ms() - p0;
		if (!more) {
			vj.ready = 1;
			vj.nt = (short)views_tiles(vj.tile, 200);
			vj.ti = 0;
			vj.take = 1;
			vj.rows = 1;
			vj.row_ms = vj.piece_ms = 0;
			slice_said("ready, the pieces", t0);
			return 1;	/* (the pieces from the next slice) */
		}
		slice_said(views_phase, t0);
		if (pf_ms() - p0 >= 10)
			return 1;	/* (a phase that worked: the next, the layout say, in a slice of its own) */
	}
	vj.inside = 1;		/* (off the screen: no wind_update until the copy) */
	if (vj.ti >= vj.nt)
		goto whole;	/* (the copy alone: the last pieces had the slice before) */
	drawn = 0;
	/* as much as the last draw's time says fits in half the slice:
	   whole rows of pieces together where they fit (each draw has its
	   cost whatever its size), a row's pieces a few at a time where one
	   row is too much; one piece at least */
	while (vj.ti < vj.nt) {
		unsigned long p0 = pf_ms(), took, half = ms / 2;
		GRECT r = vj.tile[vj.ti];
		short k = 1, rows = 0, inrow = 1, j;
		for (j = vj.ti + 1; j < vj.nt && vj.tile[j].y == r.y; j++)
			inrow++;	/* (the pieces from here to the row's end) */
		/* whole rows only when the dearest piece yet, as many times as a
		   row has pieces, fits (a job begins with pieces one by one) */
		vj.rows = (short)(vj.piece_ms ? half / (vj.piece_ms * inrow) : 0);
		if (r.x == r_view.x && vj.rows > 0) {
			/* whole rows: from here down while the guess allows */
			r.w = r_view.w;
			k = inrow;
			rows = 1;
			while (rows < vj.rows && vj.ti + k < vj.nt) {
				short y = vj.tile[vj.ti + k].y, n = 0;
				for (j = vj.ti + k; j < vj.nt && vj.tile[j].y == y; j++)
					n++;
				r.h = vj.tile[vj.ti + k].y + vj.tile[vj.ti + k].h - r.y;
				k += n;
				rows++;
			}
		} else {
			while (k < vj.take && k < inrow) {
				r.w += vj.tile[vj.ti + k].w;
				k++;
			}
		}
		/* not begun when the dearest yet says it would end past the
		   slice (one drawn already: each slice draws one at least) */
		if (drawn && pf_ms() - t0 + (rows ? vj.row_ms * rows : vj.piece_ms * k) > ms)
			break;
		drawn = 1;
		vj.ti += k;
		off_draw(&r);
		if (vj.show) {
			/* a new window: the band on the screen at once, as
			   EContact's (Erez, 31 Oct 2026) */
			wind_update(BEG_UPDATE);
			graf_mouse(M_OFF, 0);
			off_copy(&r);
			graf_mouse(M_ON, 0);
			wind_update(END_UPDATE);
		}
		took = pf_ms() - p0;
		vj.draw_ms += took;
		if (took < 5)
			took = 5;
		/* by the dearest row and piece yet (rows differ: a header is
		   cheap, a busy week dear) */
		if (rows) {
			unsigned long per = took / rows;	/* a row's time */
			if (per > vj.row_ms)
				vj.row_ms = per;
			if (took / k > vj.piece_ms)
				vj.piece_ms = took / k;
		} else if (took / k > vj.piece_ms) {
			vj.piece_ms = took / k;
		}
		if (vj.piece_ms < 1)
			vj.piece_ms = 1;
		vj.take = (short)(half / vj.piece_ms > 1 ? half / vj.piece_ms : 1);
		if (pf_ms() - t0 + took > ms)
			break;		/* (one more like it would not end inside the slice) */
	}
#ifdef AES_PROFILE
	{
		char w[40];
		snprintf(w, sizeof(w), "pieces to %d of %d", vj.ti, vj.nt);
		slice_said(w, t0);
	}
#endif
	if (vj.ti >= vj.nt && !vj.show && pf_ms() - t0 + (vj.copy_ms ? vj.copy_ms : ms / 2) > ms) {
		vj.inside = 0;
		return 1;	/* the copy in the next slice, where it fits */
	}
whole:
	if (vj.ti >= vj.nt) {
		GRECT full = work;
#ifdef AES_PROFILE
		unsigned long c0 = pf_ms();
#endif
		unsigned long k0 = pf_ms();
		if (!vj.show) {	/* (a new window's: each band copied already) */
			wind_update(BEG_UPDATE);
			graf_mouse(M_OFF, 0);
			if (rect_and(&full, &r_view))
				off_copy(&full);
			graf_mouse(M_ON, 0);
			wind_update(END_UPDATE);
			vj.copy_ms = pf_ms() - k0;	/* (what the next one is judged by) */
		}
		vj.show = 0;
		vj.on = 0;
		vj.mini = 1;		/* the small month, in the next slice */
		vj.mini_from = -1;
		{
			/* with the log on, what each view cost on this machine
			   (Erez's Falcon is slower than the emulator: the numbers
			   from there say what to make faster). Numbers only */
			char b[120];
			static const char *const vname[] = { "day", "week", "month", "year" };
			int n = snprintf(b, sizeof(b), "the %s drawn in %lu ms: the events %lu, drawing %lu, "
					 "the copy %lu (%d shown)", vname[cur_view & 3], pf_ms() - vj.t0, vj.prep_ms, vj.draw_ms,
					 pf_ms() - k0, views_count());
			conn_log("ECal", " -- ", b, n);
		}
#ifdef AES_PROFILE
		{
			char b[64];
			snprintf(b, sizeof(b), "VIEWDONE %lu ms (the copy %lu)\n", pf_ms() - vj.t0, pf_ms() - c0);
			nf_debug(b);
		}
#endif
	}
	vj.inside = 0;
	if (!vj.on)
		win_sliders();	/* (set in the slices after) */
	return vj.on;
}

/* what is left of it, now: before anything else draws in the view */
void win_view_finish(void)
{
	while (win_view_step(0xffffffffUL))
		;
}

/* the first redraw of a window just opened, in slices (Erez, 31 Oct
   2026: drawn whole it was one stretch of 575 ms on an ST): white at
   once, then the toolbar in three pieces and the list in three, a piece
   a slice, and the view in bands, each on the screen as it is drawn, as
   EContact's. 0 where it can't be: drawn whole as before */
static int win_start(void)
{
	GRECT box, r;
	short k, n = 0;
	if (win_h <= 0 || win_iconified)
		return 0;
	win_layout();
	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, 0);
	wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
	while (box.w && box.h) {
		r = box;
		if (rect_and(&r, &work)) {
			clip_on(&r);
			fill(&r, 0);
		}
		wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
	}
	clip_off();
	graf_mouse(M_ON, 0);
	wind_update(END_UPDATE);
	{
		/* the toolbar cut at the title's edges: the buttons before it,
		   the title, the rest in two (in thirds the first, buttons and
		   title, took an ST 100 ms) */
		short x[5];
		x[0] = r_tool.x;
		x[1] = r_title.x;
		x[2] = but[B_DAY].x;
		x[4] = r_tool.x + r_tool.w;
		x[3] = x[2] + (x[4] - x[2]) / 2;
		for (k = 0; k < 4; k++) {
			if (x[k + 1] <= x[k])
				continue;
			r = r_tool;
			r.x = x[k];
			r.w = x[k + 1] - x[k];
			vj.st_r[n++] = r;
		}
	}
	for (k = 0; k < 3 && r_side.w > 0; k++) {
		r = r_side;
		r.y = r_side.y + (short)((long)r_side.h * k / 3);
		r.h = r_side.y + (short)((long)r_side.h * (k + 1) / 3) - r.y;
		vj.st_r[n++] = r;
	}
	vj.st_n = n;
	vj.st_i = 0;
	win_view_later();	/* the view after them, off the screen in slices, */
	vj.show = vj.on;	/* each band shown as it is drawn */
	return 1;
}

void win_redraw(GRECT *area)
{
	GRECT box, full;
	int took;
	if (win_h <= 0)
		return;
	if (win_iconified) {
		icon_window(area);	/* over the small window, not the old work area */
		return;
	}
	if (vj.on && !vj.inside) {
		GRECT v = r_view;
		if (!area || rect_and(&v, area))
			win_view_finish();	/* (the view whole first) */
	}
	/* (common's press_update: TOS's AES holds BEG_UPDATE back while the
	   left button is held still, and a press's row drew only at its
	   release; the guidelines 5.8, 60d1ea5) */
	took = press_update_begin();
	graf_mouse(M_OFF, 0);
	full = work;
	if (area && !rect_and(&full, area)) {
		graf_mouse(M_ON, 0);
		press_update_end(took);
		return;
	}
	if (off_screen()) {
		/* all of it off the screen, then each visible piece copied */
#ifdef AES_PROFILE
		unsigned long t0 = pf_ms(), t1;
#endif
		off_draw(&full);
#ifdef AES_PROFILE
		t1 = pf_ms();
#endif
		off_copy(&full);
#ifdef AES_PROFILE
		if (pf_ms() - t0 >= 20) {
			char b[100];
			snprintf(b, sizeof(b), "REDRAWMS %dx%d off the screen: toolbar %lu side %lu all %lu copy %lu\n",
				 full.w, full.h, ms_tool, ms_side, t1 - t0, pf_ms() - t1);
			nf_debug(b);
		}
#endif
	} else {
#ifdef AES_PROFILE
		unsigned long t0 = pf_ms();
#endif
		wind_get(win_h, WF_FIRSTXYWH, &box.x, &box.y, &box.w, &box.h);
		while (box.w && box.h) {
			GRECT d = box;
			if (rect_and(&d, &full))
				draw_all(&d);
			wind_get(win_h, WF_NEXTXYWH, &box.x, &box.y, &box.w, &box.h);
		}
#ifdef AES_PROFILE
		if (pf_ms() - t0 >= 20) {
			char b[100];
			snprintf(b, sizeof(b), "REDRAWMS %dx%d on the screen: toolbar %lu side %lu all %lu\n",
				 full.w, full.h, ms_tool, ms_side, pf_ms() - t0);
			nf_debug(b);
		}
#endif
	}
	graf_mouse(M_ON, 0);
	press_update_end(took);
	if (!views_gliding())
		win_sliders();	/* (gliding: when it stops, views_glide; 45 ms a step on an ST) */
}

short win_popup(short x, short y, const char *const *labels, short n)
{
	short k = popup(x, y, labels, n);
	win_redraw(&popup_area);
	return k;
}

/* where the window goes so its work area starts on a multiple of 8
   across (the guidelines' Speed: text on a byte), common's wind_x8 */
static short snap_x(short x, short y, short w, short h)
{
	return wind_x8(kind(), x, y, w, h);
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
	if (w < MIN_COLS * cw || h < MIN_ROWS * ch || x < desk_x || y < desk_y ||
	    x + w > desk_x + desk_w + 8 || y + h > desk_y + desk_h + 8) {
		/* the first run (or a place that no longer fits): the whole
		   desk, as every main window (D39, W11) */
		x = desk_x;
		y = desk_y;
		w = desk_w;
		h = desk_h;
	}
	{
		/* snapped right, it must still end on the screen */
		short sx = snap_x(x, y, w, h);
		if (sx + w > desk_x + desk_w)
			w = desk_x + desk_w - sx;
		x = sx;
	}
	wind_open(win_h, x, y, w, h);
	sl_size = sl_pos = -1;	/* a new window's sliders */
	start_fresh = 1;	/* (its first redraw: win_start) */
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
#ifdef AES_PROFILE
	unsigned long sc0 = pf_ms(), sc1 = 0, sc2 = 0;
#endif
	if (win_h <= 0 || win_iconified)
		return;
	win_view_finish();	/* (what is on the screen is moved: the view whole) */
	under_kept = 0;		/* (moved on the screen, not in the buffer) */
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
		short pxy[8];
		pxy[0] = pxy[4] = a->x;
		pxy[2] = pxy[6] = a->x + a->w - 1;
		pxy[1] = a->y + (dy > 0 ? ady : 0);
		pxy[3] = pxy[1] + a->h - ady - 1;
		pxy[5] = a->y + (dy > 0 ? 0 : ady);
		pxy[7] = pxy[5] + a->h - ady - 1;
		GRECT in = *a;		/* the strip that comes in */
		in.h = ady;
		if (dy > 0)
			in.y = a->y + a->h - ady;
		graf_mouse(M_OFF, 0);
		if (off_screen() && rect_and(&in, &work)) {
			/* drawn off the screen first, then the rows moved and the
			   strip copied in at once: before, the rows were moved and
			   the strip drawn after, and until it was the old rows
			   showed in it a second time (Erez's video, 13 Nov 2026:
			   "a previous drawing and only then the new one", plain
			   on a Falcon in 256 colours) */
			off_draw(&in);
			clip_on(&strip);
#ifdef AES_PROFILE
			sc1 = pf_ms();
#endif
			scr_rows(vdi_h, pxy);
#ifdef AES_PROFILE
			sc2 = pf_ms();
#endif
			off_copy(&in);
			graf_mouse(M_ON, 0);
			wind_update(END_UPDATE);
#ifdef AES_PROFILE
			{
				char b[80];
				snprintf(b, sizeof(b), "SCROLLMS %dx%d by %d: strip first %lu, rows %lu, strip copied %lu\n",
					 a->w, a->h, dy, sc1 - sc0, sc2 - sc1, pf_ms() - sc2);
				nf_debug(b);
			}
#endif
			if (!views_gliding())
				win_sliders();
			return;
		}
		/* the VDI clips raster copies to the last clip, TOS's as NVDI's:
		   the area moved, not one left by the last thing drawn */
		clip_on(&strip);
#ifdef AES_PROFILE
		sc1 = pf_ms();
#endif
		scr_rows(vdi_h, pxy);	/* (on TOS's screen by the CPU, else vro_cpyfm S_ONLY) */
#ifdef AES_PROFILE
		sc2 = pf_ms();
#endif
		clip_off();
		graf_mouse(M_ON, 0);
		strip.h = ady;
		if (dy > 0)
			strip.y = a->y + a->h - ady;
	}
	wind_update(END_UPDATE);
#ifdef AES_PROFILE
	{
		unsigned long c1 = pf_ms();
		char b[80];
		win_redraw(&strip);
		snprintf(b, sizeof(b), "SCROLLMS %dx%d by %d: before %lu, rows %lu (fast %d), after %lu, strip %lu\n",
			 a->w, a->h, dy, sc1 - sc0, sc2 - sc1, text_plain_state(), c1 - sc2, pf_ms() - c1);
		nf_debug(b);
	}
#else
	win_redraw(&strip);
#endif
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
		if (start_fresh) {
			/* the AES's first redraw of the window just opened */
			start_fresh = 0;
			if (off_screen() && win_start())
				break;
		}
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
		{
			/* on 8 across, as when moved, and still on the screen */
			short sx = snap_x(fx, fy, fw, fh);
			if (sx + fw > desk_x + desk_w)
				fw = desk_x + desk_w - sx;
			fx = sx;
		}
		wind_set(win_h, WF_CURRXYWH, fx, fy, fw, fh);
		win_layout();
		win_redraw(0);
		remember_place();
		break;
	}
	case WM_SIZED:
	case WM_MOVED: {
		short w = msg[6] < MIN_COLS * cw ? MIN_COLS * cw : msg[6], h = msg[7] < MIN_ROWS * ch ? MIN_ROWS * ch : msg[7];
		short sx = snap_x(msg[4], msg[5], w, h);
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

#ifdef AES_PROFILE
/* the speed rig's press and release (the guidelines 5.8: press to row
   drawn, release to the action timed apart), from the click's coming */
static unsigned long press_t0;
static int press_said;

void press_begin(void)
{
	press_t0 = pf_ms();
	press_said = 0;
}

void press_drawn(void)
{
	char b[40];
	if (!press_t0 || press_said)
		return;
	press_said = 1;
	snprintf(b, sizeof(b), "AESPRESS %lu ms\n", pf_ms() - press_t0);
	nf_debug(b);
}

static void press_released(void)
{
	char b[40];
	if (!press_t0)
		return;
	snprintf(b, sizeof(b), "AESRELEASE %lu ms\n", pf_ms() - press_t0);
	nf_debug(b);
}
#else
#define press_released()
#endif

/* A button ECal draws itself, pressed (the guidelines 5.8): shown in
   inverse while the button is held over it (inset: inside its frame, as
   GEM's own buttons look selected), its action when let go over it (1),
   nothing elsewhere (0). The wait is common's press_track (ui_track),
   the background going on meanwhile */
static GRECT held;		/* the button held, inverted when shown */
static short held_on = -1;	/* 1 while it is shown pressed, -1 no button held */

static void held_flip(void)
{
	int took = press_update_begin();	/* (not BEG_UPDATE: held back on TOS while the button is down) */
	graf_mouse(M_OFF, 0);
	clip_on(&held);
	xor_fill(&held);
	clip_off();
	graf_mouse(M_ON, 0);
	press_update_end(took);
	held_on = !held_on;
}

/* each turn of the wait: pressed while the mouse is over it */
void win_track_idle(void)
{
	short x, y, mb, k;
	if (held_on < 0)
		return;
	graf_mkstate(&x, &y, &mb, &k);
	if (rect_hit(&held, x, y) != held_on)
		held_flip();
}

int win_press_held(const GRECT *b, short inset, short mx, short my)
{
	GRECT desk;
	int k;
	held = *b;
	held.x += inset;
	held.y += inset;
	held.w -= 2 * inset;
	held.h -= 2 * inset;
	wind_get(0, WF_WORKXYWH, &desk.x, &desk.y, &desk.w, &desk.h);
	if (!rect_and(&held, &work) || !rect_and(&held, &desk))
		return 0;
	held_on = 0;
	held_flip();	/* pressed, at once */
	press_drawn();
	k = ui_track(b, mx, my);
	press_released();
	if (held_on > 0)
		held_flip();	/* (as it was: what the action draws comes after) */
	held_on = -1;
	return k == PRESS_IN;
}

/* what a press at (mx, my) is on, for press_double: a double click is
   two presses on the same one */
short win_press_id(short mx, short my)
{
	short i;
	if (win_h <= 0 || !rect_hit(&work, mx, my))
		return 0;
	if (rect_hit(&r_tool, mx, my)) {
		for (i = 0; i < NBUT; i++)
			if (rect_hit(&but[i], mx, my))
				return (short)(3000 + i);
		return 3100;	/* (the title) */
	}
	if (r_side.w && rect_hit(&r_side, mx, my))
		return side_press_id(mx, my);
	if (rect_hit(&r_view, mx, my))
		return views_press_id(mx, my);
	return 0;
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
		/* a button: pressed while held, its action on the release over
		   it, nothing elsewhere (the guidelines 5.8) */
		if (i < NBUT && !(button & 2) && !win_press_held(&but[i], 1, mx, my))
			return 1;
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
			if (rect_hit(&r_title, mx, my) && clicks == 2 && ui_track(&r_title, mx, my) == PRESS_IN)
				dlg_goto();	/* (the double click's on its release) */
			else if (rect_hit(&r_title, mx, my))
				ui_track(&r_title, mx, my);	/* (its release seen: press_double's time) */
		}
		return 1;
	}
	if (r_side.w && rect_hit(&r_side, mx, my))
		return side_click(mx, my, clicks, button);
	if (!(button & 2) && views_divider_at(mx, my))
		return views_drag_divider(mx, my);
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
		case 0x21: find_dialog(); return 1;			/* ^F */
		case 0x22: dlg_goto(); return 1;			/* ^G */
		case 0x16:					/* ^U: close (the guidelines, 3.2) */
		case 0x10:					/* ^Q */
			if (!is_acc)
				quit_now = 1;
			win_close();
			return 1;
		case 0x11:					/* ^W: the next window; ECal has one */
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
