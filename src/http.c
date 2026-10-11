/*
 * http.c - an HTTP/1.1 client on conn.c, with one kept-open connection
 * (TLS handshakes cost a Falcon a second or two; the Pi is happy either
 * way).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "http.h"
#include "inflate.h"
#include "conn.h"
#include "charset.h"
#include "plat.h"
#include "bearssl.h"
#include "version.h"

HTTPCFG http_cfg = { .gateport = 8180 };

static CONN *kept;
static char kept_key[140];

/* ---------------- URLs ---------------- */

int url_parse(const char *s, URL *u)
{
	const char *h, *e, *colon;
	int n;
	memset(u, 0, sizeof(*u));
	while (*s == ' ')
		s++;
	if (!strncasecmp(s, "https://", 8)) {
		u->tls = 1;
		h = s + 8;
	} else if (!strncasecmp(s, "http://", 7)) {
		h = s + 7;
	} else if (!strncasecmp(s, "webcals://", 10)) {
		u->tls = 1;
		h = s + 10;
	} else if (!strncasecmp(s, "webcal://", 9)) {
		u->tls = 1;		/* webcal means "subscribe": today's servers want HTTPS */
		h = s + 9;
	} else {
		return 0;
	}
	/* user:pass@ in the URL is ignored: logins are asked for separately */
	e = h + strcspn(h, "/?#");
	{
		const char *at = memchr(h, '@', e - h);
		if (at)
			h = at + 1;
	}
	colon = memchr(h, ':', e - h);
	n = (int)((colon ? colon : e) - h);
	if (n <= 0 || n >= (int)sizeof(u->host))
		return 0;
	memcpy(u->host, h, n);
	u->host[n] = 0;
	u->port = colon ? (unsigned short)atoi(colon + 1) : (u->tls ? 443 : 80);
	if (!u->port)
		return 0;
	if (*e == '/' || *e == '?')
		str_copy(u->path, *e == '?' ? "/" : e, sizeof(u->path));
	else
		str_copy(u->path, "/", sizeof(u->path));
	if (*e == '?') {
		str_copy(u->path + 1, e, sizeof(u->path) - 1);
	}
	{
		char *hash = strchr(u->path, '#');
		if (hash)
			*hash = 0;
	}
	return 1;
}

void url_format(const URL *u, char *out, int size)
{
	if ((u->tls && u->port == 443) || (!u->tls && u->port == 80))
		snprintf(out, size, "%s://%s%s", u->tls ? "https" : "http", u->host, u->path);
	else
		snprintf(out, size, "%s://%s:%u%s", u->tls ? "https" : "http", u->host, u->port, u->path);
}

void url_resolve(const char *base, const char *ref, char *out, int size)
{
	URL b;
	if (!strncasecmp(ref, "http://", 7) || !strncasecmp(ref, "https://", 8)) {
		str_copy(out, ref, size);
		return;
	}
	if (!url_parse(base, &b)) {
		str_copy(out, ref, size);
		return;
	}
	if (ref[0] == '/') {
		str_copy(b.path, ref, sizeof(b.path));
	} else {
		char *slash = strrchr(b.path, '/');
		if (slash)
			slash[1] = 0;
		if ((long)strlen(b.path) + (long)strlen(ref) < (long)sizeof(b.path))
			strcat(b.path, ref);
	}
	url_format(&b, out, size);
}

/* ---------------- logins ---------------- */

void http_basic(const char *user, const char *pass, char *out, int size)
{
	char up[200];
	long n;
	char *b;
	snprintf(up, sizeof(up), "%s:%s", user, pass);
	b = base64_encode(up, (long)strlen(up), 0, &n);
	str_copy(out, b ? b : "", size);
	free(b);
	memset(up, 0, sizeof(up));
}

static void md5_hex(const char *s, char out[33])
{
	br_md5_context c;
	unsigned char d[16];
	int i;
	br_md5_init(&c);
	br_md5_update(&c, s, strlen(s));
	br_md5_out(&c, d);
	for (i = 0; i < 16; i++)
		sprintf(out + i * 2, "%02x", d[i]);
}

