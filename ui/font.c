/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * font.c - the font for ECal's window (menu > Font...), the same
 * selector as EMail's; dialogs and popups keep the system font.
 *
 * The font selector looks like the ones in other GEM programs: the font
 * families in a list on the left, the sizes of the chosen one next to
 * it, what kind of font it is, and a sample. The system font comes in
 * two sizes; with a GDOS (NVDI, SpeedoGDOS, FontGDOS...) its fonts are
 * listed too. ECal lays text out in character cells, so it needs a
 * monospaced font: only those are listed. Each font is measured once
 * and the answer kept in ECAL.FNT, so the selector opens quickly the
 * next time even with hundreds of fonts installed.
 *
 * The dialog also says where the system and bitmap fonts keep their
 * Hebrew letters: the Atari character set (TOS, EmuTOS) or the
 * ISO-8859-8 or DOS places, as Israeli system fonts and many GDOS fonts
 * have them. A vector font (Speedo, TrueType, Type 1) is asked through
 * NVDI for its Hebrew letters by Unicode; when it has none, they are
 * drawn in the system font in the same cells.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "../src/plat.h"
#include "conn.h"

#define MAXFAM 256
#define MAXSIZE 20

typedef struct {
	short id, format;	/* VDI font id; 1 bitmap, 2 Speedo, 4 TrueType, 8 Type 1 */
	signed char mono;	/* -1: not looked at yet */
	char name[33];
} FAM;

/* as many as there are fonts (the system font alone without GDOS, on an
   ST most often), from the heap once they are counted */
static FAM *fam, fam_sys;
static short nfam, maxfam, gdos_fonts = -1;
static short boot_fonts;	/* the accessory: its fonts settled at boot (font_boot) */
static short sizes[MAXSIZE], nsizes;
static char size_label[MAXSIZE][12];
static short sys_depth;		/* dialogs open: the system font */
static void use_sys(void);

short heb_mode;			/* HM_*: how this font gets its Hebrew */
static short heb_why;		/* HM_SYSTEM: why */
#define HW_NONE     0
#define HW_NOHEB    1		/* the font has no Hebrew letters */
#define HW_NOLOOKUP 2		/* the VDI can't look letters up (no NVDI 5) */
unsigned short heb_idx[27];	/* HM_UNICODE: the font's index of each letter */
static short cur_id = 1, cur_pt, probed_id = -1;

/* Unicode of the Hebrew letters in the Atari order (0xC2-0xDC), less 0x5D0 */
static const unsigned char heb_uni[27] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 16,
	17, 18, 20, 22, 23, 24, 25, 26, 15, 10, 13, 19, 21
};

static short format_of(short id);

/* a vector font: its Hebrew letters by Unicode through NVDI, else the
   system font draws them */
static void probe_hebrew(short id)
{
	static short probed_mode, probed_why;
	short k, fmt;
	long a = -1;
	if (id == 1) {
		heb_mode = HM_MAP;
		return;
	}
	if (id == probed_id) {
		heb_mode = probed_mode;
		heb_why = probed_why;
		return;
	}
	probed_id = id;
	heb_mode = HM_MAP;
	heb_why = HW_NONE;
	fmt = format_of(id);
	/* a bitmap font draws the Atari codes itself; a vector font (or one
	   whose kind the VDI doesn't say) is asked for its Hebrew letters */
	if (fmt != 1) {
		a = vqt_char_index(vdi_h, 'A', 1, 0);
		if (a > 0) {
			for (k = 0; k < 27; k++) {
				long i = vqt_char_index(vdi_h, (unsigned short)(0x5D0 + heb_uni[k]), 2, 0);
				if (i <= 0)
					break;
				heb_idx[k] = (unsigned short)i;
			}
			if (k == 27) {
				heb_mode = HM_UNICODE;
			} else {
				heb_mode = HM_SYSTEM;
				heb_why = HW_NOHEB;
			}
		} else if (fmt) {
			heb_mode = HM_SYSTEM;	/* a vector font, and no NVDI 5 to ask */
			heb_why = HW_NOLOOKUP;
		}
	}
	probed_mode = heb_mode;
	probed_why = heb_why;
	{
		char m[120];
		snprintf(m, sizeof(m), "font %d: format %d, 'A' at %ld, Hebrew %s", id, fmt, a,
			 heb_mode == HM_UNICODE ? "from the font" : heb_mode == HM_SYSTEM ?
			 (heb_why == HW_NOHEB ? "in the system font (none in it)" : "in the system font (no NVDI 5)") :
			 "as the font has it");
		conn_log("ECal", "--", m, (long)strlen(m));
	}
}

