/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after ovl.o: common's sync of 25 Nov 2026 (AERA-Atari bfc4656) changed its size.
   The pad puts what follows back at 2.59's addresses modulo 256, as
   the 68030's caches see them (the guidelines' Speed; checked symbol
   by symbol against 2.59's ELF with common/tools/profpad.py). When
   ovl changes size again, size it again the same way. */
#define PAD_NAME pad_ovl
#define PAD_TEXT 154
#include "../common/atari/codepad.S"
