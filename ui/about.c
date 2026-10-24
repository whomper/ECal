/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * about.c - ECal's About box (the AERA programs' shared one,
 * common/ui/aboutbox.c, with ECal's status line) and its icon in an
 * iconified window (common/ui/iconify.c).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "aboutbox.h"
#include "iconify.h"
#include "tlsovl.h"
#include "icon.h"
#include "icon16.h"
#include "icon256.h"
#include "../src/version.h"
#include "../src/plat.h"

/* today, short: "Oct 14" */
void icon_today(char *s, int size)
{
	DT now;
	ct_split(now_view(), &now);
	snprintf(s, size, "%.3s %d", month_names[now.mon - 1], now.day);
}

/* The iconified window: the icon, and today's date under it when there
 * is room (common/ui/iconify.c), over the small window as it is now */
void icon_window(GRECT *area)
{
	char s[12];
	icon_today(s, sizeof(s));
	icon_redraw(win_h, area, icon_bits, icon16_px, s);
}

void dlg_about(void)
{
	static char conn[100];
	char direct[48];
	const char *ram = about_ram(), *ver = tls_ovl_version();
	ABOUT a = { "ECal", cal_version, "A calendar for the Atari ST, TT and Falcon", { conn, 0, 0 }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	/* "Direct connection/DSP (TLS.OVL 1.0)": TLS.OVL's version once it is
	   loaded (the guidelines, 7.4) */
	snprintf(direct, sizeof(direct), "Direct connection%s%s%s%s", strcmp(falcon_dsp_state(), "DSP") ? "" : "/DSP",
		 ver[0] ? " (TLS.OVL " : "", ver, ver[0] ? ")" : "");
	snprintf(conn, sizeof(conn), "%s  \xF9  %s  \xF9  %s%s%s",
		 strcmp(net_stack(), "none") ? net_stack() : "no network",
		 opt.falcon ? direct : opt.gate[0] ? "Gateway" : "No gateway",
		 opt.offline ? "offline" : "ready", ram[0] ? "  \xF9  " : "", ram);
	about_show(&a);
}
