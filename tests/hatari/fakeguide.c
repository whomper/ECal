/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/* A stand-in for ST-Guide in the Hatari tests: an accessory called
   ST-GUIDE.ACC, so the AES knows it as "ST-GUIDE". What VA_START hands it
   goes into C:\GUIDE.TXT for the test to read. */
#include <tos.h>
#include "gem.h"

int main(void)
{
	short msg[8], id = appl_init();
	AEVENT ev;
	menu_register(id, "  ST-Guide (test)");
	for (;;) {
		evnt_multi_(MU_MESAG, 0, 0, 0, 0, msg, &ev);
		if (msg[0] == 0x4711) {
			const char *s = (const char *)(((long)msg[3] << 16) | (unsigned short)msg[4]);
			short n = 0, h = (short)Fcreate("C:\\GUIDE.TXT", 0);
			while (s && s[n])
				n++;
			if (h >= 0) {
				Fwrite(h, n, s);
				Fclose(h);
			}
		}
	}
	return 0;
}