/* set the font; cw/ch become its cell size. 0 if it isn't there.
   probe: find where its Hebrew is (on the screen's workstation; the
   window's bitmap takes the answer as it is) */
static int set_font_on(short id, short pt, int probe);

static int set_font(short id, short pt)
{
	return set_font_on(id, pt, 1);
}

static int set_font_on(short id, short pt, int probe)
{
	short w, h;
	if (id == 1) {
		vst_font(vdi_h, 1);
		vst_height(vdi_h, pt == 8 ? 6 : pt == 16 ? 13 : (gl_hchar >= 16 ? 13 : 6), &w, &h);
	} else {
		if (vst_font(vdi_h, id) != id)
			return 0;
		vst_point(vdi_h, pt, &w, &h);
	}
	if (w < 4 || h < 6)
		return 0;
	cw = w;
	ch = h;
	vst_alignment(vdi_h, 0, 5);
	cur_id = id;
	cur_pt = pt;
	if (probe)
		probe_hebrew(id);
	return 1;
}

/* back to the text font after drawing in another */
void font_reselect(void)
{
	short keep_cw = cw, keep_ch = ch;
	if (!set_font_on(cur_id, cur_pt, 0) && cur_id != 1) {
		static short told = -1;	/* said once a font */
		if (told != cur_id) {
			char m[80];
			told = cur_id;
			snprintf(m, sizeof(m), "font %d not on this workstation (%d): the system font", cur_id, vdi_h);
			conn_log("ECal", "--", m, (long)strlen(m));
		}
	}
	cw = keep_cw;
	ch = keep_ch;
}

/* The accessory loads GDOS fonts only now, before its first event call
   (guidelines 2.2). Under NVDI always: they stay its own, so Font...
   lists what the program's does and a font chosen there is used at
   once. Under FONTGDOS or SpeedoGDOS only when ECAL.INF names one
   (want): their memory belongs to the program in front, so fonts loaded
   later would be freed under the accessory. The program loads them when
   they're first wanted. 1 if fonts were loaded */
int font_boot(int want)
{
	boot_fonts = 1;
	gdos_fonts = (want || get_cookie(0x4E564449L)) && vq_gdos() ? vst_load_fonts(vdi_h, 0) : 0;	/* 'NVDI' */
	return gdos_fonts > 0;
}

/* whether another workstation (win.c's bitmap) may load them too */
int font_gdos_loaded(void)
{
	return boot_fonts ? gdos_fonts > 0 : vq_gdos();
}

void font_apply(void)
{
	if (sys_depth) {
		use_sys();
		return;
	}
	if (opt.font_id != 1 && gdos_fonts < 0 && vq_gdos())
		gdos_fonts = vst_load_fonts(vdi_h, 0);
	if (!set_font(opt.font_id, opt.font_pt)) {
		/* the accessory without its fonts (one chosen since it
		   started): the system font until the next boot, the choice
		   kept */
		if (!boot_fonts) {
			opt.font_id = 1;
			opt.font_pt = 0;
		}
		set_font(1, 0);
	}
}

/* the system font at the AES's size, for the dialog's own text */
static void use_sys(void)
{
	set_font(1, gl_hchar >= 16 ? 16 : 8);
	cw = gl_wchar;
	ch = gl_hchar;
}

