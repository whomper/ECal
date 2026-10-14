/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * field.c - dialogs drawn by ECal itself.
 *
 * GEM's own text fields show text left to right only, so a Hebrew title
 * would come out backwards. Here every text field keeps its text in
 * logical order and shows it through the bidi algorithm (bidi.c), with
 * the cursor where a reader expects it; F10 switches to the Israeli
 * keyboard as everywhere else in ECal and EMail. The rest is
 * ordinary: buttons, tick boxes, pop-up menus, a row of colours, a
 * notes box of several lines.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ui.h"
#include "bidi.h"
#include "util.h"
#include "clip.h"

static GRECT box;		/* the dialog on screen */
static short rh;		/* pixels per row */
static short pad;
static short depth;		/* dialogs open (one can open another: Custom repeat) */
void (*dlg_after)(void);	/* called when the last dialog closes (window frame) */

#define FX(f) (box.x + pad + (f)->x * cw)
#define FY(f) (box.y + pad + (f)->y * rh)

/* the dialog being run, for what depends on its section */
static DLG *cur_dlg;

static int shown_now(const FIELD *f)
{
	return !cur_dlg || !cur_dlg->npages || !f->page || f->page == cur_dlg->page;
}

static int focusable(const FIELD *f)
{
	if (!shown_now(f))
		return 0;
	return (f->type == FT_TEXT || f->type == FT_PASS || f->type == FT_MULTI) && !(f->flags & FF_DISABLED);
}

static void field_rect(const FIELD *f, GRECT *r)
{
	r->x = FX(f) - 2;
	r->y = FY(f) - 2;
	r->w = f->w * cw + 4;
	r->h = (f->type == FT_MULTI ? f->h : 1) * ch + 4;
	if (f->type == FT_BUTTON || f->type == FT_POPUP) {
		r->y--;
		r->h += 2;
	}
}

/* the shown text of a password field */
static void shown(const FIELD *f, char *out, int size)
{
	int n = (int)strlen(f->buf), i;
	if (f->type != FT_PASS || (f->flags & FF_SHOWN)) {
		str_copy(out, f->buf, size);
		return;
	}
	if (n > size - 1)
		n = size - 1;
	for (i = 0; i < n; i++)
		out[i] = '*';
	out[n] = 0;
}

/* x offset (pixels from the field's left) of the cursor at logical
   index c of the visible text s[0..n) */
static short cursor_x(const char *s, short n, short c, short w, short rtl)
{
	char vis[BIDI_MAX];
	short pos[BIDI_MAX];
	unsigned char odd[BIDI_MAX];
	short xoff = rtl ? (w - n) * cw : 0;
	if (n <= 0)
		return rtl ? w * cw - 1 : 0;
	bidi_visual_map(s, n, rtl, vis, pos, odd);
	if (c < n)
		return xoff + (odd[c] ? (pos[c] + 1) * cw - 1 : pos[c] * cw);
	return xoff + (odd[n - 1] ? pos[n - 1] * cw : (pos[n - 1] + 1) * cw);
}

static short index_at(const char *s, short n, short px, short w, short rtl)
{
	char vis[BIDI_MAX];
	short pos[BIDI_MAX], i, vc;
	unsigned char odd[BIDI_MAX];
	short xoff = rtl ? (w - n) * cw : 0;
	if (n <= 0)
		return 0;
	bidi_visual_map(s, n, rtl, vis, pos, odd);
	vc = (px - xoff) / cw;
	if (px < xoff)
		return rtl ? n : 0;
	if (vc >= n)
		return rtl ? 0 : n;
	for (i = 0; i < n; i++)
		if (pos[i] == vc)
			return odd[i] ? i + (((px - xoff) % cw) < cw / 2 ? 1 : 0)
				      : i + (((px - xoff) % cw) >= cw / 2 ? 1 : 0);
	return n;
}

static short field_rtl(const FIELD *f, const char *s, short n)
{
	if (f->flags & FF_LTR)
		return 0;
	if (!n)
		return hebrew_kbd;
	return line_rtl(s, n);
}

/* the lines of a notes box */
static long multi_lines(const FIELD *f, long *br, long max)
{
	return wrap_text(f->buf, (long)strlen(f->buf), f->w, br, max);
}

