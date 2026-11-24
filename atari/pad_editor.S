/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after editor.o: the end date that follows the start (end_follows) made its code 230 bytes longer.
   It puts what follows back at 2.58's addresses modulo 256, as the
   68030's caches see them (the guidelines' Speed; checked symbol by
   symbol by common/tools/profpad.py against 2.58's ELF). When editor.c
   changes size again, size it again the same way. */
#define PAD_NAME pad_editor
#define PAD_TEXT 26
#include "../common/atari/codepad.S"