/* dialogs, popups and the About box keep the system font, as EMail's
   do; the chosen one is for the window */

void font_sys_push(void)
{
	if (!sys_depth++)
		use_sys();
}

void font_sys_pop(void)
{
	if (sys_depth && !--sys_depth)
		font_apply();
}

/* ---------------- the fonts ---------------- */

/* whether a font is monospaced: 'i', 'W' and 'm' as wide */
static int measure_mono(FAM *f)
{
	short w, h;
	if (vst_font(vdi_h, f->id) != f->id)
		return 0;
	vst_point(vdi_h, 12, &w, &h);
	if (w < 1)
		return 0;
	return vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'W') &&
	       vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'm');
}

/* only monospaced fonts are listed. Measuring means loading each font,
   slow with hundreds of vector fonts: the answers are kept in ECAL.FNT
   ("1 Courier 10 Pitch", "0 Swiss 721") and only new fonts measured */
static void keep_monospaced(void)
{
	char path[220], *known, *line;
	SBUF out;
	short i, n = 1, done = 0, todo = 0;
	path_join(path, sizeof(path), opt.workdir, "ECAL.FNT");
	known = pf_load(path, 0);
	for (i = 1; i < nfam; i++) {
		fam[i].mono = -1;
		for (line = known; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : 0) {
			size_t len = strcspn(line + 2, "\r\n");
			if (line[1] == ' ' && len == strlen(fam[i].name) && !strncmp(line + 2, fam[i].name, len)) {
				fam[i].mono = line[0] == '1';
				break;
			}
		}
		if (fam[i].mono < 0)
			todo++;
	}
	if (todo) {
		busy(1);
		for (i = 1; i < nfam; i++)
			if (fam[i].mono < 0) {
				char m[80];
				snprintf(m, sizeof(m), "Looking for fixed-width fonts: %d of %d...", ++done, todo);
				status(m);
				fam[i].mono = (signed char)measure_mono(&fam[i]);
			}
		busy(0);
		status_updated();
		sb_init(&out);
		for (i = 1; i < nfam; i++)
			sb_printf(&out, "%d %s\r\n", fam[i].mono, fam[i].name);
		if (out.s)
			pf_save(path, out.s, out.len);
		sb_free(&out);
		font_reselect();
	}
	free(known);
	for (i = 1; i < nfam; i++)
		if (fam[i].mono)
			fam[n++] = fam[i];
	nfam = n;
}

/* the format of the font with this id (0 not known, 1 bitmap, 2 Speedo, 4 TrueType,
   8 Type 1), from the list or the VDI */
static short format_of(short id)
{
	short i, n;
	char name[34];
	for (i = 0; i < nfam; i++)
		if (fam[i].id == id)
			return fam[i].format;
	n = gdos_fonts > 0 ? gdos_fonts : 0;
	for (i = 2; i <= n + 1; i++)
		if (vqt_name(vdi_h, i, name) == id)
			return vqt_font_format(vdi_h, i);
	return 0;	/* not known (NVDI draws its fonts unloaded too): the VDI is asked */
}

/* names only: quick, whatever the number of fonts */
static void list_families(void)
{
	short i, k;
	if (nfam)
		return;
	if (gdos_fonts < 0) {
		busy(1);
		gdos_fonts = vq_gdos() ? vst_load_fonts(vdi_h, 0) : 0;
		busy(0);
	}
	maxfam = gdos_fonts + 1 < MAXFAM ? gdos_fonts + 1 : MAXFAM;
	fam = maxfam > 1 ? malloc(maxfam * sizeof(FAM)) : 0;
	if (!fam) {
		fam = &fam_sys;		/* the system font, at least */
		maxfam = 1;
	}
	fam[0].id = 1;
	fam[0].format = 1;
	fam[0].mono = 1;
	strcpy(fam[0].name, "System font");
	nfam = 1;
	for (i = 2; i <= gdos_fonts + 1 && nfam < maxfam; i++) {
		FAM *f = &fam[nfam];
		short n;
		f->id = vqt_name(vdi_h, i, f->name);
		if (f->id <= 1)
			continue;
		for (k = 1; k < nfam; k++)
			if (fam[k].id == f->id)
				break;
		if (k < nfam)
			continue;
		for (n = 31; n >= 0 && (f->name[n] == ' ' || !f->name[n]); n--)
			f->name[n] = 0;
		f->format = vqt_font_format(vdi_h, i);
		f->mono = -1;
		nfam++;
	}
	keep_monospaced();
}