static void draw_text_field(FIELD *f, int focus)
{
	GRECT r;
	char s[256];
	field_rect(f, &r);
	fill(&r, 0);
	frame(&r, (f->flags & FF_DISABLED) ? grey_color() : 1);
	if (focus) {
		GRECT r2 = r;
		r2.x--;
		r2.y--;
		r2.w += 2;
		r2.h += 2;
		frame(&r2, 1);
	}
	if (f->type == FT_MULTI) {
		long br[64], nl = multi_lines(f, br, 64), len = (long)strlen(f->buf), i, curl = 0;
		for (i = 0; i < nl; i++)
			if (br[i] <= f->cur)
				curl = i;
		if (curl < f->first)
			f->first = (short)curl;
		if (curl >= f->first + f->h)
			f->first = (short)(curl - f->h + 1);
		for (i = f->first; i < nl && i < f->first + f->h; i++) {
			long a = br[i], b = i + 1 < nl ? br[i + 1] : len, k;
			short rtl;
			while (b > a && (f->buf[b - 1] == '\n'))
				b--;
			k = b - a;
			if (k > f->w)
				k = f->w;
			rtl = field_rtl(f, f->buf + a, (short)k);
			text_at(FX(f), FY(f) + (short)(i - f->first) * ch, f->buf + a, k, f->w, rtl ? TX_RIGHT : TX_LTR);
			if (focus && i == curl) {
				short cx = cursor_x(f->buf + a, (short)k, (short)(f->cur - a > k ? k : f->cur - a), f->w, rtl);
				short y = FY(f) + (short)(i - f->first) * ch;
				line_col(FX(f) + cx, y, FX(f) + cx, y + ch - 1, 1);
			}
		}
		if (focus && !nl)
			line_col(FX(f) + (hebrew_kbd ? f->w * cw - 1 : 0), FY(f), FX(f) + (hebrew_kbd ? f->w * cw - 1 : 0), FY(f) + ch - 1, 1);
		return;
	}
	shown(f, s, sizeof(s));
	{
		short n = (short)strlen(s), vis, rtl;
		if (f->cur > n)
			f->cur = n;
		/* scroll so the cursor shows */
		if (f->cur < f->first)
			f->first = f->cur;
		if (f->cur > f->first + f->w - 1)
			f->first = f->cur - f->w + 1;
		if (f->first > n)
			f->first = n;
		if (f->first < 0)
			f->first = 0;
		vis = n - f->first > f->w ? f->w : n - f->first;
		rtl = field_rtl(f, s + f->first, vis);
		if (vis > 0)
			text_at(FX(f), FY(f), s + f->first, vis, f->w,
				(rtl ? TX_RIGHT : TX_LTR) | ((f->flags & FF_DISABLED) ? TX_DIM : 0));
		if (focus) {
			short cx = cursor_x(s + f->first, vis, f->cur - f->first, f->w, rtl);
			line_col(FX(f) + cx, FY(f), FX(f) + cx, FY(f) + ch - 1, 1);
		}
	}
}

/* ---- the eye beside a password field, as in EMail: open while the
   password is hidden, crossed out while it is shown ---- */

static const char *const eye_art[16] = {
	"................",
	"................",
	"................",
	".....######.....",
	"...##......##...",
	"..#....##....#..",
	".#....####....#.",
	"#.....####.....#",
	".#....####....#.",
	"..#....##....#..",
	"...##......##...",
	".....######.....",
	"................",
	"................",
	"................",
	"................",
};

static void eye_rect(const FIELD *f, GRECT *r)
{
	r->x = FX(f) + (f->w + 1) * cw;
	r->y = FY(f) + (ch - (ch >= 16 ? 16 : 8)) / 2;
	r->w = 16;
	r->h = ch >= 16 ? 16 : 8;
}

static void draw_eye(const FIELD *f)
{
	static unsigned short bits[16];
	static const short colors[2] = { 1, 0 };
	short rows = ch >= 16 ? 16 : 8, r, c, pxy[8];
	MFDB src, dst;
	GRECT e;
	for (r = 0; r < rows; r++) {
		const char *a = eye_art[rows == 16 ? r : r * 2 + 1];
		unsigned short v = 0;
		for (c = 0; c < 16; c++)
			v = (unsigned short)((v << 1) | (a[c] == '#'));
		if (f->flags & FF_SHOWN)	/* a stroke from bottom left to top right */
			v |= (unsigned short)((0x8000 >> ((rows - 1 - r) * 16 / rows)) | (0x4000 >> ((rows - 1 - r) * 16 / rows)));
		bits[r] = v;
	}
	eye_rect(f, &e);
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.fd_addr = st_ram(sizeof(bits));
	memcpy(src.fd_addr, bits, sizeof(bits));
	src.fd_w = 16;
	src.fd_h = rows;
	src.fd_wdwidth = 1;
	src.fd_nplanes = 1;
	pxy[0] = 0;
	pxy[1] = 0;
	pxy[2] = 15;
	pxy[3] = rows - 1;
	pxy[4] = e.x;
	pxy[5] = e.y;
	pxy[6] = e.x + 15;
	pxy[7] = e.y + rows - 1;
	vrt_cpyfm(vdi_h, 1, pxy, &src, &dst, colors);
}