/* a value from a WWW-Authenticate challenge: realm="x" */
static void chal(const char *h, const char *key, char *out, int size)
{
	const char *p = h;
	int kl = (int)strlen(key);
	out[0] = 0;
	while ((p = str_istr(p, key))) {
		if ((p == h || p[-1] == ' ' || p[-1] == ',') && p[kl] == '=') {
			const char *v = p + kl + 1;
			int o = 0;
			if (*v == '"') {
				v++;
				while (*v && *v != '"' && o < size - 1)
					out[o++] = *v++;
			} else {
				while (*v && *v != ',' && *v != ' ' && o < size - 1)
					out[o++] = *v++;
			}
			out[o] = 0;
			return;
		}
		p += kl;
	}
}

static void digest_header(const char *challenge, const char *method, const char *uri,
			  const char *user, const char *pass, char *out, int size)
{
	char realm[100], nonce[160], qop[40], opaque[100], ha1[33], ha2[33], resp[33], tmp[600], cnonce[17];
	unsigned char r[8];
	int i;
	chal(challenge, "realm", realm, sizeof(realm));
	chal(challenge, "nonce", nonce, sizeof(nonce));
	chal(challenge, "qop", qop, sizeof(qop));
	chal(challenge, "opaque", opaque, sizeof(opaque));
	pf_entropy(r, sizeof(r));
	for (i = 0; i < 8; i++)
		sprintf(cnonce + i * 2, "%02x", r[i]);
	snprintf(tmp, sizeof(tmp), "%s:%s:%s", user, realm, pass);
	md5_hex(tmp, ha1);
	snprintf(tmp, sizeof(tmp), "%s:%s", method, uri);
	md5_hex(tmp, ha2);
	if (str_istr(qop, "auth")) {
		snprintf(tmp, sizeof(tmp), "%s:%s:00000001:%s:auth:%s", ha1, nonce, cnonce, ha2);
		md5_hex(tmp, resp);
		snprintf(out, size, "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
			 "qop=auth, nc=00000001, cnonce=\"%s\", response=\"%s\"%s%s%s",
			 user, realm, nonce, uri, cnonce, resp,
			 opaque[0] ? ", opaque=\"" : "", opaque, opaque[0] ? "\"" : "");
	} else {
		snprintf(tmp, sizeof(tmp), "%s:%s:%s", ha1, nonce, ha2);
		md5_hex(tmp, resp);
		snprintf(out, size, "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
			 "response=\"%s\"%s%s%s", user, realm, nonce, uri, resp,
			 opaque[0] ? ", opaque=\"" : "", opaque, opaque[0] ? "\"" : "");
	}
	memset(tmp, 0, sizeof(tmp));
	memset(ha1, 0, sizeof(ha1));
}

/* ---------------- connections ---------------- */

void http_close_all(void)
{
	if (kept)
		conn_close(kept);
	kept = 0;
	kept_key[0] = 0;
}

/* where a request for u goes: the Pi (proxy form) or the server itself */
static int via_gateway(const URL *u)
{
	if (http_cfg.falcon)
		return 0;
	if (!http_cfg.gate[0])
		return 0;
	return 1;
}

static CONN *get_conn(const URL *u, int gw, int *reused, char *err, int errlen)
{
	char key[140];
	const char *host = gw ? http_cfg.gate : u->host;
	unsigned short port = gw ? http_cfg.gateport : u->port;
	int sec = (!gw && u->tls) ? SEC_TLS : SEC_PLAIN;
	*reused = 0;
	if (http_cfg.offline) {
		str_copy(err, "ECal is working offline: switch it off in the menu to connect", errlen);
		return 0;
	}
	if (!gw && u->tls && !http_cfg.falcon) {
		snprintf(err, errlen, "%s needs a secure connection: set up the Raspberry Pi "
			 "gateway in Settings > Connection, or turn on Falcon mode", u->host);
		return 0;
	}
	snprintf(key, sizeof(key), "%s:%u:%d", host, port, sec);
	if (kept && !strcmp(key, kept_key) && !kept->dead) {
		*reused = 1;
		return kept;
	}
	http_close_all();
	kept = conn_open("HTTP", host, port, sec, err, errlen);
	if (!kept) {
		if (gw) {
			char e2[200];
			str_copy(e2, err, sizeof(e2));
			snprintf(err, errlen, "Can't reach the gateway at %s:%u (%s). Is ecalgate running on the Pi?",
				 host, port, e2);
		}
		return 0;
	}
	kept->timeout_ms = 45000;
	str_copy(kept_key, key, sizeof(kept_key));
	return kept;
}

