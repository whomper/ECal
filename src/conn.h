/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * conn.h - a buffered, line-oriented TCP connection with timeouts and
 * an optional protocol log (ECAL.LOG). From EMail, where IMAP, POP3 and SMTP share it.
 */
#ifndef CONN_H
#define CONN_H

#include "util.h"

/* how a connection is protected */
#define SEC_PLAIN    0	/* none (through the Pi gateway, or a local server) */
#define SEC_TLS      1	/* TLS from the first byte (993, 995, 465) */
#define SEC_STARTTLS 2	/* plain first, then STARTTLS / STLS (143, 110, 587) */

typedef struct {
	int h;
	void *tls;			/* TLS *, when encrypted */
	char buf[16384];	/* a TLS record or a good STinG gulp at a time */
	long pos, len;
	long timeout_ms;
	int dead;		/* known to be gone: close without a goodbye */
	char err[160];
	const char *name;		/* "IMAP", "POP3", "SMTP" for the log */
	unsigned long tls_ms;		/* how long the TLS handshake took */
} CONN;

/* sec: SEC_TLS starts TLS at once; otherwise plain (the protocol calls
   conn_starttls when it asks for SEC_STARTTLS) */
CONN *conn_open(const char *name, const char *host, unsigned short port, int sec,
		char *err, int errlen);
/* switch to TLS after the protocol's STARTTLS; 0 with err on failure.
   conn_tls_retry is then set if a new connection should try again (the
   server wanted an ECDSA certificate; the next try accepts it) */
int   conn_starttls(CONN *c, const char *host, char *err, int errlen);
extern int conn_tls_retry;
/* the root certificates (CACERT.PEM), loaded at the first TLS connection */
extern char conn_cacert[200];
/* "TLS 1.2, ECDHE-RSA, ChaCha20-Poly1305", or "" when not encrypted */
const char *conn_security(CONN *c);
void  conn_close(CONN *c);
int   conn_write(CONN *c, const char *data, long n);
/* send one command line (CRLF added); secret: hide arguments in the log */
int   conn_cmd(CONN *c, int secret, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* read one line without CRLF into b (b is reset first); -1 on error */
long  conn_getline(CONN *c, SBUF *b);
/* read exactly n bytes and append them to b */
int   conn_getbytes(CONN *c, SBUF *b, long n);

/* up to max bytes, whatever has come (waits for some); -1 when the
   connection is closed or timed out */
long  conn_read(CONN *c, char *buf, long max);

/* logging: set conn_logfile to a path to append the dialogue to it */
extern char conn_logfile[200];
void conn_log(const char *who, const char *dir, const char *text, long n);
void conn_log_flush(void);	/* write out what is still buffered */
/* the shape of a login (lengths, '@', '-') without the login itself, so
   the log shows a cut-off or mistyped password without giving it away */
void conn_log_login(const char *who, const char *user, const char *pass);

/* progress for the UI: bytes received so far in the current operation */
extern long conn_bytes, conn_bytes_out;	/* received and sent, ever */
/* called whenever data comes in or goes out (progress display) */
extern void (*conn_tick)(void);

#endif
