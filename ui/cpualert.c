/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cpualert.c - which machine this ECal is for (the guidelines, 7.4: every
 * app has a native FireBee edition; Erez, 22 Nov 2026). common's
 * atari/cpucheck.c (ECAL.PRG, ECAL.ACC) and cpucheck_cf.S (ECALFB.PRG,
 * ECALFB.ACC) come first and show the alert where the other edition
 * belongs; one string for the program and the accessory, which are one
 * program (the accessory goes quiet after it, the program quits).
 */
#ifdef __mcoldfire__
/* ECALFB on an ST, a TT or a Falcon (no _CF_ cookie) */
const char cpu_alert[] = "[3][This ECal is for the FireBee.|On this machine use ECAL.PRG|or ECAL.ACC.][ OK ]";
#else
/* every 68000 runs ECAL: cpu_alert never shows (cpu_need 0) */
const long cpu_need = 0;
const char cpu_alert[] = "";
/* ECAL on a FireBee (FireTOS's _CF_ cookie) */
const char cpu_cf_alert[] = "[3][This ECal is for the ST, TT|and Falcon. On the FireBee use|ECALFB.PRG or ECALFB.ACC.][ OK ]";
#endif
