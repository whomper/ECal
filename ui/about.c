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
#include "aboutline.h"
#include "../src/http.h"
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

/* The About box's status lines in the family's words (the guidelines,
 * 1.3; Erez, 27 Oct 2026): the connection, what ECal holds, the memory */
void dlg_about(void)
{
	static char net[100], mine[60], mem[80];
	char kept[64];
	long events = 0;
	int i;
	ABOUT a = { "ECal", cal_version, "A calendar for the Atari ST, TT and Falcon", { net, mine, mem }, { 0, 0, 0 },
		    icon_bits, icon16_px, icon16_rgb, icon256_px, icon256_rgb };
	http_kept_host(kept, sizeof(kept));
	about_net_line(net, sizeof(net), net_stack(),
		       opt.falcon ? ABOUT_DIRECT : opt.gate[0] ? ABOUT_GATEWAY : ABOUT_NO_GATEWAY,
		       opt.offline ? ABOUT_OFFLINE : kept[0] ? ABOUT_ONLINE : ABOUT_READY);
	for (i = 0; i < ncals; i++)
		events += cals[i].nev;
	if (!calendars_read())
		str_copy(mine, "The calendars not read yet", sizeof(mine));
	else
		snprintf(mine, sizeof(mine), "%ld event%s in %d calendar%s", events, events == 1 ? "" : "s", ncals,
			 ncals == 1 ? "" : "s");
	about_mem_line(mem, sizeof(mem));
	about_show(&a);
}
