/*
 * tos.h - GEMDOS / BIOS / XBIOS / MiNTnet calls as inline traps.
 *
 * Adapted from Claude ST (whomper/atari_claude). The program is built
 * freestanding (no MiNTLib), so it runs on TOS 1.0 - 4.x, EmuTOS,
 * MagiC and MiNT. TOS may trash d0-d2/a0-a2 across a trap; all inputs
 * are forced into registers so an sp-relative operand can't move under
 * our pushes.
 */
#ifndef TOS_H
#define TOS_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define TRAP_CLOBBER "d1", "d2", "a0", "a1", "a2", "memory", "cc"

static inline long trap1_w(short fn)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #2,%%sp"
		: "=r"(r) : "r"(fn) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wl(short fn, long a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wlw(short fn, long a, short b)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\taddq.l #8,%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wwll(short fn, short a, long b, long c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 12(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wlww(short fn, long a, short b, short c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %4,-(%%sp)\n\tmove.w %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 10(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wlll(short fn, long a, long b, long c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 14(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wwlw(short fn, short a, long b, short c)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 10(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c) : TRAP_CLOBBER);
	return r;
}

static inline long trap1_wwlll(short fn, short a, long b, long c, long d)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %5,-(%%sp)\n\tmove.l %4,-(%%sp)\n\tmove.l %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #1\n\tlea 16(%%sp),%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b), "r"(c), "r"(d) : TRAP_CLOBBER);
	return r;
}

static inline long trap13_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #13\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap13_www(short fn, short a, short b)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %3,-(%%sp)\n\tmove.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #13\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a), "r"(b) : TRAP_CLOBBER);
	return r;
}

static inline long trap14_w(short fn)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #2,%%sp"
		: "=r"(r) : "r"(fn) : TRAP_CLOBBER);
	return r;
}

static inline long trap14_ww(short fn, short a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.w %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #4,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

static inline long trap14_wl(short fn, long a)
{
	register long r __asm__("d0");
	__asm__ volatile("move.l %2,-(%%sp)\n\tmove.w %1,-(%%sp)\n\ttrap #14\n\taddq.l #6,%%sp"
		: "=r"(r) : "r"(fn), "r"(a) : TRAP_CLOBBER);
	return r;
}

/* GEMDOS */
#define Cconws(s)          trap1_wl(0x09, (long)(s))
#define Dsetdrv(d)         trap1_ww(0x0e, (d))
#define Dgetdrv()          trap1_w(0x19)
#define Fsetdta(p)         trap1_wl(0x1a, (long)(p))
#define Tgetdate()         trap1_w(0x2a)
#define Tgettime()         trap1_w(0x2c)
#define Fgetdta()          ((void *)trap1_w(0x2f))
#define Dcreate(p)         trap1_wl(0x39, (long)(p))
#define Ddelete(p)         trap1_wl(0x3a, (long)(p))
#define Dsetpath(p)        trap1_wl(0x3b, (long)(p))
#define Fcreate(n, a)      trap1_wlw(0x3c, (long)(n), (a))
#define Fopen(n, m)        trap1_wlw(0x3d, (long)(n), (m))
#define Fclose(h)          trap1_ww(0x3e, (h))
#define Fread(h, n, b)     trap1_wwll(0x3f, (h), (long)(n), (long)(b))
#define Fwrite(h, n, b)    trap1_wwll(0x40, (h), (long)(n), (long)(b))
#define Fdelete(n)         trap1_wl(0x41, (long)(n))
#define Fseek(o, h, m)     trap1_wlww(0x42, (long)(o), (h), (m))
#define Mxalloc(n, m)      ((void *)trap1_wlw(0x44, (long)(n), (m)))
#define Dgetpath(b, d)     trap1_wlw(0x47, (long)(b), (d))
#define Malloc(n)          ((void *)trap1_wl(0x48, (long)(n)))
#define Mfree(p)           trap1_wl(0x49, (long)(p))
#define Fsfirst(p, a)      trap1_wlw(0x4e, (long)(p), (a))
#define Fsnext()           trap1_w(0x4f)
#define Frename(o, n)      trap1_wwll(0x56, 0, (long)(o), (long)(n))

/* MiNT and MiNTnet (GEMDOS extensions; -32 EINVFN when absent) */
#define Fcntl(h, a, c)     trap1_wwlw(0x104, (h), (long)(a), (c))
#define Fselect(t, r, w, x) trap1_wwlll(0x11d, (t), (long)(r), (long)(w), (long)(x))
#define Fsocket(d, t, p)   trap1_wlll(0x160, (long)(d), (long)(t), (long)(p))
#define Fconnect(h, a, l)  trap1_wwll(0x163, (h), (long)(a), (long)(l))
#define FIONREAD  0x4601
#define FIONWRITE 0x4602
#define F_SETFL   4
#define O_NDELAY  0x100

/* BIOS */
#define Bconstat(d)        trap13_ww(1, (d))
#define Bconin(d)          trap13_ww(2, (d))
#define Bconout(d, c)      trap13_www(3, (d), (c))

/* XBIOS */
#define Iorec(d)           ((void *)trap14_ww(14, (d)))
#define Supexec(f)         trap14_wl(38, (long)(f))

typedef struct {
	char *ibuf;
	short ibufsiz;
	volatile short ibufhd;
	volatile short ibuftl;
	short ibuflow;
	short ibufhi;
} IOREC;

typedef struct {
	char  d_reserved[21];
	u8    d_attrib;
	u16   d_time;
	u16   d_date;
	u32   d_length;
	char  d_fname[14];
} DTA;

extern void *_BasePage;

/* tos.c */
long get_cookie(long id);	/* 0 when missing */
u32 tos_hz200(void);		/* 200 Hz system timer */
void nf_debug(const char *s);	/* emulator console (NatFeats); no-op on hardware */

#endif
