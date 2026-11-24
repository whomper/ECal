/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after field.o: the date picker's opening (open_date, key_again) made its code 124 bytes longer and its BSS 4.
   It puts what follows back at 2.58's addresses modulo 256, as the
   68030's caches see them (the guidelines' Speed; checked symbol by
   symbol by common/tools/profpad.py against 2.58's ELF). When field.c
   changes size again, size it again the same way. */
#define PAD_NAME pad_field
#define PAD_TEXT 132
#define PAD_BSS 252
#include "../common/atari/codepad.S"