/* the sizes of a family, and whether it is monospaced */
static void look_at(FAM *f)
{
	static const short try[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36 };
	short i, w, h;
	nsizes = 0;
	if (f->id == 1) {
		sizes[0] = 8;
		strcpy(size_label[0], "8 x 8");
		sizes[1] = 16;
		strcpy(size_label[1], "8 x 16");
		nsizes = 2;
		return;
	}
	busy(1);
	if (vst_font(vdi_h, f->id) != f->id) {
		f->mono = 0;
		busy(0);
		return;
	}
	for (i = 0; i < (short)(sizeof(try) / sizeof(try[0])) && nsizes < MAXSIZE; i++) {
		short got = vst_point(vdi_h, try[i], &w, &h);
		if (w < 4 || h < 6 || (nsizes && sizes[nsizes - 1] >= got))
			continue;
		if (f->mono < 0)
			f->mono = vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'W') &&
				  vqt_width(vdi_h, 'i') == vqt_width(vdi_h, 'm');
		sizes[nsizes] = got;
		snprintf(size_label[nsizes], sizeof(size_label[0]), "%d pt", got);
		nsizes++;
	}
	if (f->mono < 0)
		f->mono = 0;
	if (!f->mono)
		nsizes = 0;
	busy(0);
}

static const char *format_name(FAM *f)
{
	if (f->id == 1)
		return "Bitmap (built in)";
	switch (f->format) {
	case 1: return "Bitmap (GDOS)";
	case 2: return "Speedo";
	case 4: return "TrueType";
	case 8: return "Type 1";
	}
	return "GDOS font";
}

/* ---------------- the dialog's tree, built in code as EMail's ---------------- */

#define DMAX 24
static OBJECT tree[DMAX];
static short nobj, last_child;

static void d_begin(short w, short h)
{
	memset(tree, 0, sizeof(tree));
	nobj = 1;
	last_child = 0;
	tree[0].ob_next = -1;
	tree[0].ob_head = tree[0].ob_tail = -1;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = 0x00021100L;
	tree[0].ob_width = w;
	tree[0].ob_height = h;
}

static short d_add(short type, short flags, short state, long spec, short x, short y, short w, short h)
{
	short i = nobj++;
	OBJECT *o = &tree[i];
	o->ob_next = 0;
	o->ob_head = o->ob_tail = -1;
	o->ob_type = type;
	o->ob_flags = flags;
	o->ob_state = state;
	o->ob_spec = spec;
	o->ob_x = x;
	o->ob_y = y;
	o->ob_width = w;
	o->ob_height = h;
	if (last_child)
		tree[last_child].ob_next = i;
	else
		tree[0].ob_head = i;
	tree[0].ob_tail = i;
	last_child = i;
	return i;
}

static short d_text(short x, short y, const char *s)
{
	return d_add(G_STRING, 0, 0, (long)s, x, y, (short)strlen(s), 1);
}

static short d_button(short x, short y, short w, const char *s, short flags)
{
	return d_add(G_BUTTON, SELECTABLE | flags, 0, (long)s, x, y, w, 1);
}

static void d_end(void)
{
	short i;
	tree[last_child].ob_next = 0;
	tree[nobj - 1].ob_flags |= LASTOB;
	for (i = 0; i < nobj; i++)
		rsrc_obfix(tree, i);
}