/* ---------------- one exchange ---------------- */

typedef struct {
	int chunked, close_after, keep_alive;
	int packed;			/* Content-Encoding: 1 gzip, 2 deflate */
	long clen;			/* -1 unknown */
	char auth[300];			/* WWW-Authenticate */
} RHDR;

static void header_line(const char *l, HTTPRESP *rs, RHDR *h)
{
	const char *v = strchr(l, ':');
	int n;
	if (!v)
		return;
	n = (int)(v - l);
	v++;
	while (*v == ' ' || *v == '\t')
		v++;
#define IS(name) (n == (int)sizeof(name) - 1 && !strncasecmp(l, name, n))
	if (IS("Content-Length"))
		h->clen = atol(v);
	else if (IS("Transfer-Encoding") && str_istr(v, "chunked"))
		h->chunked = 1;
	else if (IS("Content-Encoding"))
		h->packed = str_istr(v, "gzip") ? 1 : str_istr(v, "deflate") ? 2 : 0;
	else if (IS("Connection") && str_istr(v, "close"))
		h->close_after = 1;
	else if (IS("Connection") && str_istr(v, "keep-alive"))
		h->keep_alive = 1;
	else if (IS("Location"))
		str_copy(rs->location, v, sizeof(rs->location));
	else if (IS("ETag"))
		str_copy(rs->etag, v, sizeof(rs->etag));
	else if (IS("Content-Type"))
		str_copy(rs->ctype, v, sizeof(rs->ctype));
	else if (IS("Last-Modified"))
		str_copy(rs->lastmod, v, sizeof(rs->lastmod));
	else if (IS("WWW-Authenticate")) {
		/* prefer Digest when offered next to Basic */
		if (!h->auth[0] || !strncasecmp(v, "Digest", 6))
			str_copy(h->auth, v, sizeof(h->auth));
	}
#undef IS
}

typedef struct {
	const HTTPREQ *rq;
	SBUF *body;
	long max;
	int to_sink;
	int failed;
} BODY;

static int body_add(BODY *b, const char *d, long n)
{
	if (b->to_sink) {
		if (!b->rq->sink(b->rq->ud, d, n)) {
			b->failed = 1;
			return 0;
		}
		return 1;
	}
	if (b->body->len + n > b->max) {
		b->failed = 2;
		return 0;
	}
	return sb_add(b->body, d, n);
}

static int read_n(CONN *c, BODY *b, long n, HTTPRESP *rs)
{
	char tmp[1024];
	while (n > 0) {
		long k = conn_read(c, tmp, n > (long)sizeof(tmp) ? (long)sizeof(tmp) : n);
		if (k <= 0)
			return 0;
		rs->length += k;
		if (!body_add(b, tmp, k))
			return 0;
		n -= k;
	}
	return 1;
}

/* send the request and read the answer: 1 ok, 0 failed, -1 failed on a
   reused connection before anything came back (worth one more try) */
