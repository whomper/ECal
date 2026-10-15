/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * iCalendar from a server (the AERA guidelines, 7.1): the events parsed
 * (folded lines, escapes, UTF-8, time zones, repeat rules), each one's
 * occurrences in a month worked out, and written back as ECal does when
 * it changes one.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ics.h"
#include "tz.h"

static const char *text;
static long len;

static void occ(EVENT *e, CTIME start, CTIME end, void *ud)
{
	(void)e;
	(void)start;
	(void)end;
	(*(long *)ud)++;
}

static int got(EVENT *e, void *ud)
{
	long count = 0, outlen;
	CTIME from = ct_make(2026, 10, 1, 0, 0);
	char *out;
	(void)ud;
	ics_expand(e, from, from + 31L * 24 * 60, tz_utc(), occ, 0, &count);
	out = ics_build(e, text, len, &outlen);
	free(out);
	ev_free(e);
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	/* NUL-ended, as ECal keeps an event's text (ev_raw) */
	char *copy = malloc(n + 1);
	if (!copy)
		return 0;
	memcpy(copy, d, n);
	copy[n] = 0;
	text = copy;
	len = (long)n;
	ics_parse(text, len, got, 0, ct_make(2025, 1, 1, 0, 0), ct_make(2028, 1, 1, 0, 0));
	free(copy);
	return 0;
}