/* ---------------- a list box ---------------- */

typedef struct {
	short obj;		/* its G_BOX */
	GRECT r;		/* inside the border */
	short n, top, sel, rows;
	int list_of_sizes;
} LIST;

static OBJECT *tr;
static LIST lf, ls;
static short sel_heb;

static short sbar(void)
{
	return 2 * gl_wchar;
}

static void list_place(LIST *l)
{
	short x, y;
	objc_offset(tr, l->obj, &x, &y);
	l->r.x = x + 1;
	l->r.y = y + 1;
	l->r.w = tr[l->obj].ob_width - 2;
	l->r.h = tr[l->obj].ob_height - 2;
	l->rows = l->r.h / gl_hchar;
}

static void outline(short x, short y, short w, short h)
{
	GRECT r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	frame(&r, 1);
}

static void arrow(short x, short y, short w, short h, int up)
{
	short cx = x + w / 2, cy = y + h / 2, k = h >= 14 ? 3 : 2;
	short y1 = up ? cy + k / 2 + 1 : cy - k / 2 - 1, y2 = up ? cy - k / 2 - 1 : cy + k / 2 + 1;
	outline(x, y, w, h);
	line_col(cx - k, y1, cx, y2, 1);
	line_col(cx, y2, cx + k, y1, 1);
}

static void list_draw(LIST *l)
{
	GRECT r = l->r, t;
	short i, sw = sbar(), cols = (r.w - sw - 4) / gl_wchar, ah = gl_hchar;
	use_sys();
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	for (i = 0; i < l->rows && l->top + i < l->n; i++) {
		short k = l->top + i, y = r.y + i * gl_hchar, fl = 0;
		const char *s = l->list_of_sizes ? size_label[k] : fam[k].name;
		if (!l->list_of_sizes && !fam[k].mono)
			fl = TX_DIM;
		if (k == l->sel) {
			t.x = r.x;
			t.y = y;
			t.w = r.w - sw;
			t.h = gl_hchar;
			fill(&t, 1);
			fl = TX_INVERSE;
		}
		text_at(r.x + 2, y, s, strlen(s), cols, fl | TX_LTR);
	}
	/* the scroll bar */
	t.x = r.x + r.w - sw;
	t.y = r.y;
	t.w = sw;
	t.h = r.h;
	fill(&t, 0);
	arrow(t.x, t.y, sw, ah, 1);
	arrow(t.x, t.y + t.h - ah, sw, ah, 0);
	{
		short ty = t.y + ah, th = t.h - 2 * ah;
		GRECT g;
		g.x = t.x + 1;
		g.y = ty;
		g.w = sw - 2;
		g.h = th;
		fill_pat(&g, 1, 2, 1);
		if (l->n > l->rows) {
			short h = (short)((long)th * l->rows / l->n), y;
			if (h < 6)
				h = 6;
			y = ty + (short)((long)(th - h) * l->top / (l->n - l->rows));
			t.y = y;
			t.h = h;
			fill(&t, 0);
			outline(t.x, y, sw, h);
		} else {
			t.y = ty;
			t.h = th;
			fill(&t, 0);
		}
		outline(r.x + r.w - sw, r.y, sw, r.h);
	}
	graf_mouse(M_ON, 0);
}

static void list_show(LIST *l, short k)
{
	if (k < l->top)
		l->top = k;
	if (k >= l->top + l->rows)
		l->top = k - l->rows + 1;
	if (l->top > l->n - l->rows)
		l->top = l->n - l->rows;
	if (l->top < 0)
		l->top = 0;
}

/* ---------------- the dialog ---------------- */

static short o_ok, o_type, o_prev, o_heb[3];
static char type_line[64];

