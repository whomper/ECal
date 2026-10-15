/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * An RRULE from a server (the AERA guidelines, 7.1): read, described in
 * words for the editor and written back.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ics.h"
#include "tz.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char s[400], out[300];
	RRULE r;
	if (n >= sizeof(s))
		n = sizeof(s) - 1;
	memcpy(s, d, n);
	s[n] = 0;
	if (rrule_parse(s, &r)) {
		rrule_describe(&r, out, sizeof(out));
		rrule_format(&r, tz_utc(), (int)(n & 1), out, sizeof(out));
	}
	return 0;
}
