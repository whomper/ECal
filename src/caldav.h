/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * caldav.h - CalDAV (RFC 4791), the calendar protocol iCloud, Fastmail,
 * Nextcloud, Radicale and others speak: WebDAV requests carrying
 * iCalendar data.
 */
#ifndef CALDAV_H
#define CALDAV_H

#include "ctime.h"
#include "util.h"

typedef struct {
	char url[400];		/* the calendar collection, absolute */
	char name[64];		/* its name, Atari text */
	char color[10];		/* "#RRGGBB" or "" */
	char ctag[100];		/* changes whenever anything in it changes */
	int readonly;
} DAVCAL;

typedef struct {
	char *href;		/* as the server wrote it (a path, usually) */
	char *etag;
} DAVITEM;

typedef struct {
	const char *user, *pass;
} DAVAUTH;

/* the calendars of an account. base: the server's address, e.g.
   "https://caldav.icloud.com" (the principal and calendar home are
   found from there), or a calendar's own URL. *cals is malloc'ed */
int caldav_discover(const char *base, const DAVAUTH *a, DAVCAL **cals, int *ncals,
		    char *err, int errlen);

/* the calendar's ctag now; "" when the server doesn't give one */
int caldav_ctag(const char *url, const DAVAUTH *a, char *ctag, int size, char *err, int errlen);

/* hrefs and etags of the events touching [from, to) (UTC) */
int caldav_list(const char *url, const DAVAUTH *a, CTIME from, CTIME to,
		DAVITEM **items, int *n, char *err, int errlen);
void caldav_free_items(DAVITEM *items, int n);

/* fetch events by href, a few at a time; cb gets each (UTF-8 text) */
typedef int (*DAV_CB)(void *ud, const char *href, const char *etag, const char *data, long len);
int caldav_multiget(const char *url, const DAVAUTH *a, char **hrefs, int n,
		    DAV_CB cb, void *ud, char *err, int errlen);

/* store an event at href (relative to the calendar url): etag NULL for
   a new one (fails if it exists), else only if unchanged since. The
   new etag comes back in newetag ("" if the server didn't say) */
extern int caldav_status;	/* HTTP status of the last PUT or DELETE; 0: no answer */
int caldav_put(const char *url, const char *href, const DAVAUTH *a, const char *data, long len,
	       const char *etag, char *newetag, int size, char *err, int errlen);
int caldav_delete(const char *url, const char *href, const DAVAUTH *a, const char *etag,
		  char *err, int errlen);

/* an HTTP status as words for alerts */
const char *caldav_status_text(int status);

#endif