static void draw_obj(short obj)
{
	short x, y;
	objc_offset(tr, obj, &x, &y);
	objc_draw(tr, 0, 8, x - 2, y - 2, tr[obj].ob_width + 4, tr[obj].ob_height + 4);
}

static short chosen_pt(void)
{
	return ls.sel >= 0 && ls.sel < nsizes ? sizes[ls.sel] : 0;
}

static void draw_preview(void)
{
	static const char l1[] = "AaBbCc 0123 The quick brown fox";
	/* shalom, olam (logical order, Atari character set) */
	static const char l2[] = "\xD6\xCD\xC7\xDA \xD1\xC7\xCD\xDA - Hebrew";
	GRECT r;
	short keep = opt.hebfont, x, y;
	objc_offset(tr, o_prev, &x, &y);
	r.x = x + 1;
	r.y = y + 1;
	r.w = tr[o_prev].ob_width - 2;
	r.h = tr[o_prev].ob_height - 2;
	graf_mouse(M_OFF, 0);
	clip_on(&r);
	fill(&r, 0);
	if (fam[lf.sel].mono && set_font(fam[lf.sel].id, chosen_pt())) {
		opt.hebfont = sel_heb;
		text_at(r.x + cw, r.y + 2, l1, strlen(l1), (r.w - 2 * cw) / cw, TX_LTR);
		text_at(r.x + cw, r.y + 4 + ch, l2, strlen(l2), (r.w - 2 * cw) / cw, TX_LTR);
		opt.hebfont = keep;
	} else {
		use_sys();
		text_at(r.x + gl_wchar, r.y + 2, "Proportional: ECal needs a fixed-width font.", 43,
			(r.w - 2 * gl_wchar) / gl_wchar, TX_LTR);
	}
	use_sys();
	clip_off();
	graf_mouse(M_ON, 0);
}

static void draw_type(void)
{
	FAM *f = &fam[lf.sel];
	short x, y, n;
	GRECT r;
	if (f->id != 1 && f->format != 1) {
		/* setting it says where its Hebrew comes from */
		set_font(f->id, chosen_pt() ? chosen_pt() : 12);
		snprintf(type_line, sizeof(type_line), "%s, Hebrew %s", format_name(f),
			 heb_mode == HM_UNICODE ? "from the font" :
			 heb_why == HW_NOHEB ? "in the system font (none in it)" :
			 heb_why == HW_NOLOOKUP ? "in the system font (no NVDI 5)" : "as the font has it");
		use_sys();
	} else {
		snprintf(type_line, sizeof(type_line), "%s", format_name(f));
	}
	objc_offset(tr, o_type, &x, &y);
	r.x = x;
	r.y = y;
	r.w = tr[o_type].ob_width;
	r.h = tr[o_type].ob_height;
	graf_mouse(M_OFF, 0);
	use_sys();
	clip_on(&r);
	fill(&r, 0);
	n = (short)strlen(type_line);
	text_at(x, y, type_line, n, r.w / gl_wchar, TX_LTR);
	clip_off();
	graf_mouse(M_ON, 0);
}

static void set_ok(void)
{
	short dis = !fam[lf.sel].mono || !nsizes;
	short was = (tr[o_ok].ob_state & DISABLED) != 0;
	if (dis == was)
		return;
	if (dis)
		tr[o_ok].ob_state |= DISABLED;
	else
		tr[o_ok].ob_state &= ~DISABLED;
	draw_obj(o_ok);
}

/* a family was picked: its sizes, keeping the size if it has it */
static void family_changed(int draw)
{
	short want = chosen_pt(), i, best = 0;
	if (!want)
		want = opt.font_id == 1 ? (gl_hchar >= 16 ? 16 : 8) : opt.font_pt;
	look_at(&fam[lf.sel]);
	ls.n = nsizes;
	for (i = 0; i < nsizes; i++)
		if (sizes[i] <= want)
			best = i;
	if (fam[lf.sel].id == 1)
		best = want >= 16 ? 1 : 0;
	ls.sel = nsizes ? best : -1;
	ls.top = 0;
	list_show(&ls, ls.sel < 0 ? 0 : ls.sel);
	if (draw) {
		list_draw(&ls);
		draw_type();
		draw_preview();
		set_ok();
	}
}