static int exchange(const HTTPREQ *rq, const URL *u, const char *auth, HTTPRESP *rs, RHDR *h,
		    SBUF *body, char *err, int errlen)
{
	SBUF req, line;
	CONN *c;
	int gw = via_gateway(u), reused, ok = 0, got_status = 0, http10 = 0;
	BODY b;
	char hostport[120];
	unsigned long t0 = pf_ms();
	long body_start = 0, unpacked = -1;
	c = get_conn(u, gw, &reused, err, errlen);
	if (!c)
		return 0;
	if ((u->tls && u->port == 443) || (!u->tls && u->port == 80))
		str_copy(hostport, u->host, sizeof(hostport));
	else
		snprintf(hostport, sizeof(hostport), "%s:%u", u->host, u->port);
	sb_init(&req);
	sb_init(&line);
	if (gw)
		sb_printf(&req, "%s %s://%s%s HTTP/1.1\r\n", rq->method, u->tls ? "https" : "http", hostport, u->path);
	else
		sb_printf(&req, "%s %s HTTP/1.1\r\n", rq->method, u->path);
	/* straight to the server, the answer comes compressed: unpacking costs
	   the 68030 a fraction of decrypting the bytes it saves. Through the
	   gateway the Pi unpacks it, and a download to a file stays as it is */
	sb_printf(&req, "Host: %s\r\nUser-Agent: ECal/%s (Atari ST/TT/Falcon)\r\nAccept-Encoding: %s\r\n", hostport,
		  cal_version, !gw && !rq->sink ? "gzip, deflate" : "identity");
	conn_log("HTTP", " >> ", req.s, req.len - 2);
	if (auth && *auth)
		sb_printf(&req, "Authorization: %s\r\n", auth);
	if (rq->headers)
		sb_adds(&req, rq->headers);
	if (rq->body) {
		sb_printf(&req, "Content-Type: %s\r\nContent-Length: %ld\r\n",
			  rq->ctype ? rq->ctype : "application/octet-stream", rq->bodylen);
	} else if (!strcmp(rq->method, "PUT") || !strcmp(rq->method, "POST")) {
		sb_adds(&req, "Content-Length: 0\r\n");
	}
	sb_adds(&req, "\r\n");
	if (!conn_write(c, req.s, req.len) || (rq->body && !conn_write(c, rq->body, rq->bodylen))) {
		str_copy(err, c->err[0] ? c->err : "connection lost while sending", errlen);
		c->dead = 1;
		sb_free(&req);
		http_close_all();
		return reused ? -1 : 0;
	}
	sb_free(&req);
	/* status line, skipping "100 Continue" */
	for (;;) {
		if (conn_getline(c, &line) < 0)
			goto lost;
		if (!line.len)
			continue;
		if (strncmp(line.s, "HTTP/", 5))
			goto lost;
		got_status = 1;
		http10 = !strncmp(line.s, "HTTP/1.0", 8);
		rs->status = atoi(line.s + 9);
		str_copy(rs->reason, line.len > 13 ? line.s + 13 : "", sizeof(rs->reason));
		/* headers */
		memset(h, 0, sizeof(*h));
		h->clen = -1;
		for (;;) {
			if (conn_getline(c, &line) < 0)
				goto lost;
			if (!line.len)
				break;
			header_line(line.s, rs, h);
		}
		if (rs->status >= 200 || rs->status < 100)
			break;
	}
	/* an HTTP/1.0 server closes after the answer unless it says otherwise */
	if (http10 && !h->keep_alive)
		h->close_after = 1;
	memset(&b, 0, sizeof(b));
	b.rq = rq;
	b.body = body;
	body_start = body ? body->len : 0;
	b.max = rq->maxbody ? rq->maxbody : 1048576L;
	b.to_sink = rq->sink && rs->status >= 200 && rs->status < 300;
	if (!b.to_sink && rq->sink)
		b.max = 8192;		/* an error page: just enough to read */
	if (!strcmp(rq->method, "HEAD") || rs->status == 204 || rs->status == 304) {
		ok = 1;
	} else if (h->chunked) {
		for (;;) {
			long n;
			if (conn_getline(c, &line) < 0)
				goto lost;
			n = strtol(line.s, 0, 16);
			if (n <= 0) {
				/* trailer lines up to the blank one */
				do {
					if (conn_getline(c, &line) < 0)
						goto lost;
				} while (line.len);
				break;
			}
			if (!read_n(c, &b, n, rs))
				goto body_err;
			if (conn_getline(c, &line) < 0)
				goto lost;
		}
		ok = 1;
	} else if (h->clen >= 0) {
		if (!read_n(c, &b, h->clen, rs))
			goto body_err;
		ok = 1;
	} else {
		/* until the server closes */
		char tmp[1024];
		long k;
		while ((k = conn_read(c, tmp, sizeof(tmp))) > 0) {
			rs->length += k;
			if (!body_add(&b, tmp, k))
				goto body_err;
		}
		h->close_after = 1;
		ok = 1;
	}
	if (ok && h->packed && !b.to_sink && body && body->len > body_start) {
		SBUF plain;
		int good;
		sb_init(&plain);
		good = h->packed == 1 ? inflate_gzip((const unsigned char *)body->s + body_start, body->len - body_start, &plain)
				      : inflate_zlib((const unsigned char *)body->s + body_start, body->len - body_start, &plain);
		if (!good) {
			sb_free(&plain);
			str_copy(err, "the server's compressed answer is damaged", errlen);
			ok = 0;
		} else {
			body->len = body_start;
			ok = sb_add(body, plain.s ? plain.s : "", plain.len);
			unpacked = plain.len;
			sb_free(&plain);
		}
	}
	{
		char msg[120];
		char how[40] = "";
		if (unpacked >= 0)
			snprintf(how, sizeof(how), ", %ld unpacked", unpacked);
		snprintf(msg, sizeof(msg), "%d, %ld bytes%s in %lu ms%s", rs->status, rs->length, how, pf_ms() - t0,
			 reused ? "" : " (new connection)");
		conn_log("HTTP", " -- ", msg, strlen(msg));
	}
	if (h->close_after)
		http_close_all();
	sb_free(&line);
	return ok;

body_err:
	if (b.failed == 2)
		snprintf(err, errlen, "the answer from %s is too big for this Atari's memory", u->host);
	else if (b.failed == 1)
		snprintf(err, errlen, "couldn't save what %s sent (disk full?)", u->host);
	else
		snprintf(err, errlen, "%s: %s", u->host, c->err[0] ? c->err : "connection lost");
	sb_free(&line);
	http_close_all();
	return 0;
lost:
	snprintf(err, errlen, "%s: %s", gw ? "the gateway" : u->host,
		 c->err[0] ? c->err : "connection lost");
	sb_free(&line);
	http_close_all();
	return (reused && !got_status) ? -1 : 0;
}

