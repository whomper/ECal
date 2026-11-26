/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after draw.o: an event's frame and strip in one line, its
   write mode and the 68000's text by the CPU (26 Nov 2026), and the write
   mode kept for solid fills and lines (2.61), changed its size. The pad
   puts what follows back at 2.60's addresses modulo 256,
   as the 68030's caches see them (the guidelines' Speed; checked symbol
   by symbol against 2.60's ELF). When draw.c changes size again, size it
   again the same way. */
#define PAD_NAME pad_draw
#define PAD_TEXT 44
#define PAD_BSS 244
#include "../common/atari/codepad.S"