static short button_down(short *mx, short *my)
{
	short mb, ks;
	graf_mkstate(mx, my, &mb, &ks);
	return mb & 3;
}

/* a click in a list: 1 = a double click on an entry */
static int list_click(LIST *l, short mx, short my, short clicks)
{
	short sw = sbar(), ah = gl_hchar, x, y;
	if (mx >= l->r.x + l->r.w - sw) {
		short first = 1;
		if (my < l->r.y + ah || my >= l->r.y + l->r.h - ah) {
			short d = my < l->r.y + ah ? -1 : 1;
			do {
				short old = l->top;
				l->top += d;
				list_show(l, l->top < old ? l->top : l->top + l->rows - 1);
				if (l->top != old)
					list_draw(l);
				evnt_timer_(first ? 250 : 50);
				first = 0;
			} while (button_down(&x, &y));
		} else {
			short track_h = l->r.h - 2 * ah, mid = l->r.y + ah + track_h / 2;
			l->top += my < mid ? -l->rows : l->rows;
			if (l->top > l->n - l->rows)
				l->top = l->n - l->rows;
			if (l->top < 0)
				l->top = 0;
			list_draw(l);
		}
		return 0;
	}
	{
		short k = l->top + (my - l->r.y) / gl_hchar;
		if (k < 0 || k >= l->n)
			return 0;
		if (k != l->sel) {
			l->sel = k;
			list_draw(l);
			if (l == &lf)
				family_changed(1);
			else
				draw_preview();
		}
		return clicks >= 2;
	}
}

static void wait_up(void)
{
	short mx, my;
	while (button_down(&mx, &my))
		evnt_timer_(10);
}

