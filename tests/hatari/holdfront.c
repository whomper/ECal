/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* A stand-in for a program started at boot (TOS 1.04 and 4.04 can start
   one), for tests/hatari/small_acc.py: it is the program in front while
   the accessories take their first turn, waits HOLD seconds in the AES so
   they do, then quits. What an accessory took while it was in front and
   didn't take as its own goes back to TOS with it (the hub, 26 Nov 2026). */
#include <tos.h>
#include "gem.h"

#define HOLD 30

int main(void)
{
	short id = appl_init(), i;
	if (id < 0)
		return 1;
	for (i = 0; i < HOLD; i++)
		evnt_timer_(1000);
	appl_exit();
	return 0;
}
