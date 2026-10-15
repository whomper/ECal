/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * The calendar server's answers, from the fuzz input (the AERA
 * guidelines, 7.1): HTTP (status, headers, Content-Length, chunks, gzip,
 * redirects, logins), the WebDAV XML and the events in it, read by
 * ECal's own CalDAV code. The first byte picks the request and the way
 * (straight, or through the gateway).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "caldav.h"
#include "http.h"
#include "fuzz_net.h"

static int got(void *ud, const char *href, const char *etag, const char *data, long len)
{
	volatile long sum = 0;
	long i;
	(void)ud;
	for (i = 0; i < len; i++)
		sum += data[i];
	sum += (long)strlen(href) + (long)strlen(etag);
	return 1;
}

static int sink(void *ud, const char *data, long n)
{
	(void)ud;
	(void)data;
	(void)n;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char cal[] = "http://cal.example.org/calendars/me/home/";
	char err[200], ctag[100], etag[100];
	DAVAUTH a = { "user@example.org", "secret" };
	int sel;
	if (n < 1)
		return 0;
	sel = d[0];
	fuzz_net_set(d + 1, (long)n - 1);
	memset(&http_cfg, 0, sizeof(http_cfg));
	if (sel & 0x80) {
		strcpy(http_cfg.gate, "pi.example");
		http_cfg.gateport = 8180;
	}
	switch (sel & 7) {
	case 0: {
		DAVCAL *cals = 0;
		int nc = 0;
		caldav_discover("http://cal.example.org/", &a, &cals, &nc, err, sizeof(err));
		free(cals);
		break;
	}
	case 1:
		caldav_ctag(cal, &a, ctag, sizeof(ctag), err, sizeof(err));
		break;
	case 2: {
		DAVITEM *items = 0;
		int ni = 0;
		if (caldav_list(cal, &a, 0, 0x7fffffffL, &items, &ni, err, sizeof(err)))
			caldav_free_items(items, ni);
		break;
	}
	case 3: {
		char *hrefs[] = { "/calendars/me/home/a.ics", "/calendars/me/home/b.ics" };
		caldav_multiget(cal, &a, hrefs, 2, got, 0, err, sizeof(err));
		break;
	}
	case 4:
		caldav_put(cal, "c.ics", &a, "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n", 31, (sel & 8) ? "\"1\"" : 0,
			   etag, sizeof(etag), err, sizeof(err));
		break;
	case 5:
		caldav_delete(cal, "c.ics", &a, "\"1\"", err, sizeof(err));
		break;
	default: {
		/* a subscription: GET, to memory or (as ECal does) to a file */
		HTTPREQ rq;
		HTTPRESP rs;
		SBUF body;
		memset(&rq, 0, sizeof(rq));
		rq.method = "GET";
		rq.url = "http://feeds.example.org/holidays.ics";
		if (sel & 1)
			rq.sink = sink;
		rq.maxbody = 64L * 1024;
		sb_init(&body);
		http_request(&rq, &rs, &body, err, sizeof(err));
		sb_free(&body);
	}
	}
	http_close_all();
	return 0;
}