void font_menu(void)
{
	static char heb_label[3][24];
	short i, x, y, w, h, o_cancel, o_fam, o_size, done = 0, ok = 0, m[8];
	AEVENT e;

	if (boot_fonts && !gdos_fonts && vq_gdos()) {
		alert(1, "[1][The accessory loads GDOS fonts|when the machine starts, and|only when one is chosen. Choose|it in ECAL.PRG (Options > Font),|then restart.][ OK ]");
		return;
	}
	list_families();
	sel_heb = opt.hebfont;
	lf.sel = 0;
	for (i = 0; i < nfam; i++)
		if (fam[i].id == opt.font_id)
			lf.sel = i;
	lf.n = nfam;
	lf.top = 0;
	lf.list_of_sizes = 0;
	ls.list_of_sizes = 1;
	ls.sel = -1;
	family_changed(0);

	/* samples of shalom as each kind of font would show it */
	strcpy(heb_label[0], "Atari  \xDA\xC7\xCD\xD6");
	strcpy(heb_label[1], "ISO 8859-8  \xED\xE5\xEC\xF9");
	strcpy(heb_label[2], "DOS 862  \x8D\x85\x8C\x99");

	/* fits ST medium too: 23 rows of 8 pixels */
	d_begin(64, 23);
	d_add(G_STRING, 0, 0, (long)"Font for the text", 2, 1, 17, 1);
	d_text(2, 3, "Family");
	d_text(39, 3, "Size");
	o_fam = d_add(G_BOX, TOUCHEXIT, 0, 0x00FF1100L, 2, 4, 35, 9);
	o_size = d_add(G_BOX, TOUCHEXIT, 0, 0x00FF1100L, 39, 4, 12, 9);
	o_ok = d_button(53, 4, 9, "OK", EXIT | DEFAULT);
	o_cancel = d_button(53, 6, 9, "Cancel", EXIT);
	d_text(2, 14, "Type:");
	o_type = d_add(G_IBOX, 0, 0, 0L, 8, 14, 54, 1);
	d_text(2, 16, "Hebrew in bitmap fonts: pick the sample that reads right");
	for (i = 0; i < 3; i++)
		o_heb[i] = d_add(G_BUTTON, SELECTABLE | RBUTTON, i == sel_heb ? SELECTED : 0,
				 (long)heb_label[i], 2 + i * 20, 17, 18, 1);
	o_prev = d_add(G_BOX, 0, 0, 0x00FF1100L, 2, 19, 60, 3);
	d_end();
	tr = tree;
	lf.obj = o_fam;
	ls.obj = o_size;
	if (!fam[lf.sel].mono || !nsizes)
		tr[o_ok].ob_state |= DISABLED;

	form_center(tr, &x, &y, &w, &h);
	wind_update(BEG_UPDATE);
	wind_update(3);
	form_dial(FMD_START, x, y, w, h);
	objc_draw(tr, 0, 8, x, y, w, h);
	list_place(&lf);
	list_place(&ls);
	list_show(&lf, lf.sel);
	list_show(&ls, ls.sel < 0 ? 0 : ls.sel);
	list_draw(&lf);
	list_draw(&ls);
	draw_type();
	draw_preview();
	wait_up();

	while (!done) {
		evnt_multi_(MU_KEYBD | MU_BUTTON, 2, 1, 1, 0, m, &e);
		if (e.which & MU_KEYBD) {
			short sc = KEY_SCAN(e.kreturn), as = KEY_ASCII(e.kreturn);
			if (as == 0x1b || sc == 0x61) {
				done = 1;
			} else if (as == 0x0d || sc == 0x72) {
				if (!(tr[o_ok].ob_state & DISABLED))
					done = ok = 1;
			} else if ((sc == 0x48 && lf.sel > 0) || (sc == 0x50 && lf.sel < nfam - 1)) {
				lf.sel += sc == 0x48 ? -1 : 1;
				list_show(&lf, lf.sel);
				list_draw(&lf);
				family_changed(1);
			} else if ((sc == 0x4b && ls.sel > 0) || (sc == 0x4d && ls.sel >= 0 && ls.sel < nsizes - 1)) {
				ls.sel += sc == 0x4b ? -1 : 1;
				list_show(&ls, ls.sel);
				list_draw(&ls);
				draw_preview();
			}
		}
		if (e.which & MU_BUTTON) {
			short obj = objc_find(tr, 0, 8, e.mx, e.my);
			if (obj == o_fam || obj == o_size) {
				if (list_click(obj == o_fam ? &lf : &ls, e.mx, e.my, e.breturn) &&
				    !(tr[o_ok].ob_state & DISABLED))
					done = ok = 1;
			} else if (obj == o_heb[0] || obj == o_heb[1] || obj == o_heb[2]) {
				for (i = 0; i < 3; i++) {
					short on = obj == o_heb[i];
					if (on != ((tr[o_heb[i]].ob_state & SELECTED) != 0)) {
						tr[o_heb[i]].ob_state ^= SELECTED;
						draw_obj(o_heb[i]);
					}
					if (on)
						sel_heb = i;
				}
				draw_preview();
				wait_up();
			} else if ((obj == o_ok && !(tr[o_ok].ob_state & DISABLED)) || obj == o_cancel) {
				tr[obj].ob_state |= SELECTED;
				draw_obj(obj);
				wait_up();
				tr[obj].ob_state &= ~SELECTED;
				done = 1;
				ok = obj == o_ok;
			}
		}
	}
	form_dial(FMD_FINISH, x, y, w, h);
	wind_update(2);
	wind_update(END_UPDATE);
	if (ok) {
		opt.font_id = fam[lf.sel].id;
		opt.font_pt = chosen_pt();
		opt.hebfont = sel_heb;
	}
	font_apply();
	if (ok) {
		settings_later();
		win_layout();
		win_redraw(0);
	}
}
