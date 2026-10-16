/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * caldav.c - CalDAV requests and their multistatus answers.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "caldav.h"
#include "http.h"
#include "xml.h"
#include "charset.h"
#include "conn.h"

#define NS "xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" " \
	   "xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:a=\"http://apple.com/ns/ical/\""

const char *caldav_status_text(int st)
{
	switch (st) {
	case 400: return "the server didn't understand the request";
	case 401: return "the server refused the user name or password (iCloud wants an app-specific password)";
	case 403: return "this calendar can't be changed (read-only?)";
	case 404: return "not found on the server";
	case 405: return "the server doesn't allow this here";
	case 409: return "the server refused: a conflict";
	case 412: return "it was changed elsewhere in the meantime; refresh and try again";
	case 413: return "too big for the server";
	case 415: return "the server doesn't take this kind of data";
	case 500: return "the server had an internal error";
	case 502: case 504: return "the gateway couldn't reach the server";
	case 503: return "the server is busy; try again later";
	}
	return st >= 500 ? "server error" : st >= 400 ? "request refused" : "unexpected answer";
}

/* ---------------- multistatus ---------------- */

typedef struct {
	char href[400];
	char name[128];			/* UTF-8 */
	char color[16];
	char ctag[100];
	char etag[100];
	char principal[400];
	char home[400];
	int is_cal, comp_seen, vevent, priv_seen, writable;
	SBUF data;
} DAVRESP;

typedef void (*RESP_FN)(DAVRESP *r, void *ud);