static int eye_hit(const FIELD *f, short mx, short my)
{
	GRECT e;
	if (f->type != FT_PASS)
		return 0;
	eye_rect(f, &e);
	e.x -= 2;
	e.y -= 2;
	e.w += 4;
	e.h += 4;
	return rect_hit(&e, mx, my);
}

static void draw_field(DLG *d, short i)
{
	FIELD *f = &d->f[i];
	GRECT r;
	short dis = (f->flags & FF_DISABLED) ? TX_DIM : 0;
	switch (f->type) {
	case FT_LABEL:
	case FT_HEAD:
		text_at(FX(f), FY(f), f->label, (long)strlen(f->label), f->w ? f->w : (short)strlen(f->label),
			(f->type == FT_HEAD ? TX_BOLD : 0) | (line_rtl(f->label, (long)strlen(f->label)) ? TX_RIGHT : TX_LTR) | dis);
		break;
	case FT_TEXT:
	case FT_PASS:
	case FT_MULTI:
		draw_text_field(f, d->focus == i);
		if (f->type == FT_PASS)
			draw_eye(f);
		break;
	case FT_BUTTON:
		field_rect(f, &r);
		fill(&r, 0);
		frame(&r, dis ? grey_color() : 1);
		if (f->flags & FF_DEFAULT) {
			GRECT r2 = r;
			r2.x--;
			r2.y--;
			r2.w += 2;
			r2.h += 2;
			frame(&r2, 1);
		}
		text_at(FX(f), FY(f), f->label, (long)strlen(f->label), f->w, TX_CENTER | TX_LTR | dis |
			((f->flags & FF_DEFAULT) ? TX_BOLD : 0));
		break;
	case FT_CHECK: {
		GRECT b;
		b.x = FX(f);
		b.y = FY(f) + 1;
		b.w = 2 * cw - 4;
		b.h = ch - 2;
		fill(&b, 0);
		frame(&b, dis ? grey_color() : 1);
		if (f->val) {
			line_col(b.x + 2, b.y + b.h / 2, b.x + b.w / 2 - 1, b.y + b.h - 3, 1);
			line_col(b.x + b.w / 2 - 1, b.y + b.h - 3, b.x + b.w - 3, b.y + 2, 1);
			line_col(b.x + 2, b.y + b.h / 2 + 1, b.x + b.w / 2 - 1, b.y + b.h - 2, 1);
			line_col(b.x + b.w / 2 - 1, b.y + b.h - 2, b.x + b.w - 3, b.y + 3, 1);
		}
		text_at(FX(f) + 2 * cw, FY(f), f->label, (long)strlen(f->label), f->w - 2, TX_LTR | dis);
		break;
	}
	case FT_POPUP: {
		const char *l = f->val >= 0 && f->val < f->nitems ? f->items[f->val] : "";
		field_rect(f, &r);
		fill(&r, 0);
		frame(&r, dis ? grey_color() : 1);
		/* a shadow says "menu" */
		line_col(r.x + 1, r.y + r.h, r.x + r.w, r.y + r.h, 1);
		line_col(r.x + r.w, r.y + 1, r.x + r.w, r.y + r.h, 1);
		while (*l == ' ')
			l++;
		text_at(FX(f) + cw / 2, FY(f), l, (long)strlen(l), f->w - 2,
			(line_rtl(l, (long)strlen(l)) ? 0 : TX_LTR) | dis);
		text_at(FX(f) + (f->w - 1) * cw, FY(f), "\x02", 1, 1, TX_LTR | dis);
		break;
	}
	case FT_COLOR: {
		short k;
		for (k = 0; k < NCOLORS; k++) {
			GRECT b;
			PEN p;
			pen_color(k, &p);
			b.x = FX(f) + k * 3 * cw;
			b.y = FY(f) - 1;
			b.w = 2 * cw;
			b.h = ch + 2;
			fill(&b, 0);
			b.x += 2;
			b.y += 2;
			b.w -= 4;
			b.h -= 4;
			if (ncolors >= 16)
				fill(&b, p.color);
			else
				fill_pat(&b, p.color, p.interior, p.style == 2 ? 4 : p.style);
			b.x -= 2;
			b.y -= 2;
			b.w += 4;
			b.h += 4;
			frame(&b, f->val == k ? 1 : 0);
			if (f->val == k) {
				b.x--;
				b.y--;
				b.w += 2;
				b.h += 2;
				frame(&b, 1);
			}
		}
		break;
	}
	}
}

