/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* for common/tests/heap/test_heap.c: needs heap_largest, set by heap_check (see README) */
/* a trace from the Atari (conv.py), replayed: where the replay fails */
static void test_replay(const char *path)
{
	FILE *f = fopen(path, "r");
	static unsigned char **ptr;
	char line[100];
	long nops = 0, nfree;
	ptr = calloc(400000, sizeof(*ptr));
	pool_new(64L << 20);
	while (fgets(line, sizeof(line), f)) {
		long a, b, n;
		nops++;
		if (line[0] == 'A' && sscanf(line + 2, "%ld", &n) == 1)
			heap_add(pool, (size_t)n);
		else if (line[0] == 'M' && sscanf(line + 2, "%ld %ld", &a, &n) == 2) {
			unsigned char *p = heap_malloc((size_t)n);
			if (!p || !a) {
				long fr = heap_check(&nfree);
				printf("op %ld: malloc %ld %s on the Atari, %s here: free %ld in %ld, largest %ld, use %ld\n", nops, n,
				       a ? "worked" : "FAILED", p ? "works" : "FAILS", fr, nfree, heap_largest, heap_use);
			}
			if (a) ptr[a] = p;
			else if (p) heap_free(p);
		} else if (line[0] == 'F' && sscanf(line + 2, "%ld", &a) == 1) {
			heap_free(ptr[a]);
			ptr[a] = 0;
		} else if (line[0] == 'R' && sscanf(line + 2, "%ld %ld %ld", &a, &b, &n) == 3) {
			unsigned char *p = heap_realloc(a ? ptr[a] : 0, (size_t)n);
			if (!p || !b) {
				long fr = heap_check(&nfree);
				printf("op %ld: realloc %ld %s on the Atari, %s here: free %ld in %ld, largest %ld, use %ld\n", nops, n,
				       b ? "worked" : "FAILED", p ? "works" : "FAILS", fr, nfree, heap_largest, heap_use);
			}
			if (p) {
				if (a) ptr[a] = 0;
				if (b) ptr[b] = p;
				else heap_free(p);	/* (kept as the Atari had it: the old one stays) */
			}
		} else if (line[0] == 'X') {
			long fr = heap_check(&nfree);
			printf("op %ld: the Atari's failure point: free %ld in %ld, largest %ld, use %ld\n", nops, fr, nfree, heap_largest, heap_use);
		}
	}
	printf("replay: %ld ops, use %ld, peak %ld\n", nops, heap_use, heap_peak);
}

