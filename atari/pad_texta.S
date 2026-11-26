/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* The accessory's only, after pad_text: draw.c's use of common's scr_text
   (26 Nov 2026) links it into the accessory, where only dialog.c's weak
   reference had named it (ECAL.PRG had it already), its code and its two statics. The pad puts what
   follows back at 2.59's addresses modulo 256 (the guidelines' Speed;
   checked symbol by symbol against 2.59's ELF). */
#define PAD_NAME pad_texta
#define PAD_TEXT 180
#define PAD_BSS 246
#include "../common/atari/codepad.S"