/* ---- sections ---- */

static void draw_all(DLG *d);

static void section_rect(DLG *d, short k, GRECT *r)
{
	r->x = box.x + 5;
	r->y = box.y + pad + (1 + 2 * k) * rh - 3;
	r->w = d->side * cw + pad - 6;
	r->h = ch + 6;
}

static short section_at(DLG *d, short mx, short my)
{
	short k;
	for (k = 0; k < d->npages; k++) {
		GRECT r;
		section_rect(d, k, &r);
		r.y -= rh / 2;		/* the space between the names counts too */
		r.h += rh;
		if (rect_hit(&r, mx, my))
			return k + 1;
	}
	return 0;
}

/* the list of sections, the line beside it, and the chosen one's name
   over a rule */
static void draw_sections(DLG *d)
{
	short k, lx = box.x + pad + d->side * cw + cw / 2;
	short hx = box.x + pad + (d->side + 2) * cw, hy = box.y + pad + rh;
	for (k = 0; k < d->npages; k++) {
		GRECT r;
		const char *s = d->pages[k];
		section_rect(d, k, &r);
		fill(&r, k + 1 == d->page ? 1 : 0);
		text_at(r.x + cw, r.y + 3, s, (long)strlen(s), d->side, TX_LTR | (k + 1 == d->page ? TX_INVERSE : 0));
	}
	line_col(lx, box.y + ch + 6, lx, box.y + box.h - 4, 1);
	if (d->page >= 1 && d->page <= d->npages) {
		const char *s = d->pages[d->page - 1];
		text_at(hx, hy, s, (long)strlen(s), d->w - d->side - 2, TX_LTR | TX_BOLD);
		line_col(hx, hy + ch + 3, box.x + box.w - pad, hy + ch + 3, 1);
	}
}

static void set_page(DLG *d, short p)
{
	short i;
	d->page = p;
	d->focus = -1;
	for (i = 0; i < d->n; i++)
		if (focusable(&d->f[i])) {
			d->focus = i;
			break;
		}
	draw_all(d);
}

static void draw_all(DLG *d)
{
	GRECT t;
	short i;
	char title[80];
	clip_on(&box);
	graf_mouse(M_OFF, 0);
	fill(&box, 0);
	frame(&box, 1);
	{
		GRECT in = box;
		in.x += 2;
		in.y += 2;
		in.w -= 4;
		in.h -= 4;
		frame(&in, 1);
	}
	t.x = box.x + 3;
	t.y = box.y + 3;
	t.w = box.w - 6;
	t.h = ch + 2;
	fill(&t, 1);
	snprintf(title, sizeof(title), "%s", d->title ? d->title : "");
	text_at(t.x, t.y + 1, title, (long)strlen(title), t.w / cw, TX_INVERSE | TX_BOLD | TX_CENTER |
		(line_rtl(title, (long)strlen(title)) ? 0 : TX_LTR));
	if (hebrew_kbd)
		text_at(t.x + t.w - 4 * cw, t.y + 1, "\xD3\xC3", 2, 2, TX_INVERSE);	/* "עב": Hebrew keyboard */
	if (d->npages)
		draw_sections(d);
	for (i = 0; i < d->n; i++)
		if (shown_now(&d->f[i]))
			draw_field(d, i);
	graf_mouse(M_ON, 0);
}

void dlg_redraw_field(DLG *d, short i)
{
	GRECT r;
	FIELD *f = &d->f[i];
	clip_on(&box);
	graf_mouse(M_OFF, 0);
	field_rect(f, &r);
	if (f->type == FT_LABEL || f->type == FT_HEAD || f->type == FT_CHECK || f->type == FT_COLOR) {
		r.x = FX(f) - 2;
		r.w = (f->type == FT_COLOR ? NCOLORS * 3 : f->w ? f->w : (short)strlen(f->label)) * cw + 6;
		r.y = FY(f) - 3;
		r.h = ch + 6;
		rect_and(&r, &box);
		fill(&r, 0);
	} else {
		r.x -= 2;
		r.y -= 2;
		r.w += 5;
		r.h += 5;
		fill(&r, 0);
	}
	draw_field(d, i);
	graf_mouse(M_ON, 0);
}

static short next_focus(DLG *d, short from, short dir)
{
	short i = from, k;
	for (k = 0; k < d->n; k++) {
		i += dir;
		if (i < 0)
			i = d->n - 1;
		if (i >= d->n)
			i = 0;
		if (focusable(&d->f[i]))
			return i;
	}
	return from;
}

