/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * tls.h - TLS 1.2 on the Atari itself (Falcon mode), on BearSSL.
 *
 * The mail protocols talk to a TLS connection exactly as to a plain one:
 * conn.c sends its bytes through tls_write/tls_read when the connection
 * is encrypted. Certificates are checked against the root certificates
 * in CACERT.PEM (the Mozilla list, as curl publishes it).
 */
#ifndef TLS_H
#define TLS_H

#include <stddef.h>

typedef struct TLS TLS;
void tls_forget_sessions(void);	/* settings changed: start afresh */

/* 0 in the ST edition, which has no TLS of its own (tls_none.c) */
extern const int tls_available;

/* the local time zone, minutes east of UTC, for certificate dates */
extern int tls_tz_minutes;
/* where the random seed is kept between sessions (EMAIL\\SEED.DAT) */
extern char tls_seed_path[200];
/* the decoded root certificates are kept here (ROOTS.DAT, next to CACERT.PEM) */
extern char tls_roots_cache[200];
/* a word for the status line while something slow happens, or NULL */
extern void (*tls_note)(const char *msg);

/* load the root certificates from a PEM file (once; later calls are
   cheap). Returns how many, or 0 with err filled in */
int  tls_load_anchors(const char *path, char *err, int errlen);
int  tls_anchor_count(void);
void tls_free_anchors(void);	/* forget them (a new CACERT.PEM) */

/* handshake on a connected socket. rsa_only: ask the server for an RSA
   certificate (fast to check, on the DSP); 0 also accepts ECDSA.
   NULL with err filled in on failure; *retry_ecdsa set when the server
   refused an RSA-only handshake and a new connection should try again */
TLS *tls_start(int sock, const char *host, int rsa_only, int *retry_ecdsa, char *err, int errlen);

/* like net_read/net_write: > 0 bytes, 0 nothing yet, < 0 closed */
long tls_read(TLS *t, void *buf, long max);
long tls_write(TLS *t, const void *buf, long n);
void tls_close(TLS *t);		/* sends close_notify, frees; not the socket */

/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305" for the log and the About box */
const char *tls_describe(TLS *t);

/* an RSA public-key operation x = x^e mod n done elsewhere (the Falcon's
   DSP): returns 1 if it did it, 0 to let BearSSL do it. NULL: none */
extern int (*tls_rsa_accel)(unsigned char *x, size_t xlen,
			    const unsigned char *n, size_t nlen,
			    const unsigned char *e, size_t elen);

#endif
