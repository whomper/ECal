/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * XML from a WebDAV server (the AERA guidelines, 7.1): every token read,
 * and entities decoded.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xml.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	XMLP x;
	char *copy = malloc(n + 1);
	if (!copy)
		return 0;
	memcpy(copy, d, n);
	copy[n] = 0;
	xml_init(&x, copy, (long)n);
	while (xml_next(&x) > 0)
		;
	xml_done(&x);
	memcpy(copy, d, n);
	xml_decode(copy, (long)n);
	free(copy);
	return 0;
}