static void set_focus(DLG *d, short i)
{
	short old = d->focus;
	d->focus = i;
	if (old >= 0 && old != i)
		dlg_redraw_field(d, old);
	if (i >= 0)
		dlg_redraw_field(d, i);
}

static void press_effect(DLG *d, short i)
{
	GRECT r;
	field_rect(&d->f[i], &r);
	clip_on(&box);
	graf_mouse(M_OFF, 0);
	set_wrmode(3);
	fill(&r, 1);
	evnt_timer_(90);
	fill(&r, 1);
	set_wrmode(1);
	graf_mouse(M_ON, 0);
}

/* ^C ^X ^V (common/ui/clip.c). With nothing selected, the field, and in a
   field of several lines the line the cursor is in; nothing is copied out
   of a password. A paste goes in as far as the field takes it, its first
   line only unless the field has more */
static int clip_field(DLG *d, FIELD *f, int what)
{
	char t[1200], *p;
	short n = (short)strlen(f->buf), s = 0, e = n;
	if (f->type == FT_MULTI) {
		for (s = f->cur; s > 0 && f->buf[s - 1] != '\n'; s--)
			;
		for (e = f->cur; e < n && f->buf[e] != '\n'; e++)
			;
	}
	if (what == CLIP_PASTE) {
		clip_get(t, sizeof(t));
		if (f->type != FT_MULTI)
			clip_line(t);
		for (p = t; *p && n < f->max - 1; p++) {
			unsigned char a = *p == '\t' ? ' ' : (unsigned char)*p;
			if ((a < 32 && a != '\n') || ((f->flags & FF_DIGITS) && (a < '0' || a > '9')) ||
			    (f->type == FT_PASS && a >= 0x80))
				continue;
			memmove(f->buf + f->cur + 1, f->buf + f->cur, n - f->cur + 1);
			f->buf[f->cur++] = (char)a;
			n++;
		}
	} else {
		if (f->type == FT_PASS || !clip_put(f->buf + s, e - s) || what != CLIP_CUT)
			return 1;
		if (f->type == FT_MULTI && e < n)
			e++;			/* the line goes with its end */
		memmove(f->buf + s, f->buf + e, n - e + 1);
		f->cur = s;
		f->first = 0;
	}
	dlg_redraw_field(d, (short)(f - d->f));
	return 1;
}

/* typing into a text field; 1 if the key was taken */
static int edit_key(DLG *d, FIELD *f, short kstate, short key)
{
	short scan = KEY_SCAN(key), n = (short)strlen(f->buf);
	unsigned char a = key_char(kstate, key);
	short rtl = field_rtl(f, f->buf, n);
	if ((kstate & K_CTRL) && clip_key(key))
		return clip_field(d, f, clip_key(key));
	switch (scan) {
	case 0x0e:		/* Backspace */
		if (f->cur > 0) {
			memmove(f->buf + f->cur - 1, f->buf + f->cur, n - f->cur + 1);
			f->cur--;
		}
		break;
	case 0x53:		/* Delete */
		if (f->cur < n)
			memmove(f->buf + f->cur, f->buf + f->cur + 1, n - f->cur);
		break;
	case 0x4b:		/* left: on screen, so backwards in Hebrew */
	case 0x4d:
		if ((scan == 0x4b) != (rtl != 0)) {
			if (f->cur > 0)
				f->cur--;
		} else if (f->cur < n) {
			f->cur++;
		}
		break;
	case 0x47:		/* Clr/Home: start, with Shift the end */
		f->cur = (kstate & (K_LSHIFT | K_RSHIFT)) ? n : 0;
		break;
	case 0x61:		/* Undo: empty the field */
		f->buf[0] = 0;
		f->cur = 0;
		f->first = 0;
		break;
	case 0x48:
	case 0x50:
		if (f->type == FT_MULTI) {
			long br[64], nl = multi_lines(f, br, 64), i, line = 0;
			for (i = 0; i < nl; i++)
				if (br[i] <= f->cur)
					line = i;
			if (scan == 0x48 && line > 0)
				f->cur = (short)(br[line - 1] + (f->cur - br[line] < br[line] - br[line - 1] ? f->cur - br[line] : br[line] - br[line - 1] - 1));
			else if (scan == 0x50 && line + 1 < nl) {
				long next_end = line + 2 < nl ? br[line + 2] - 1 : n;
				f->cur = (short)(br[line + 1] + (f->cur - br[line]));
				if (f->cur > next_end)
					f->cur = (short)next_end;
			} else
				return 0;
			break;
		}
		return 0;
	case 0x1c:
	case 0x72:
		if (f->type != FT_MULTI || (kstate & K_CTRL))
			return 0;
		a = '\n';
		/* fall through */
	default:
		if (a < 32 && a != '\n')
			return 0;
		if ((f->flags & FF_DIGITS) && (a < '0' || a > '9'))
			return 1;	/* a number field: taken, and ignored */
		if (n >= f->max - 1)
			return 1;	/* full */
		if (f->type == FT_PASS && a >= 0x80)
			return 1;
		memmove(f->buf + f->cur + 1, f->buf + f->cur, n - f->cur + 1);
		f->buf[f->cur++] = (char)a;
	}
	dlg_redraw_field(d, (short)(f - d->f));
	return 1;
}

