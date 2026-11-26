/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after alert.o: common's sync of 25 Nov 2026 (AERA-Atari bfc4656) changed its size.
   The pad puts what follows back at 2.59's addresses modulo 256, as
   the 68030's caches see them (the guidelines' Speed; checked symbol
   by symbol against 2.59's ELF with common/tools/profpad.py). When
   alert changes size again, size it again the same way. */
#define PAD_NAME pad_alert
#define PAD_TEXT 70
#define PAD_DATA 236
#include "../common/atari/codepad.S"
