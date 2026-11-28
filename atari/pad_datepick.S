/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after datepick.o in ECAL.ACC (pad_datepickp.S in ECAL.PRG): linked last, its strings (in the block the linker merges every string into, at the start of the read-only data) and read-only data move what follows them.
   It puts what follows back at 2.58's addresses modulo 256, as the
   68030's caches see them (the guidelines' Speed; checked symbol by
   symbol by common/tools/profpad.py against 2.58's ELF). When datepick.c
   changes size again, size it again the same way. */
#define PAD_NAME pad_datepick
#define PAD_STR 88
#define PAD_RO 240
#include "../common/atari/codepad.S"