static int click_field(DLG *d, short i, short mx, short my)
{
	FIELD *f = &d->f[i];
	if (f->type == FT_MULTI) {
		long br[64], nl = multi_lines(f, br, 64), line = f->first + (my - FY(f)) / ch;
		long len = (long)strlen(f->buf), a, b;
		if (line >= nl)
			line = nl - 1;
		if (line < 0) {
			f->cur = (short)len;
		} else {
			a = br[line];
			b = line + 1 < nl ? br[line + 1] : len;
			while (b > a && f->buf[b - 1] == '\n')
				b--;
			if (b - a > f->w)
				b = a + f->w;
			f->cur = (short)(a + index_at(f->buf + a, (short)(b - a), mx - FX(f), f->w,
						      field_rtl(f, f->buf + a, (short)(b - a))));
		}
	} else {
		char s[256];
		short n, vis;
		shown(f, s, sizeof(s));
		n = (short)strlen(s);
		vis = n - f->first > f->w ? f->w : n - f->first;
		f->cur = f->first + index_at(s + f->first, vis, mx - FX(f), f->w, field_rtl(f, s + f->first, vis));
	}
	set_focus(d, i);
	return 1;
}

static short field_at(DLG *d, short mx, short my)
{
	short i;
	for (i = d->n - 1; i >= 0; i--) {
		FIELD *f = &d->f[i];
		GRECT r;
		if (f->type == FT_LABEL || f->type == FT_HEAD || !shown_now(f))
			continue;
		if (eye_hit(f, mx, my))
			return i;
		field_rect(f, &r);
		if (f->type == FT_CHECK) {
			r.w = ((short)strlen(f->label) + 2) * cw + 4;
		} else if (f->type == FT_COLOR) {
			r.w = NCOLORS * 3 * cw;
		}
		if (rect_hit(&r, mx, my))
			return i;
	}
	return -1;
}

static short find_flag(DLG *d, short flag)
{
	short i;
	for (i = 0; i < d->n; i++)
		if (d->f[i].type == FT_BUTTON && (d->f[i].flags & flag) && !(d->f[i].flags & FF_DISABLED) &&
		    shown_now(&d->f[i]))
			return i;
	return -1;
}

void dlg_redraw(DLG *d)
{
	draw_all(d);
}

