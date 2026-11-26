/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after views.o: the day view's fix in collect_for (its
   list not valid until its days' table is filled, 25 Nov 2026) and an
   event's lines outside the clip left out (26 Nov 2026), and the day's
   and week's events drawn apart from the grid (2.61) changed its
   size. The pad puts what follows back at 2.60's
   addresses modulo 256, as the 68030's caches see them (the guidelines'
   Speed; checked symbol by symbol against 2.60's ELF). When views.c
   changes size again, size it again the same way. */
#define PAD_NAME pad_views
#define PAD_TEXT 46
#define PAD_BSS 252
#include "../common/atari/codepad.S"
