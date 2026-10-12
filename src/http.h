/*
 * http.h - HTTP/1.1 for CalDAV and calendar subscriptions.
 *
 * Two ways out, like EMail's:
 *  - through the Raspberry Pi gateway (ecalgate.py): every request goes
 *    to the Pi in plain HTTP, in proxy form ("GET https://host/path"),
 *    and the Pi makes the HTTPS connection. Any Atari can do this.
 *  - Falcon mode: TLS 1.2 on the Atari itself (tls.c, RSA on the DSP).
 * Plain http:// addresses are reached directly in both modes when no
 * gateway is set.
 */
#ifndef HTTP_H
#define HTTP_H

#include "util.h"

typedef struct {
	int tls;			/* https */
	char host[100];
	unsigned short port;
	char path[512];			/* with the query, starting with '/' */
} URL;

/* http, https, webcal, webcals; 0 if it isn't a URL we can use */
int  url_parse(const char *s, URL *u);
void url_format(const URL *u, char *out, int size);
/* a reference (href) seen in an answer from base -> absolute URL */
void url_resolve(const char *base, const char *ref, char *out, int size);

typedef struct {
	int falcon;			/* 1: TLS here; 0: through the gateway */
	int offline;			/* Work offline: no connections at all */
	char gate[64];			/* the Pi: host name or address, "" = none */
	unsigned short gateport;	/* 8180 by default */
} HTTPCFG;
extern HTTPCFG http_cfg;

typedef struct {
	int status;			/* 200, 207, 404 ... */
	char reason[48];
	char location[512];		/* of a redirect */
	char etag[100];
	char ctype[64];
	char lastmod[40];
	char final_url[512];		/* after redirects */
	long length;			/* body bytes received */
} HTTPRESP;

typedef struct {
	const char *method, *url;
	const char *user, *pass;	/* NULL: no login */
	const char *ctype;		/* of the body */
	const char *body;
	long bodylen;
	const char *headers;		/* more header lines, each with CRLF */
	/* the body of a 2xx answer goes here instead of into memory */
	int (*sink)(void *ud, const char *data, long n);
	void *ud;
	long maxbody;			/* in memory; 0 = 1 MB */
} HTTPREQ;

/* 1: an answer came (look at rs->status), 0: no answer, err says why.
   Redirects are followed; a 401 to a Basic login is answered with a
   Digest one when the server asks for that. */
int  http_request(const HTTPREQ *rq, HTTPRESP *rs, SBUF *body, char *err, int errlen);
void http_close_all(void);	/* drop the kept-open connection */
void http_kept_host(char *out, int size);	/* its host, "" if none */

/* base64 of "user:pass" for Basic */
void http_basic(const char *user, const char *pass, char *out, int size);

#endif
