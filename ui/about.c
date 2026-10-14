/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * about.c - ECal's About box (the AERA programs' shared one,
 * common/ui/about.c, with ECal's status line) and its icon in an
 * iconified window.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "aboutbox.h"
#include "icon.h"
#include "icon16.h"
#include "../src/version.h"
#include "../src/plat.h"

static void rect_fill(short x1, short y1, short x2, short y2, short color)
{
	GRECT r;
	r.x = x1;
	r.y = y1;
	r.w = x2 - x1 + 1;
	r.h = y2 - y1 + 1;
	fill(&r, color);
}

/* today, short: "Oct 14" */
void icon_today(char *s, int size)
{
	DT now;
	ct_split(now_view(), &now);
	snprintf(s, size, "%.3s %d", month_names[now.mon - 1], now.day);
}

/* The iconified window: the icon, and today's date under it when there
 * is room. The desktop's palette is everyone's, so the colour icon is
 * drawn in the standard pens here: white, black, red, green, the greys. */
void icon_window(GRECT *work, GRECT *clip)
{
	static const short std[16] = { 0, 1, 0, 0, 0, 0, 0, 0, 8, 9, 1, 1, 0, 2, 3, 9 };
	char s[12];
	short n, x, y, room;
	icon_today(s, sizeof(s));
	n = (short)strlen(s);
	room = work->h >= ICON_H + ch + 2 && work->w >= n * cw;
	x = work->x + (work->w - ICON_W) / 2;
	y = work->y + (work->h - ICON_H - (room ? ch + 1 : 0)) / 2;
	clip_on(clip);
	rect_fill(work->x, work->y, work->x + work->w - 1, work->y + work->h - 1, 0);
	if (ncolors >= 16) {
		about_icon16(icon16_px, x, y, 1, 1, std);
	} else {
		/* in runs of fills, which keep to the clip as a raster copy wouldn't */
		short r, c, e;
		for (r = 0; r < ICON_H; r++)
			for (c = 0; c < ICON_W; c = e) {
				short on = (icon_bits[r * 2 + c / 16] >> (15 - (c & 15))) & 1;
				for (e = c + 1; e < ICON_W && ((icon_bits[r * 2 + e / 16] >> (15 - (e & 15))) & 1) == on; e++)
					;
				if (on)
					rect_fill(x + c, y + r, x + e - 1, y + r, 1);
			}
	}
	if (room)
		text_at(work->x + (work->w - n * cw) / 2, y + ICON_H + 1, s, n, n, TX_BOLD | TX_LTR);
	clip_off();
}

void dlg_about(void)
{
	static char conn[100];
	const char *ram = about_ram();
	ABOUT a = { "ECal", cal_version, "A calendar for the Atari ST, TT and Falcon", { conn, 0, 0 }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb };
	snprintf(conn, sizeof(conn), "%s  \xF9  %s  \xF9  %s%s%s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 opt.falcon ? (strcmp(falcon_dsp_state(), "DSP") ? "Falcon" : "Falcon/DSP") : opt.gate[0] ? "Gateway" : "No gateway",
		 opt.offline ? "offline" : "ready", ram[0] ? "  \xF9  " : "", ram);
	about_show(&a);
}