short dlg_run(DLG *d)
{
	short result = -2, i, mx, my, mb, ks, m[8];
	AEVENT e;
	GRECT outer = box;		/* the dialog under this one, if any */
	short outer_rh = rh, outer_pad = pad;
	DLG *outer_dlg = cur_dlg;
	font_sys_push();
	cur_dlg = d;
	if (d->npages && (d->page < 1 || d->page > d->npages))
		d->page = 1;
	pad = cw + 2;
	rh = ch + 6;
	box.w = d->w * cw + 2 * pad;
	box.h = d->h * rh + 2 * pad;
	if (box.w > desk_w)
		box.w = desk_w;
	if (box.h > desk_h)
		box.h = desk_h;
	box.x = desk_x + (desk_w - box.w) / 2;
	box.y = desk_y + (desk_h - box.h) / 2;
	pad += 0;
	/* the title bar takes the first row */
	for (i = 0; i < d->n; i++) {
		d->f[i].cur = d->f[i].buf ? (short)strlen(d->f[i].buf) : 0;
		d->f[i].first = 0;
		if (d->f[i].type == FT_MULTI)
			d->f[i].cur = 0;
	}
	if (d->focus < 0 || d->focus >= d->n || !focusable(&d->f[d->focus]))
		d->focus = next_focus(d, -1, 1);
	if (d->focus >= 0 && !focusable(&d->f[d->focus]))
		d->focus = -1;
	depth++;
	wind_update(BEG_UPDATE);
	wind_update(3);		/* BEG_MCTRL: the dialog owns the mouse */
	form_dial(FMD_START, box.x, box.y, box.w + 2, box.h + 2);
	graf_mouse(ARROW, 0);
	draw_all(d);
	while (result == -2) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x102, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short scan = KEY_SCAN(e.kreturn);
			FIELD *f = d->focus >= 0 ? &d->f[d->focus] : 0;
			if (scan == 0x44) {			/* F10: the Hebrew keyboard */
				set_hebrew_kbd(!hebrew_kbd);
				draw_all(d);
			} else if (scan == 0x01) {		/* Esc */
				result = find_flag(d, FF_CANCEL);
				if (result < 0)
					result = -1;
			} else if (scan == 0x0f || ((scan == 0x48 || scan == 0x50) && (!f || f->type != FT_MULTI))) {
				short dir = (scan == 0x48 || (scan == 0x0f && (e.kstate & (K_LSHIFT | K_RSHIFT)))) ? -1 : 1;
				if (d->focus >= 0)
					set_focus(d, next_focus(d, d->focus, dir));
			} else if (f && edit_key(d, f, e.kstate, e.kreturn)) {
				/* typed */
			} else if (scan == 0x1c || scan == 0x72) {	/* Return */
				short b = find_flag(d, FF_DEFAULT);
				if (b >= 0) {
					press_effect(d, b);
					result = b;
				}
			}
		}
		if ((e.which & MU_BUTTON) && d->npages) {
			short k = section_at(d, e.mx, e.my);
			if (k) {
				if (k != d->page)
					set_page(d, k);
				e.which &= ~MU_BUTTON;
				do {
					graf_mkstate(&mx, &my, &mb, &ks);
				} while (mb & 3);
			}
		}
		if (e.which & MU_BUTTON) {
			short hit = field_at(d, e.mx, e.my);
			if (hit >= 0 && !(d->f[hit].flags & FF_DISABLED)) {
				FIELD *f = &d->f[hit];
				switch (f->type) {
				case FT_TEXT:
				case FT_PASS:
				case FT_MULTI:
					if (eye_hit(f, e.mx, e.my)) {
						/* show or hide the password; the dialog stays */
						f->flags ^= FF_SHOWN;
						dlg_redraw_field(d, hit);
						break;
					}
					click_field(d, hit, e.mx, e.my);
					break;
				case FT_BUTTON:
					press_effect(d, hit);
					result = hit;
					break;
				case FT_CHECK:
					f->val = !f->val;
					dlg_redraw_field(d, hit);
					if (d->changed)
						d->changed(d, hit);
					break;
				case FT_POPUP: {
					GRECT r;
					short k;
					field_rect(f, &r);
					/* the popup draws over the dialog: it saves nothing, so redraw after */
					k = popup(r.x, r.y + r.h, f->items, f->nitems);
					draw_all(d);
					if (k >= 0 && k != f->val) {
						f->val = k;
						dlg_redraw_field(d, hit);
						if (d->changed)
							d->changed(d, hit);
					}
					break;
				}
				case FT_COLOR: {
					short k = (e.mx - FX(f)) / (3 * cw);
					if (k >= 0 && k < NCOLORS) {
						f->val = k;
						dlg_redraw_field(d, hit);
						if (d->changed)
							d->changed(d, hit);
					}
					break;
				}
				}
			}
			do {
				graf_mkstate(&mx, &my, &mb, &ks);
			} while (mb & 3);
		}
	}
	clip_off();
	form_dial(FMD_FINISH, box.x, box.y, box.w + 2, box.h + 2);
	wind_update(2);
	wind_update(END_UPDATE);
	/* back to the dialog underneath: its place, and the caller repaints it */
	box = outer;
	rh = outer_rh;
	pad = outer_pad;
	cur_dlg = outer_dlg;
	font_sys_pop();
	if (--depth == 0 && dlg_after)
		dlg_after();
	return result;
}

/* ---------------- a list to pick from ---------------- */

