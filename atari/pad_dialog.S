/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after dialog.o, in the accessory only (the program links
   dialog_cpu.o at its end instead): common's app_redraw (998cc2c, the
   app's own font for its windows under a family dialog) made it longer.
   The pad puts what follows (the people files, notify, aboutline and
   text.o's drawing) back at 2.61's addresses modulo 256, as the 68030's
   caches see them (the guidelines' Speed). When dialog changes size
   again, size it again the same way. */
#define PAD_NAME pad_dialog
#define PAD_TEXT 202
#include "../common/atari/codepad.S"
