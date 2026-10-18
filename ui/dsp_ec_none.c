/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * dsp_ec_none.c - the ST edition has no TLS of its own (tls_none.c), so
 * no elliptic curves on a DSP either: common/ui/dsp_ec.c's calls, empty.
 */
#include "ui.h"
#include "tls.h"

void falcon_dsp_ec_init(void)
{
}

const char *falcon_dsp_ec_state(void)
{
	return "68030";
}

/* tls.c's hook, which common's tls_none.c doesn't have yet: main.c sets
   it in both editions (remove this when tls_none.c gets it) */
long (*tls_read_at)(const char *path, long off, void *buf, long n);
