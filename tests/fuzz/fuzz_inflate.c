/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * Compressed answers (the AERA guidelines, 7.1): gzip, zlib and raw
 * deflate, as servers send them.
 */
#include <stdint.h>
#include <stdlib.h>
#include "util.h"
#include "inflate.h"

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	SBUF out;
	sb_init(&out);
	inflate_gzip(d, (long)n, &out);
	sb_free(&out);
	sb_init(&out);
	inflate_zlib(d, (long)n, &out);
	sb_free(&out);
	sb_init(&out);
	inflate_raw(d, (long)n, &out);
	sb_free(&out);
	return 0;
}
