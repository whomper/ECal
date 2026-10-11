/*
 * draw.c - drawing for ECal's window: rectangles, lines, the colour
 * (or pattern) of each calendar, and text. Text is kept in logical
 * order; lines with Hebrew go through bidi_visual() and, when the
 * paragraph is right-to-left, can be aligned to the right edge (from
 * EMail's draw.c).
 */
#include <string.h>
#include "ui.h"
#include "../src/bidi.h"

static short next_text_color = 1;

/* VDI attributes as last set: each call is a trap, and on an 8 MHz ST
   setting them again for every piece of text cost more than the text */
static short c_wr = -1, c_fint = -1, c_fsty = -1, c_fcol = -1, c_lcol = -1, c_tcol = -1, c_teff = -1, c_per = -1;

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

short red_color(void)
{
	return ncolors >= 4 ? 2 : 1;
}

short grey_color(void)
{
	return ncolors >= 16 ? 8 : 1;
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
static short pens_ready;

/* The standard palettes differ (the ST's index 9 is dark grey, the
   Falcon's dark red), and the user may have changed them: pick, for each
   of our colours, the nearest one the screen really has. */
static void pens_init(void)
{
	/* the most distinctive first: they get their nearest match, and the
	   others the nearest one still free, so no two calendar colours
	   look the same (on 16 colours purple, brown and grey all came
	   out the same grey) */
	static const short order[NCOLORS] = { 0, 2, 3, 4, 5, 9, 8, 10, 11, 1, 6, 7 };
	short j, i, n = ncolors > 256 || ncolors <= 0 ? 256 : ncolors, rgb[3];
	unsigned char taken[256];
	pens_ready = 1;
	if (ncolors < 16)
		return;
	memset(taken, 0, sizeof(taken));
	for (j = 0; j < NCOLORS; j++) {
		short k = order[j], pick = -1;
		long best = 0x7fffffffL;
		for (i = 2; i < n; i++) {
			long dr, dg, db, dist;
			if (taken[i])
				continue;
			vq_color(vdi_h, i, rgb);
			dr = rgb[0] - mac_rgb[k][0];
			dg = rgb[1] - mac_rgb[k][1];
			db = rgb[2] - mac_rgb[k][2];
			dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
			if (dist < best) {
				best = dist;
				pick = i;
			}
		}
		if (pick >= 0) {
			c16[k] = pick;
			taken[pick] = 1;
		}
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

void text_color(short color)
{
	next_text_color = color;
}

void text_at(short x, short y, const char *s, long n, short cols, short flags)
{
	char vis[BIDI_MAX];
	const char *out = s;
	short i, color = next_text_color;
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
	if (opt.hebfont != HEB_ATARI && has_heb(out, n)) {
		if (out != vis)
			memcpy(vis, out, n);
		out = vis;
		heb_font_map(vis, n, opt.hebfont);
	}
	set_wrmode(2);
	set_text((flags & TX_INVERSE) ? 0 : color,
		 ((flags & TX_BOLD) ? 1 : 0) | ((flags & TX_DIM) ? 2 : 0) | ((flags & TX_UNDER) ? 8 : 0));
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
