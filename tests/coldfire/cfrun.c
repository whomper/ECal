/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cfrun.c - what a core test needs to run as ColdFire code under
 * qemu-m68k -cpu cfv4e (make core-cf): GEMDOS's memory calls stood in for
 * (common's atari/libc.c, ECALFB.PRG's own, takes its heap from them),
 * printf through libc.c's vsnprintf to Linux's stdout, and the few
 * platform calls the core makes. common/tests/coldfire's rt.S starts it
 * and gives the Linux calls; its gemdos.c the signal's way in.
 */
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include "rt.h"
#include "gemdos.h"
#include "plat.h"

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int cf_main(void);

short _is_acc;
void *_BasePage;

/* the heap: one arena, given out from the bottom; Mfree gives nothing back */
static unsigned char arena[24L * 1024 * 1024] __attribute__((aligned(16)));
static long arena_used;

static long gd_alloc(long n)
{
	if (n < 0)
		return (long)sizeof(arena) - arena_used;	/* the largest block */
	n = (n + 15) & ~15L;
	if (arena_used + n > (long)sizeof(arena))
		return 0;
	arena_used += n;
	return (long)(arena + arena_used - n);
}

void out(const char *s) { sys_write(1, s, (long)strlen(s)); }

void out_hex(unsigned long v)
{
	char b[11];
	int i;
	b[0] = '0';
	b[1] = 'x';
	for (i = 0; i < 8; i++)
		b[9 - i] = "0123456789abcdef"[(v >> (4 * i)) & 15];
	b[10] = 0;
	out(b);
}

static void handler(int sig, void *info, void *ucv)
{
	UCTX *uc = ucv;
	unsigned long sp = uc->gregs[15], pc = uc->gregs[16];
	unsigned short fn = *(unsigned short *)sp;
	(void)sig;
	(void)info;
	if (*(unsigned short *)pc == 0x4e41)	/* (at the trap: past it) */
		pc += 2;
	else if (*(unsigned short *)(pc - 2) != 0x4e41) {
		out("cfrun: SIGILL at ");
		out_hex(pc);
		out(", not a trap #1\n");
		sys_exit(3);
	}
	switch (fn) {
	case 0x48:			/* Malloc(n) */
		uc->gregs[0] = (unsigned long)gd_alloc(*(long *)(sp + 2));
		break;
	case 0x44:			/* Mxalloc(n, mode) */
		uc->gregs[0] = (unsigned long)gd_alloc(*(long *)(sp + 2));
		break;
	case 0x49:			/* Mfree */
		uc->gregs[0] = 0;
		break;
	case 0x4c:			/* Pterm(code) */
		sys_exit(*(short *)(sp + 2));
	case 0x00:
		sys_exit(0);
	default:
		out("cfrun: GEMDOS ");
		out_hex(fn);
		out(" not stood in for\n");
		sys_exit(3);
	}
	uc->gregs[16] = pc;
}

int cf_printf(const char *fmt, ...)
{
	static char b[1024];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	out(b);
	return n;
}

/* the platform calls the core links to; the tests read no files */
char *pf_load(const char *path, long *len) { (void)path; (void)len; return 0; }
int pf_exists(const char *path) { (void)path; return 0; }
int pf_open(const char *path, int mode) { (void)path; (void)mode; return -1; }
long pf_read(int h, void *buf, long n) { (void)h; (void)buf; (void)n; return -1; }
long pf_seek(int h, long off) { (void)h; (void)off; return -1; }
void pf_close(int h) { (void)h; }
void pf_entropy(unsigned char *buf, int n) { memset(buf, 0x5a, (size_t)n); }
void (*pf_idle)(void);
void pf_debug(const char *s) { (void)s; }
unsigned long pf_ms(void) { return 0; }
/* libc.c's acc_core (an accessory's own memory, common 9fd1335): never
   an accessory here, but linked */
long get_cookie(long id) { (void)id; return 0; }
long tos_supexec(void *f) { return ((long (*)(void))f)(); }
const char cal_version[] = "core-cf";

void heap_add(void *p, size_t bytes);

int main(void)
{
	sig_install(4, handler);	/* SIGILL: GEMDOS's trap #1 */
	heap_add(arena, sizeof(arena));	/* (libc.c's heap: the arena, fixed) */
	arena_used = sizeof(arena);
	sys_exit(cf_main());
}
