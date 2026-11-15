| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| Linked right after ovl.o: TLSFB.OVL's change (AERA-Atari a99ea65) made
| ovl.c's code 42 bytes longer, and everything after it moved; with the
| 68030's cache that cost the start 15 ms a slice and its first picture
| 320 ms on Erez's configuration (Falcon-4:256, NVDI). The pad puts the
| code after it back at 2.51's addresses mod 256 (42 + 214 = 256).
| Kept by -Wl,-u,pad_ovl (the link drops unused sections). When ovl.c
| changes size again, resize it: 256 minus its growth since 2.51, mod 256.
	.section .text.pad_ovl,"ax"
	.globl	pad_ovl
pad_ovl:
	.skip	214
