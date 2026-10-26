/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * draw.c - drawing for ECal's window: rectangles, lines, the colour
 * (or pattern) of each calendar, and text. Text is kept in logical
 * order; lines with Hebrew go through bidi_visual() and, when the
 * paragraph is right-to-left, can be aligned to the right edge (from
 * EMail's draw.c).
 */
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "bidi.h"
#include "conn.h"

static short next_text_color = 1;

/* VDI attributes as last set: each call is a trap, and on an 8 MHz ST
   setting them again for every piece of text cost more than the text */
static short c_wr = -1, c_fint = -1, c_fsty = -1, c_fcol = -1, c_lcol = -1, c_tcol = -1, c_teff = -1, c_per = -1;

/* the attributes above are set again at the next use: after drawing
   with another VDI handle (win.c's off-screen bitmap) */
void draw_forget(void)
{
	c_wr = c_fint = c_fsty = c_fcol = c_lcol = c_tcol = c_teff = c_per = -1;
}

void set_wrmode(short m)
{
	if (m != c_wr)
		vswr_mode(vdi_h, c_wr = m);
}

static void set_fill(short interior, short style, short color)
{
	if (interior != c_fint)
		vsf_interior(vdi_h, c_fint = interior);
	if (interior >= 2 && style != c_fsty)
		vsf_style(vdi_h, c_fsty = style);
	if (color != c_fcol)
		vsf_color(vdi_h, c_fcol = color);
	if (c_per != 0)
		vsf_perimeter(vdi_h, c_per = 0);
}

static void set_lcolor(short c)
{
	if (c != c_lcol)
		vsl_color(vdi_h, c_lcol = c);
}

static void set_text(short color, short effects)
{
	if (color != c_tcol)
		vst_color(vdi_h, c_tcol = color);
	if (effects != c_teff)
		vst_effects(vdi_h, c_teff = effects);
}

void clip_on(GRECT *r)
{
	short pxy[4];
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	vs_clip(vdi_h, 1, pxy);
}

void clip_off(void)
{
	short pxy[4] = { 0, 0, 0, 0 };
	pxy[2] = scr_w - 1;
	pxy[3] = scr_h - 1;
	vs_clip(vdi_h, 0, pxy);
}

int rect_hit(const GRECT *r, short x, short y)
{
	return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int rect_and(GRECT *a, const GRECT *b)
{
	short x1 = a->x > b->x ? a->x : b->x, y1 = a->y > b->y ? a->y : b->y;
	short x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
	short y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
	a->x = x1;
	a->y = y1;
	a->w = x2 - x1;
	a->h = y2 - y1;
	return a->w > 0 && a->h > 0;
}

/* Bitmaps the VDI copies to the screen must be in ST RAM: a blitter
   can't reach TT/Fast RAM, where the program itself is loaded when there
   is some. One small block, taken at start-up (an accessory can't take
   memory later); ST RAM is all there is when Mxalloc is missing. */
static unsigned short st_fallback[512];
static unsigned short *st_buf;

void st_ram_init(void)
{
	void *p = Mxalloc(sizeof(st_fallback), 0);
	st_buf = (long)p > 0 ? p : st_fallback;
}

void *st_ram(long n)
{
	if (!st_buf)
		st_ram_init();
	return n <= (long)sizeof(st_fallback) ? st_buf : 0;
}

/* small marks drawn on events: a loop for a repeating one, a bell for
   one with an alert; 16x16 for tall fonts, 8x8 for small ones */
static const unsigned short ic16[2][16] = {
	{ 0x0000, 0x07A0, 0x1FB0, 0x3038, 0x603C, 0x6020, 0xC000, 0xC003,
	  0xC003, 0xC003, 0x6006, 0x6006, 0x381C, 0x1FF8, 0x07E0, 0x0000 },
	{ 0x0000, 0x0180, 0x07E0, 0x0FF0, 0x0FF0, 0x0FF0, 0x1FF8, 0x1FF8,
	  0x1FF8, 0x3FFC, 0x3FFC, 0x7FFE, 0xFFFF, 0x0000, 0x03C0, 0x0180 }
};
static const unsigned short ic8[2][8] = {
	{ 0x3400, 0x4600, 0x8700, 0x8000, 0x8100, 0x4200, 0x3C00, 0x0000 },
	{ 0x1800, 0x3C00, 0x3C00, 0x7E00, 0x7E00, 0xFF00, 0x1800, 0x0000 }
};

short mark_w(void)
{
	return ch >= 14 ? 16 : 8;
}

void draw_mark(short x, short y, short n, int which, short color, const GRECT *clip)
{
	MFDB src, dst;
	short pxy[8], colors[2];
	unsigned short *bits = st_ram(32);
	/* the VDI's raster copy may not clip: only the part inside is copied,
	   so a scroll that draws a strip draws its piece of the mark */
	short x0 = x > clip->x ? x : clip->x, y0 = y > clip->y ? y : clip->y;
	short x1 = x + n < clip->x + clip->w ? x + n : clip->x + clip->w;
	short y1 = y + n < clip->y + clip->h ? y + n : clip->y + clip->h;
	if (!bits || x0 >= x1 || y0 >= y1)
		return;
	memcpy(bits, n == 16 ? ic16[which] : ic8[which], n * 2);
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = bits;
	src.fd_w = 16;
	src.fd_h = n;
	src.fd_wdwidth = 1;
	src.fd_nplanes = 1;
	pxy[0] = x0 - x;
	pxy[1] = y0 - y;
	pxy[2] = x1 - x - 1;
	pxy[3] = y1 - y - 1;
	pxy[4] = x0;
	pxy[5] = y0;
	pxy[6] = x1 - 1;
	pxy[7] = y1 - 1;
	colors[0] = color;
	colors[1] = 0;
	vrt_cpyfm(vdi_h, 2, pxy, &src, &dst, colors);	/* transparent */
}

void fill_pat(GRECT *r, short color, short interior, short style)
{
	short pxy[4];
	if (r->w <= 0 || r->h <= 0)
		return;
	pxy[0] = r->x;
	pxy[1] = r->y;
	pxy[2] = r->x + r->w - 1;
	pxy[3] = r->y + r->h - 1;
	set_fill(interior, style, color);
	set_wrmode(interior >= 2 ? 2 : 1);	/* patterns over what is there */
	vr_recfl(vdi_h, pxy);
}

void fill(GRECT *r, short color)
{
	fill_pat(r, color, 1, 0);
}

void line_col(short x1, short y1, short x2, short y2, short color)
{
	short pxy[4];
	pxy[0] = x1;
	pxy[1] = y1;
	pxy[2] = x2;
	pxy[3] = y2;
	set_wrmode(1);
	set_lcolor(color);
	v_pline(vdi_h, 2, pxy);
}

void hline(short x1, short x2, short y)
{
	line_col(x1, y, x2, y, 1);
}

void vline(short x, short y1, short y2)
{
	line_col(x, y1, x, y2, 1);
}

void frame(GRECT *r, short color)
{
	short p[10];
	p[0] = r->x;
	p[1] = r->y;
	p[2] = r->x + r->w - 1;
	p[3] = r->y;
	p[4] = p[2];
	p[5] = r->y + r->h - 1;
	p[6] = r->x;
	p[7] = p[5];
	p[8] = r->x;
	p[9] = r->y;
	set_wrmode(1);
	set_lcolor(color);
	v_pline(vdi_h, 5, p);
}

/* an outline drawn in XOR: drawn twice it is gone (a dragged event) */
void xor_frame(GRECT *r)
{
	short p[10];
	p[0] = p[6] = p[8] = r->x;
	p[1] = p[3] = p[9] = r->y;
	p[2] = p[4] = r->x + r->w - 1;
	p[5] = p[7] = r->y + r->h - 1;
	set_wrmode(3);
	set_lcolor(1);
	v_pline(vdi_h, 5, p);
	p[0] = p[6] = p[8] = r->x + 1;
	p[1] = p[3] = p[9] = r->y + 1;
	p[2] = p[4] = r->x + r->w - 2;
	p[5] = p[7] = r->y + r->h - 2;
	if (r->w > 4 && r->h > 4)
		v_pline(vdi_h, 5, p);
	set_wrmode(1);
}

short red_color(void)
{
	return ncolors >= 4 ? 2 : 1;
}

static short pens_ready;

/* the two greys for text and lines, found in the screen's own palette
   (pens_init); black where there are none */
static short grey_dark = 1, grey_light = 1;
static void pens_init(void);

short on_3d;

short grey_color(void)
{
	if (ncolors < 16)
		return 1;
	text_grey(0);
	if (on_3d)
		return grey_dark;
	return grey_light != 1 ? grey_light : 8;
}

short text_grey(int pale)
{
	if (ncolors < 16)
		return 1;
	if (!pens_ready)
		pens_init();
	return pale && !on_3d ? grey_light : grey_dark;
}

/* the Mac's colours (red orange yellow green blue purple brown grey) on
   the standard VDI palette; on 4 colours and mono, patterns tell the
   calendars apart */
void pen_of(int cal, PEN *p)
{
	pen_color((cal >= 0 && cal < ncals) ? cals[cal].color : 4, p);
}

/* the calendars' twelve colours, in VDI's 0-1000 RGB: saturated, so
   they stay bright on the screen's nearest colours */
static const short mac_rgb[NCOLORS][3] = {
	{ 1000, 156, 156 },
	{ 1000, 549, 0 },
	{ 1000, 843, 0 },
	{ 156, 745, 235 },
	{ 0, 431, 1000 },
	{ 588, 235, 862 },
	{ 588, 392, 196 },
	{ 549, 549, 549 },
	{ 1000, 392, 705 },
	{ 0, 705, 745 },
	{ 78, 117, 549 },
	{ 0, 431, 156 },
};
static short c16[NCOLORS] = { 2, 10, 6, 3, 4, 15, 14, 8, 7, 5, 12, 11 };

/* TOS's own 16 colours on a TT or Falcon (VDI's 0-1000), as the
   calendars were made to look */
static const short tos_pal[16][3] = {
	{ 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
	{ 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
	{ 733, 733, 733 }, { 533, 533, 533 }, { 667, 0, 0 }, { 0, 667, 0 },
	{ 0, 0, 667 }, { 0, 667, 667 }, { 667, 667, 0 }, { 667, 0, 667 },
};

/* the two greys for text in palette pal: a real grey (red, green and
   blue alike) nearest each target, taken from the calendars' pens */
static void pick_greys(const short (*pal)[3], unsigned char *taken, short *dark, short *light)
{
	static const short want[2] = { 420, 690 };	/* dark, light (0-1000) */
	short g, i;
	*dark = *light = 1;
	for (g = 0; g < 2; g++) {
		short pick = -1;
		long best = 0x7fffffffL;
		for (i = 2; i < 16; i++) {
			short lo, hi;
			long dist;
			if (taken[i])
				continue;
			lo = pal[i][0] < pal[i][1] ? pal[i][0] : pal[i][1];
			lo = lo < pal[i][2] ? lo : pal[i][2];
			hi = pal[i][0] > pal[i][1] ? pal[i][0] : pal[i][1];
			hi = hi > pal[i][2] ? hi : pal[i][2];
			if (hi - lo > 120 || hi < 200 || lo > 900)
				continue;	/* not a grey, or too near black or white (NVDI's index 8 is 851) */
			dist = (long)(pal[i][0] + pal[i][1] + pal[i][2]) / 3 - want[g];
			dist = dist < 0 ? -dist : dist;
			if (dist < best) {
				best = dist;
				pick = i;
			}
		}
		if (pick >= 0) {
			taken[pick] = 1;
			if (g)
				*light = pick;
			else
				*dark = pick;
		}
	}
	if (*dark == 1 && *light != 1)
		*dark = *light;		/* only one grey: for both */
	if (*light == 1 && *dark != 1)
		*light = *dark;		/* never black for the light grey */
}

static long colour_dist(const short *a, const short *b)
{
	long dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
	return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

/* each colour of want[] its pen in palette pal: the most distinctive
   first, each the nearest still free, so no two calendar colours look
   the same (on 16 colours purple, brown and grey all came out the same
   grey). With swap, then two colours trade pens while that brings both
   nearer together: NVDI's light cyan, nearest to TOS's dark cyan, would
   otherwise go to the blue calendar and leave the teal one dark */
static void pick_pens(const short (*want)[3], const short (*pal)[3], unsigned char *taken, short *pen, int swap)
{
	static const short order[NCOLORS] = { 0, 2, 3, 4, 5, 9, 8, 10, 11, 1, 6, 7 };
	short j, i, again;
	for (j = 0; j < NCOLORS; j++) {
		short k = order[j], pick = -1;
		long best = 0x7fffffffL;
		pen[k] = -1;
		for (i = 2; i < 16; i++) {
			long dist;
			if (taken[i])
				continue;
			dist = colour_dist(pal[i], want[k]);
			if (dist < best) {
				best = dist;
				pick = i;
			}
		}
		if (pick >= 0) {
			pen[k] = pick;
			taken[pick] = 1;
		}
	}
	for (again = swap; again; ) {
		again = 0;
		for (j = 0; j < NCOLORS; j++)
			for (i = j + 1; i < NCOLORS; i++)
				if (pen[j] >= 0 && pen[i] >= 0 &&
				    colour_dist(pal[pen[i]], want[j]) + colour_dist(pal[pen[j]], want[i]) <
				    colour_dist(pal[pen[j]], want[j]) + colour_dist(pal[pen[i]], want[i])) {
					short t = pen[j];
					pen[j] = pen[i];
					pen[i] = t;
					again = 1;
				}
	}
}

/* Only among the first 16 pens, the standard palette, in 256 colours
   and more too (other programs set the pens above 15 as they please):
   the calendars and the grey text look the same whichever the screen
   has. Those pens' colours differ, though: NVDI's index 14 is khaki
   where TOS's is olive, its 7 pink where TOS's is magenta, and the
   user may have changed them. So each calendar first finds the pen it
   gets in TOS's own palette, then takes, from the screen's real
   colours, the pen nearest to that one: under NVDI as under TOS. */
static void pens_init(void)
{
	short i, j, pal[16][3], want[NCOLORS][3], tos_pen[NCOLORS], d, l;
	unsigned char taken[16];
	pens_ready = 1;
	if (ncolors < 16)
		return;
	/* how the calendars look under TOS */
	memset(taken, 0, sizeof(taken));
	pick_greys(tos_pal, taken, &d, &l);
	pick_pens(mac_rgb, tos_pal, taken, tos_pen, 0);
	for (j = 0; j < NCOLORS; j++)
		memcpy(want[j], tos_pen[j] >= 0 ? tos_pal[tos_pen[j]] : mac_rgb[j], sizeof(want[j]));
	/* and on this screen */
	for (i = 0; i < 16; i++)
		vq_color(vdi_h, i, pal[i]);
	memset(taken, 0, sizeof(taken));
	pick_greys((const short (*)[3])pal, taken, &grey_dark, &grey_light);
	pick_pens((const short (*)[3])want, (const short (*)[3])pal, taken, tos_pen, 1);
	for (j = 0; j < NCOLORS; j++)
		if (tos_pen[j] >= 0)
			c16[j] = tos_pen[j];
	{
		/* for the log: the screen's 16 pens, and the one each calendar
		   colour and the two greys took */
		char m[400];
		int n = snprintf(m, sizeof(m), "pens:");
		for (i = 0; i < 16 && n < (int)sizeof(m) - 16; i++)
			n += snprintf(m + n, sizeof(m) - n, " %d=%d,%d,%d", i, pal[i][0], pal[i][1], pal[i][2]);
		n += snprintf(m + n, sizeof(m) - n, "; calendars");
		for (j = 0; j < NCOLORS && n < (int)sizeof(m) - 6; j++)
			n += snprintf(m + n, sizeof(m) - n, " %d", c16[j]);
		snprintf(m + n, sizeof(m) - n, "; greys %d %d", grey_dark, grey_light);
		conn_log("ECal", "--", m, (long)strlen(m));
	}
}

void pen_color(short k, PEN *p)
{
	/* 4 colours (white, black, red, green) and mono: patterns tell them apart */
	static const short c4[NCOLORS] = { 2, 2, 3, 3, 1, 1, 2, 1, 2, 3, 1, 3 };
	static const short int4[NCOLORS] = { 2, 3, 2, 2, 3, 3, 3, 3, 2, 3, 2, 3 };
	static const short sty4[NCOLORS] = { 2, 9, 3, 2, 7, 8, 1, 9, 6, 3, 4, 12 };
	static const short mono_int[NCOLORS] = { 2, 3, 2, 3, 2, 3, 3, 2, 3, 2, 3, 2 };
	static const short mono_sty[NCOLORS] = { 2, 7, 1, 8, 3, 9, 12, 4, 1, 6, 3, 5 };
	if (k < 0 || k >= NCOLORS)
		k = 4;
	if (!pens_ready)
		pens_init();
	if (ncolors >= 16) {
		p->color = c16[k];
		p->interior = 2;
		p->style = k == 2 ? 3 : 2;	/* yellow is pale: a little more */
	} else if (ncolors >= 4) {
		p->color = c4[k];
		p->interior = int4[k];
		p->style = sty4[k];
	} else {
		p->color = 1;
		p->interior = mono_int[k];
		p->style = mono_sty[k];
	}
}

short line_rtl(const char *s, long n)
{
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	return bidi_has_rtl(s, (short)n) && bidi_is_rtl(s, (short)n);
}

/* Some fonts have the Hebrew letters where ISO-8859-8 or DOS 862 put
   them; Settings says which, and the letters move there just before
   drawing (from EMail). */
static const unsigned char heb_order[27] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	17, 18, 20, 22, 23, 24, 25, 26,
	15, 10, 13, 19, 21
};

void heb_font_map(char *s, long n, short where)
{
	long i;
	unsigned char base = where == HEB_ISO ? 0xE0 : 0x80;
	if (where != HEB_ISO && where != HEB_DOS)
		return;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xC2 && c <= 0xDC)
			s[i] = (char)(base + heb_order[c - 0xC2]);
	}
}

static int has_heb(const char *s, long n)
{
	long i;
	for (i = 0; i < n; i++)
		if ((unsigned char)s[i] >= 0xC2 && (unsigned char)s[i] <= 0xDC)
			return 1;
	return 0;
}

static int is_heb(unsigned char c)
{
	return c >= 0xC2 && c <= 0xDC;
}

/* a vector font: the Hebrew letters by the font's own indices (NVDI's
   map mode 0), or in the system font, one cell each; the rest as usual */
static void runs_at(short x, short y, const char *s, short n)
{
	short i = 0;
	while (i < n) {
		short j = i, heb = is_heb((unsigned char)s[i]);
		while (j < n && is_heb((unsigned char)s[j]) == heb && j - i < 120)
			j++;
		if (!heb) {
			v_gtext_n(vdi_h, x + i * cw, y, s + i, j - i);
		} else if (heb_mode == HM_UNICODE) {
			unsigned short w[120];
			short k;
			for (k = i; k < j; k++)
				w[k - i] = heb_idx[(unsigned char)s[k] - 0xC2];
			vst_map_mode(vdi_h, 0);
			v_gtext_w(vdi_h, x + i * cw, y, w, j - i);
			vst_map_mode(vdi_h, 1);
		} else {
			/* the system font, each letter centred in its cell */
			short keep_cw = cw, keep_ch = ch, dw, dh, k;
			char c[BIDI_MAX];
			vst_font(vdi_h, 1);
			vst_height(vdi_h, keep_ch >= 16 ? 13 : 6, &dw, &dh);
			vst_alignment(vdi_h, 0, 5);
			memcpy(c, s + i, j - i);
			heb_font_map(c, j - i, opt.hebfont);
			for (k = 0; k < j - i; k++)
				v_gtext_n(vdi_h, x + (i + k) * keep_cw + (keep_cw - dw) / 2,
					  y + (keep_ch - dh) / 2, c + k, 1);
			font_reselect();
			cw = keep_cw;
			ch = keep_ch;
		}
		i = j;
	}
}

void text_color(short color)
{
	next_text_color = color;
}

#ifdef AES_PROFILE
long prof_texts, prof_texts_off, prof_texts_bold;	/* for VIEWMS: what text cost */
#endif

/* Set while the window is drawn (win.c): text goes on a byte. TOS's VDI
   writes text off a byte boundary several times slower (TOS 1.04 36.5
   ms against 5.4 a line, TOS 4.04 9.1 against 3.4; the guidelines,
   Speed). Not in ECal's dialogs, whose cursors are placed by the pixel */
short text_bytes;

/* where text_at puts left-anchored text that starts at x: for a box
   drawn round it */
short text_x(short x)
{
	return text_bytes ? (short)((x + 7) & ~7) : x;
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	short orig_x = x;
	char vis[BIDI_MAX];
	const char *out = s;
	short i, color = next_text_color, effects;
	next_text_color = 1;
	if (cols <= 0 || n <= 0)
		return;
	/* the text up to a line end */
	{
		const char *nl = memchr(s, '\n', n);
		if (nl)
			n = nl - s;
	}
	if (n > cols)
		n = cols;
	if (n > BIDI_MAX)
		n = BIDI_MAX;
	if (n <= 0)
		return;
	if (bidi_has_rtl(s, (short)n)) {
		short rtl = (flags & TX_LTR) ? 0 : bidi_is_rtl(s, (short)n);
		bidi_visual(s, (short)n, rtl, vis);
		out = vis;
		if (rtl && (flags & TX_RIGHT))
			x += (cols - (short)n) * cw;
	}
	if (flags & TX_CENTER)
		x += (cols - (short)n) * cw / 2;
	if (text_bytes && (x & 7)) {
		/* centred: to the nearest byte; against the right edge (Hebrew):
		   left, into the room it leaves; else right, a column less if
		   it filled its room */
		if (flags & TX_CENTER)
			x = (x + 3) & ~7;
		else if (x > orig_x)
			x &= ~7;
		else {
			x = (x + 7) & ~7;
			if (n >= cols)
				n = cols - 1;
			if (n <= 0)
				return;
		}
	}
	/* the greys where the screen has them; disabled text is light grey,
	   or dotted (the light effect) without colours */
	effects = ((flags & TX_BOLD) ? 1 : 0) | ((flags & TX_UNDER) ? 8 : 0);
	if (color == 1 && (flags & (TX_GREY | TX_PALE | TX_DIM))) {
		short g = text_grey((flags & (TX_PALE | TX_DIM)) != 0);
		if (g != 1)
			color = g;
		else if (flags & TX_DIM)
			effects |= 2;
	}
	if (heb_mode != HM_MAP && has_heb(out, n)) {
		set_wrmode(2);
		set_text((flags & TX_INVERSE) ? 0 : color, effects);
		runs_at(x, y, out, (short)n);
		return;
	}
	if (opt.hebfont != HEB_ATARI && has_heb(out, n)) {
		if (out != vis)
			memcpy(vis, out, n);
		out = vis;
		heb_font_map(vis, n, opt.hebfont);
	}
	set_wrmode(2);
	set_text((flags & TX_INVERSE) ? 0 : color, effects);
#ifdef AES_PROFILE
	prof_texts++;
	if (x & 7)
		prof_texts_off++;
	if (effects)
		prof_texts_bold++;
#endif
	for (i = 0; i < n; i += 120) {
		short k = n - i > 120 ? 120 : (short)(n - i);
		v_gtext_n(vdi_h, x + i * cw, y, out + i, k);
	}
}

long wrap_text(const char *s, long n, short width, long *breaks, long max)
{
	long count = 0, i = 0;
	if (width < 4)
		width = 4;
	while (i <= n && count < max) {
		long start = i, end, sp = -1, j;
		breaks[count++] = start;
		for (j = start; j < n && s[j] != '\n' && j - start < width; j++)
			if (s[j] == ' ')
				sp = j;
		if (j >= n)
			break;
		if (s[j] == '\n') {
			i = j + 1;
			if (i == n)
				break;
			continue;
		}
		end = (sp > start) ? sp + 1 : j;
		i = end;
	}
	return count;
}