static void trim_copy(char *d, const char *s, long n, int size)
{
	while (n && (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t'))
		s++, n--;
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t'))
		n--;
	if (n > size - 1)
		n = size - 1;
	memcpy(d, s, n);
	d[n] = 0;
}

static void parse_multistatus(const char *xml, long n, RESP_FN fn, void *ud)
{
	XMLP x;
	char stack[16][48];
	int depth = 0, t, in_resp = 0;
	DAVRESP r;
	memset(&r, 0, sizeof(r));
	sb_init(&r.data);
	xml_init(&x, xml, n);
	while ((t = xml_next(&x)) != X_EOF) {
		const char *parent = depth > 0 ? stack[depth - 1] : "";
		const char *gp = depth > 1 ? stack[depth - 2] : "";
		if (t == X_START || t == X_EMPTY) {
			if (!strcmp(x.name, "response")) {
				in_resp = 1;
				sb_free(&r.data);
				memset(&r, 0, sizeof(r));
				sb_init(&r.data);
			}
			if (in_resp) {
				if (!strcmp(x.name, "calendar") && !strcmp(parent, "resourcetype"))
					r.is_cal = 1;
				else if (!strcmp(x.name, "comp") && !strcmp(parent, "supported-calendar-component-set")) {
					r.comp_seen = 1;
					if (str_istr(x.attrs, "VEVENT"))
						r.vevent = 1;
				} else if (!strcmp(parent, "privilege")) {
					r.priv_seen = 1;
					if (!strcmp(x.name, "write") || !strcmp(x.name, "write-content") || !strcmp(x.name, "all"))
						r.writable = 1;
				}
			}
			if (t == X_START && depth < 16)
				str_copy(stack[depth++], x.name, sizeof(stack[0]));
			continue;
		}
		if (t == X_END) {
			if (depth > 0)
				depth--;
			if (!strcmp(x.name, "response") && in_resp) {
				in_resp = 0;
				fn(&r, ud);
			}
			continue;
		}
		/* text */
		if (!in_resp)
			continue;
		if (!strcmp(parent, "href")) {
			if (!strcmp(gp, "response"))
				trim_copy(r.href, x.text, x.textlen, sizeof(r.href));
			else if (!strcmp(gp, "current-user-principal"))
				trim_copy(r.principal, x.text, x.textlen, sizeof(r.principal));
			else if (!strcmp(gp, "calendar-home-set"))
				trim_copy(r.home, x.text, x.textlen, sizeof(r.home));
		} else if (!strcmp(parent, "displayname")) {
			trim_copy(r.name, x.text, x.textlen, sizeof(r.name));
		} else if (!strcmp(parent, "getctag") || !strcmp(parent, "sync-token")) {
			if (!r.ctag[0] || !strcmp(parent, "getctag"))
				trim_copy(r.ctag, x.text, x.textlen, sizeof(r.ctag));
		} else if (!strcmp(parent, "calendar-color")) {
			trim_copy(r.color, x.text, x.textlen, sizeof(r.color));
		} else if (!strcmp(parent, "getetag")) {
			trim_copy(r.etag, x.text, x.textlen, sizeof(r.etag));
		} else if (!strcmp(parent, "calendar-data")) {
			sb_add(&r.data, x.text, x.textlen);
		}
	}
	xml_done(&x);
	sb_free(&r.data);
}

static int dav(const char *method, const char *url, const DAVAUTH *a, int depth, const char *body,
	       HTTPRESP *rs, SBUF *out, char *err, int errlen)
{
	HTTPREQ rq;
	char hdr[40];
	memset(&rq, 0, sizeof(rq));
	rq.method = method;
	rq.url = url;
	rq.user = a ? a->user : 0;
	rq.pass = a ? a->pass : 0;
	if (body) {
		rq.body = body;
		rq.bodylen = (long)strlen(body);
		rq.ctype = "application/xml; charset=utf-8";
	}
	if (depth >= 0) {
		snprintf(hdr, sizeof(hdr), "Depth: %d\r\n", depth);
		rq.headers = hdr;
	}
	rq.maxbody = 1500000L;
	if (!http_request(&rq, rs, out, err, errlen))
		return 0;
	if (rs->status != 207 && rs->status != 200) {
		snprintf(err, errlen, "%s (HTTP %d)", caldav_status_text(rs->status), rs->status);
		return 0;
	}
	return 1;
}

/* ---------------- discovery ---------------- */

typedef struct {
	char principal[400], home[400];
	int self_cal;
	DAVCAL self;
} PRINC;

static void got_principal(DAVRESP *r, void *ud)
{
	PRINC *p = ud;
	if (r->principal[0] && !p->principal[0])
		str_copy(p->principal, r->principal, sizeof(p->principal));
	if (r->home[0] && !p->home[0])
		str_copy(p->home, r->home, sizeof(p->home));
	if (r->is_cal)
		p->self_cal = 1;
}

typedef struct {
	DAVCAL *cals;
	int n, max;
	const char *base;
} CALLIST;

static void name_to_atari(char *d, const char *utf8, int size)
{
	long n;
	char *a = cs_to_atari(utf8, (long)strlen(utf8), CS_UTF8, &n);
	str_copy(d, a ? a : utf8, size);
	free(a);
}

static void got_calendar(DAVRESP *r, void *ud)
{
	CALLIST *l = ud;
	DAVCAL *c;
	if (!r->is_cal || (r->comp_seen && !r->vevent))
		return;			/* not a calendar, or one of tasks only */
	if (l->n >= l->max) {
		DAVCAL *nc;
		l->max = l->max ? l->max * 2 : 8;
		nc = realloc(l->cals, l->max * sizeof(DAVCAL));
		if (!nc)
			return;
		l->cals = nc;
	}
	c = &l->cals[l->n++];
	memset(c, 0, sizeof(*c));
	url_resolve(l->base, r->href, c->url, sizeof(c->url));
	name_to_atari(c->name, r->name[0] ? r->name : "Calendar", sizeof(c->name));
	/* Apple writes #RRGGBBAA */
	if (r->color[0] == '#' && strlen(r->color) >= 7) {
		memcpy(c->color, r->color, 7);
		c->color[7] = 0;
	}
	str_copy(c->ctag, r->ctag, sizeof(c->ctag));
	c->readonly = r->priv_seen && !r->writable;
}

int caldav_discover(const char *base, const DAVAUTH *a, DAVCAL **cals, int *ncals,
		    char *err, int errlen)
{
	static const char q_princ[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><d:current-user-principal/><c:calendar-home-set/>"
		"<d:resourcetype/><d:displayname/><cs:getctag/><a:calendar-color/>"
		"</d:prop></d:propfind>";
	static const char q_home[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><d:resourcetype/><d:displayname/><cs:getctag/>"
		"<a:calendar-color/><c:supported-calendar-component-set/>"
		"<d:current-user-privilege-set/></d:prop></d:propfind>";
	HTTPRESP rs;
	SBUF out;
	PRINC p;
	CALLIST l;
	URL u;
	char url[512], tried[512];
	int ok = 0;
	*cals = 0;
	*ncals = 0;
	if (!url_parse(base, &u)) {
		snprintf(err, errlen, "not a web address: %.60s", base);
		return 0;
	}
	sb_init(&out);
	memset(&p, 0, sizeof(p));
	/* 1. the principal: at the address given, or via .well-known */
	url_format(&u, url, sizeof(url));
	if (!strcmp(u.path, "/")) {
		char wk[512];
		url_resolve(url, "/.well-known/caldav", wk, sizeof(wk));
		if (dav("PROPFIND", wk, a, 0, q_princ, &rs, &out, err, errlen)) {
			str_copy(url, rs.final_url, sizeof(url));
			parse_multistatus(out.s, out.len, got_principal, &p);
		}
	}
	if (!p.principal[0] && !p.home[0] && !p.self_cal) {
		if (!dav("PROPFIND", url, a, 0, q_princ, &rs, &out, err, errlen))
			goto done;
		str_copy(url, rs.final_url, sizeof(url));
		parse_multistatus(out.s, out.len, got_principal, &p);
	}
	memset(&l, 0, sizeof(l));
	if (p.self_cal && !p.home[0]) {
		/* the address is a calendar itself */
		l.base = url;
		if (!dav("PROPFIND", url, a, 0, q_home, &rs, &out, err, errlen))
			goto done;
		parse_multistatus(out.s, out.len, got_calendar, &l);
		if (l.n)
			str_copy(l.cals[0].url, url, sizeof(l.cals[0].url));
		ok = 1;
		goto have;
	}
	/* 2. the calendar home, from the principal */
	if (!p.home[0] && p.principal[0]) {
		url_resolve(url, p.principal, tried, sizeof(tried));
		if (!dav("PROPFIND", tried, a, 0, q_princ, &rs, &out, err, errlen))
			goto done;
		str_copy(url, rs.final_url, sizeof(url));
		parse_multistatus(out.s, out.len, got_principal, &p);
	}
	if (!p.home[0]) {
		snprintf(err, errlen, "the server didn't say where the calendars are (no calendar-home-set)");
		goto done;
	}
	/* 3. the calendars in it */
	url_resolve(url, p.home, tried, sizeof(tried));
	if (!dav("PROPFIND", tried, a, 1, q_home, &rs, &out, err, errlen))
		goto done;
	l.base = rs.final_url;
	parse_multistatus(out.s, out.len, got_calendar, &l);
	ok = 1;
have:
	*cals = l.cals;
	*ncals = l.n;
	if (!l.n) {
		snprintf(err, errlen, "no calendars found in this account");
		ok = 0;
	}
done:
	sb_free(&out);
	return ok;
}

static void got_ctag(DAVRESP *r, void *ud)
{
	char *c = ud;
	if (r->ctag[0] && !c[0])
		str_copy(c, r->ctag, 100);
}

int caldav_ctag(const char *url, const DAVAUTH *a, char *ctag, int size, char *err, int errlen)
{
	static const char q[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<d:propfind " NS "><d:prop><cs:getctag/><d:sync-token/></d:prop></d:propfind>";
	HTTPRESP rs;
	SBUF out;
	char c[100];
	int ok;
	sb_init(&out);
	c[0] = 0;
	ok = dav("PROPFIND", url, a, 0, q, &rs, &out, err, errlen);
	if (ok)
		parse_multistatus(out.s, out.len, got_ctag, c);
	str_copy(ctag, c, size);
	sb_free(&out);
	return ok;
}

/* ---------------- events ---------------- */

typedef struct {
	DAVITEM *items;
	int n, max;
} ITEMS;

static void got_item(DAVRESP *r, void *ud)
{
	ITEMS *l = ud;
	if (!r->etag[0] || !r->href[0])
		return;			/* the collection itself */
	if (l->n >= l->max) {
		DAVITEM *ni;
		l->max = l->max ? l->max * 2 : 32;
		ni = realloc(l->items, l->max * sizeof(DAVITEM));
		if (!ni)
			return;
		l->items = ni;
	}
	l->items[l->n].href = strdup(r->href);
	l->items[l->n].etag = strdup(r->etag);
	if (l->items[l->n].href && l->items[l->n].etag)
		l->n++;
}

void caldav_free_items(DAVITEM *items, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		free(items[i].href);
		free(items[i].etag);
	}
	free(items);
}

int caldav_list(const char *url, const DAVAUTH *a, CTIME from, CTIME to,
		DAVITEM **items, int *n, char *err, int errlen)
{
	char q[700], f[24], t[24];
	HTTPRESP rs;
	SBUF out;
	ITEMS l;
	int ok;
	ct_format_ics(from, 3, f, sizeof(f));
	ct_format_ics(to, 3, t, sizeof(t));
	snprintf(q, sizeof(q), "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		 "<c:calendar-query " NS "><d:prop><d:getetag/></d:prop>"
		 "<c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\">"
		 "<c:time-range start=\"%s\" end=\"%s\"/></c:comp-filter></c:comp-filter></c:filter>"
		 "</c:calendar-query>", f, t);
	memset(&l, 0, sizeof(l));
	sb_init(&out);
	ok = dav("REPORT", url, a, 1, q, &rs, &out, err, errlen);
	if (ok)
		parse_multistatus(out.s, out.len, got_item, &l);
	sb_free(&out);
	*items = l.items;
	*n = l.n;
	return ok;
}

typedef struct {
	DAV_CB cb;
	void *ud;
	int stop, got;
} MGET;

static void got_data(DAVRESP *r, void *ud)
{
	MGET *m = ud;
	if (m->stop || !r->data.len)
		return;
	m->got++;
	if (!m->cb(m->ud, r->href, r->etag, r->data.s, r->data.len))
		m->stop = 1;
}

int caldav_multiget(const char *url, const DAVAUTH *a, char **hrefs, int n,
		    DAV_CB cb, void *ud, char *err, int errlen)
{
	int i, ok = 1;
	SBUF q, out;
	sb_init(&out);
	for (i = 0; i < n && ok; i += 20) {
		HTTPRESP rs;
		MGET m;
		int k;
		sb_init(&q);
		sb_adds(&q, "<?xml version=\"1.0\" encoding=\"utf-8\"?><c:calendar-multiget " NS
			"><d:prop><d:getetag/><c:calendar-data/></d:prop>");
		for (k = i; k < n && k < i + 20; k++) {
			char esc[600];
			xml_escape(hrefs[k], esc, sizeof(esc));
			sb_printf(&q, "<d:href>%s</d:href>", esc);
		}
		sb_adds(&q, "</c:calendar-multiget>");
		ok = dav("REPORT", url, a, 1, q.s, &rs, &out, err, errlen);
		sb_free(&q);
		if (!ok)
			break;
		memset(&m, 0, sizeof(m));
		m.cb = cb;
		m.ud = ud;
		parse_multistatus(out.s, out.len, got_data, &m);
		if (m.stop)
			break;
	}
	sb_free(&out);
	return ok;
}

int caldav_status;

int caldav_put(const char *url, const char *href, const DAVAUTH *a, const char *data, long len,
	       const char *etag, char *newetag, int size, char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF out;
	char target[512], hdr[160];
	int ok;
	url_resolve(url, href, target, sizeof(target));
	memset(&rq, 0, sizeof(rq));
	rq.method = "PUT";
	rq.url = target;
	rq.user = a->user;
	rq.pass = a->pass;
	rq.body = data;
	rq.bodylen = len;
	rq.ctype = "text/calendar; charset=utf-8";
	if (etag && *etag)
		snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
	else
		snprintf(hdr, sizeof(hdr), "If-None-Match: *\r\n");
	rq.headers = hdr;
	sb_init(&out);
	caldav_status = 0;
	ok = http_request(&rq, &rs, &out, err, errlen);
	sb_free(&out);
	if (!ok)
		return 0;
	caldav_status = rs.status;
	if (rs.status != 200 && rs.status != 201 && rs.status != 204) {
		snprintf(err, errlen, "%s (HTTP %d)", caldav_status_text(rs.status), rs.status);
		return 0;
	}
	str_copy(newetag, rs.etag, size);
	return 1;
}

int caldav_delete(const char *url, const char *href, const DAVAUTH *a, const char *etag,
		  char *err, int errlen)
{
	HTTPREQ rq;
	HTTPRESP rs;
	SBUF out;
	char target[512], hdr[160];
	int ok;
	url_resolve(url, href, target, sizeof(target));
	memset(&rq, 0, sizeof(rq));
	rq.method = "DELETE";
	rq.url = target;
	rq.user = a->user;
	rq.pass = a->pass;
	if (etag && *etag) {
		snprintf(hdr, sizeof(hdr), "If-Match: %s\r\n", etag);
		rq.headers = hdr;
	}
	sb_init(&out);
	caldav_status = 0;
	ok = http_request(&rq, &rs, &out, err, errlen);
	sb_free(&out);
	if (!ok)
		return 0;
	caldav_status = rs.status;
	if (rs.status == 404 || rs.status == 410)
		return 1;		/* gone already */
	if (rs.status != 200 && rs.status != 204) {
		snprintf(err, errlen, "%s (HTTP %d)", caldav_status_text(rs.status), rs.status);
		return 0;
	}
	return 1;
}