int http_request(const HTTPREQ *rq0, HTTPRESP *rs, SBUF *body, char *err, int errlen)
{
	HTTPREQ rq = *rq0;
	char url[512], auth[600], basic[300];
	int hops, digest_tried = 0;
	RHDR h;
	str_copy(url, rq.url, sizeof(url));
	auth[0] = 0;
	if (rq.user && rq.user[0]) {
		http_basic(rq.user, rq.pass ? rq.pass : "", basic, sizeof(basic));
		snprintf(auth, sizeof(auth), "Basic %s", basic);
	}
	for (hops = 0; hops < 8; hops++) {
		URL u;
		int r;
		memset(rs, 0, sizeof(*rs));
		if (body)
			sb_reset(body);
		if (!url_parse(url, &u)) {
			snprintf(err, errlen, "not a web address: %.60s", url);
			return 0;
		}
		url_format(&u, rs->final_url, sizeof(rs->final_url));
		rq.url = url;
		r = exchange(&rq, &u, auth, rs, &h, body, err, errlen);
		if (r < 0) {		/* the kept connection had gone stale */
			memset(rs, 0, sizeof(*rs));
			if (body)
				sb_reset(body);
			url_format(&u, rs->final_url, sizeof(rs->final_url));
			r = exchange(&rq, &u, auth, rs, &h, body, err, errlen);
		}
		if (r <= 0)
			return 0;
		if ((rs->status == 301 || rs->status == 302 || rs->status == 303 ||
		     rs->status == 307 || rs->status == 308) && rs->location[0]) {
			char next[512];
			url_resolve(url, rs->location, next, sizeof(next));
			conn_log("HTTP", " -- ", "redirected", 10);
			str_copy(url, next, sizeof(url));
			if (rs->status == 303)
				rq.method = "GET", rq.body = 0;
			continue;
		}
		if (rs->status == 401 && h.auth[0] && !strncasecmp(h.auth, "Digest", 6) &&
		    rq.user && !digest_tried) {
			digest_tried = 1;
			digest_header(h.auth, rq.method, u.path, rq.user, rq.pass ? rq.pass : "", auth, sizeof(auth));
			continue;
		}
		return 1;
	}
	snprintf(err, errlen, "too many redirects");
	return 0;
}