short dlg_pick(const char *title, const char *const *items, short n, short sel)
{
	short rows = (desk_h - 8 * ch) / (ch + 2), w = 10, i, first, res = -2, m[8];
	GRECT b, list;
	AEVENT e;
	if (rows > n)
		rows = n;
	if (rows < 3)
		rows = 3;
	for (i = 0; i < n; i++)
		if ((short)strlen(items[i]) > w)
			w = (short)strlen(items[i]);
	if ((short)strlen(title) > w)
		w = (short)strlen(title);
	w += 4;
	if (sel < 0)
		sel = 0;
	first = sel - rows / 2;
	b.w = w * cw + 4 * cw;
	b.h = (rows + 3) * (ch + 2) + ch;
	b.x = desk_x + (desk_w - b.w) / 2;
	b.y = desk_y + (desk_h - b.h) / 2;
	list.x = b.x + cw;
	list.y = b.y + 2 * (ch + 2);
	list.w = b.w - 2 * cw;
	list.h = rows * (ch + 2);
	wind_update(BEG_UPDATE);
	wind_update(3);
	form_dial(FMD_START, b.x, b.y, b.w + 2, b.h + 2);
	while (res == -2) {
		short mx, my, mb, ks;
		if (first > n - rows)
			first = n - rows;
		if (first < 0)
			first = 0;
		clip_on(&b);
		graf_mouse(M_OFF, 0);
		fill(&b, 0);
		frame(&b, 1);
		text_at(b.x + cw, b.y + 4, title, (long)strlen(title), w, TX_BOLD | TX_LTR);
		frame(&list, 1);
		for (i = 0; i < rows && first + i < n; i++) {
			GRECT r;
			r.x = list.x + 1;
			r.y = list.y + i * (ch + 2) + 1;
			r.w = list.w - 2;
			r.h = ch + 1;
			fill(&r, first + i == sel ? 1 : 0);
			text_at(r.x + cw, r.y, items[first + i], (long)strlen(items[first + i]), w,
				(first + i == sel ? TX_INVERSE : 0) | (line_rtl(items[first + i], (long)strlen(items[first + i])) ? 0 : TX_LTR));
		}
		text_at(b.x + cw, list.y + list.h + 4, "\x01\x02 Return: choose  Esc: cancel", 29, w, TX_GREY | TX_LTR);
		graf_mouse(M_ON, 0);
		evnt_multi_(MU_KEYBD | MU_BUTTON, 0x102, 3, 0, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn);
			unsigned char a = KEY_ASCII(e.kreturn);
			if (sc == 0x01)
				res = -1;
			else if (sc == 0x1c || sc == 0x72)
				res = sel;
			else if (sc == 0x48 && sel > 0) {
				sel--;
				if (sel < first)
					first = sel;
			} else if (sc == 0x50 && sel < n - 1) {
				sel++;
				if (sel >= first + rows)
					first = sel - rows + 1;
			} else if (a >= 'A') {
				/* jump to the next item starting with that letter */
				for (i = 1; i <= n; i++) {
					short k = (sel + i) % n;
					const char *s = items[k];
					while (*s == ' ')
						s++;
					if ((*s | 32) == (a | 32)) {
						sel = k;
						first = sel - rows / 2;
						break;
					}
				}
			}
		}
		if (e.which & MU_BUTTON) {
			if (rect_hit(&list, e.mx, e.my)) {
				short k = first + (e.my - list.y) / (ch + 2);
				if (k < n) {
					if (k == sel && e.breturn >= 2)
						res = sel;
					sel = k;
					if (e.breturn >= 2)
						res = sel;
				}
			} else if (!rect_hit(&b, e.mx, e.my)) {
				res = -1;
			} else if (e.my < list.y) {
				first -= rows - 1;
			} else if (e.my > list.y + list.h) {
				first += rows - 1;
			}
			do {
				graf_mkstate(&mx, &my, &mb, &ks);
			} while (mb & 3);
		}
	}
	clip_off();
	form_dial(FMD_FINISH, b.x, b.y, b.w + 2, b.h + 2);
	wind_update(2);
	wind_update(END_UPDATE);
	return res;
}

int dlg_ask(const char *title, const char *label, char *buf, short len)
{
	FIELD f[4];
	DLG d;
	short w = len + (short)strlen(label) + 4, r;
	if (w < 36)
		w = 36;
	memset(f, 0, sizeof(f));
	memset(&d, 0, sizeof(d));
	f[0].type = FT_LABEL;
	f[0].x = 0;
	f[0].y = 2;
	f[0].label = label;
	f[1].type = FT_TEXT;
	f[1].x = (short)strlen(label) + 1;
	f[1].y = 2;
	f[1].w = len < w - f[1].x - 1 ? len : w - f[1].x - 1;
	f[1].buf = buf;
	f[1].max = len + 1;
	f[2].type = FT_BUTTON;
	f[2].flags = FF_CANCEL;
	f[2].x = w - 22;
	f[2].y = 4;
	f[2].w = 10;
	f[2].label = "Cancel";
	f[3].type = FT_BUTTON;
	f[3].flags = FF_DEFAULT;
	f[3].x = w - 10;
	f[3].y = 4;
	f[3].w = 10;
	f[3].label = "OK";
	d.title = title;
	d.w = w;
	d.h = 5;
	d.f = f;
	d.n = 4;
	d.focus = 1;
	r = dlg_run(&d);
	return r == 3;
}
