/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cpualert.c - which machine this ECal is for (the guidelines, 7.4: two
 * packages, the standard one for the ST, TT and Falcon and the FireBee's,
 * the same names inside; decided 22 Nov 2026). common's atari/cpucheck.c
 * (the standard ECAL.PRG and ECAL.ACC) and cpucheck_cf.S (the FireBee's)
 * come first and show the alert where the other package belongs: the
 * program at once, then it quits; the accessory nothing at boot, its Desk
 * entry (cpu_acc_name) showing it when chosen. The words decided 22 Nov 2026, a line
 * of 30 characters at most.
 */
const char cpu_acc_name[] = "  ECal";

#ifdef __mcoldfire__
/* the FireBee package on an ST, a TT or a Falcon (no _CF_ cookie) */
const char cpu_alert[] = "[3][ECAL.PRG is for the FireBee.|On an ST, a TT or a Falcon,|install ECal's standard|package.][ Quit ]";
const char cpu_acc_alert[] = "[3][ECAL.ACC is for the FireBee.|On an ST, a TT or a Falcon,|install ECal's standard|package.][ OK ]";
#else
/* every 68000 runs the standard package: cpu_alert never shows (cpu_need 0) */
const long cpu_need = 0;
const char cpu_alert[] = "";
/* the standard package on a FireBee (FireTOS's _CF_ cookie) */
const char cpu_cf_alert[] = "[3][ECAL.PRG is for the ST, TT|and Falcon. On the FireBee,|install ECal's FireBee|package.][ Quit ]";
const char cpu_cf_acc_alert[] = "[3][ECAL.ACC is for the ST, TT|and Falcon. On the FireBee,|install ECal's FireBee|package.][ OK ]";
#endif
