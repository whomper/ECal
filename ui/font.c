/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * font.c - the font for ECal's window (menu > Font...); dialogs and
 * popups keep the system font. The Font dialog is the family's
 * (common/ui/fontsel.c, the guidelines 5.3): the fixed-width fonts with
 * letters, measured once and kept in ECAL.FNT. What is ECal's is here:
 * the font set on the screen's workstation and the window's bitmap,
 * where its Hebrew comes from, and what OK does.
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
#include "fontsel.h"

static short gdos_fonts = -1;	/* loaded on vdi_h: vst_load_fonts' count */
static short boot_fonts;	/* the accessory: its fonts settled at boot (font_boot) */
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

/* whether another workstation (win.c's bitmap) loads them too: only
   when the window's has (the program loads them when a font is chosen,
   and win_buffer_fonts then) */
int font_gdos_loaded(void)
{
	return gdos_fonts > 0;
}

void font_apply(void)
{
	if (sys_depth) {
		use_sys();
		return;
	}
	if (opt.font_id != 1 && gdos_fonts < 0 && vq_gdos()) {
		gdos_fonts = vst_load_fonts(vdi_h, 0);
		win_buffer_fonts();
	}
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

/* the window's own font for a while, however many dialogs are open: the
   window drawn again under a dialog window (dlg_redraw_hook) */
short font_sys_suspend(void)
{
	short k = sys_depth;
	if (k) {
		sys_depth = 0;
		font_apply();
	}
	return k;
}

void font_sys_resume(short k)
{
	if (k) {
		sys_depth = k;
		use_sys();
	}
}

/* ---------------- the fonts ---------------- */

/* the format of the font with this id (0 not known, 1 bitmap, 2 Speedo,
   4 TrueType, 8 Type 1), from the VDI */
static short format_of(short id)
{
	static short known_id = -1, known_format;
	short i, n = gdos_fonts > 0 ? gdos_fonts : 0;
	char name[34];
	if (id == known_id)
		return known_format;
	for (i = 2; i <= n + 1; i++)
		if (vqt_name(vdi_h, i, name) == id) {
			known_id = id;
			return known_format = vqt_font_format(vdi_h, i);
		}
	return 0;	/* not known (NVDI draws its fonts unloaded too): the VDI is asked */
}

/* ---------------- the dialog (common/ui/fontsel.c) ---------------- */

/* OK: the font for the window, kept in ECAL.INF. The accessory's fonts
   of the next start stay chosen until then (font_apply) */
static void take(fontsel *s)
{
	opt.font_id = s->id;
	opt.font_pt = s->pt;
	font_apply();
	settings_later();
	win_layout();
	win_redraw(0);
	if (s->later)
		alert(1, "[1][ECal uses %s from the next start: the accessory loads its fonts only as the Atari starts.][ OK ]",
		      s->font);
}

void font_menu(void)
{
	static fontsel fs;
	static char fnt[220];
	path_join(fnt, sizeof(fnt), opt.workdir, "ECAL.FNT");
	memset(&fs, 0, sizeof(fs));
	fs.app = "ECal";
	fs.fnt = fnt;
	fs.id = opt.font_id;
	fs.pt = opt.font_pt;
	/* the accessory without them (FONTGDOS, SpeedoGDOS, none named at
	   boot): ECAL.FNT's for the next start */
	fs.gdos = gdos_fonts < 0 || (boot_fonts && !gdos_fonts) ? FS_UNLOADED : gdos_fonts;
	fs.acc = boot_fonts;
	fs.apply = take;
	fontsel_run(&fs);
	if (!boot_fonts && fs.gdos >= 0)
		gdos_fonts = fs.gdos;	/* the program loads them when first wanted */
}
