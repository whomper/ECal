/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after views.o: the day view's fix in collect_for (its
   list not valid until its days' table is filled, 25 Nov 2026) made its
   code 6 bytes longer. The pad puts what follows back at 2.59's
   addresses modulo 256, as the 68030's caches see them (the guidelines'
   Speed; checked symbol by symbol against 2.59's ELF). When views.c
   changes size again, size it again the same way. */
#define PAD_NAME pad_views
#define PAD_TEXT 250
#include "../common/atari/codepad.S"
