| SPDX-License-Identifier: GPL-2.0-or-later
| Copyright (C) 2026 AERA | Erez Yaary
|
| Linked right after tls_ovl.o: TLSFB.OVL's change (AERA-Atari a99ea65)
| made tls_ovl.c's code 134 bytes longer and its strings 92; the code and
| strings after it moved. The pads put them back at 2.51's addresses mod
| 256 (134 + 122, 92 + 164). Kept by -Wl,-u. See pad_ovl.S.
	.section .text.pad_tlsovl,"ax"
	.globl	pad_tlsovl
pad_tlsovl:
	.skip	122
	.section .rodata.pad_tlsovl,"a"
	.globl	pad_tlsovl_ro
pad_tlsovl_ro:
	.skip	164
