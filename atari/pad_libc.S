/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* Linked right after libc.o: common's acc_core and acc_free (9fd1335, an
   accessory's memory its own on plain TOS) made it longer. The pad puts
   what follows (tos, gem's AES and VDI calls, tls_ovl, main) back at
   2.60's addresses modulo 256, as the 68030's caches see them (the
   guidelines' Speed). When libc changes size again, size it again the
   same way. */
#define PAD_NAME pad_libc
#define PAD_TEXT 166
#include "../common/atari/codepad.S"
