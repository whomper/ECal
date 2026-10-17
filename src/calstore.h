/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * calstore.h - accounts, calendars and their events, the settings in
 * ECAL.INF, the copies kept on disk, and keeping them in step with
 * the servers.
 *
 * Three kinds of account, as on the Mac:
 *   CalDAV        iCloud, Fastmail, Nextcloud...: calendars you can change
 *   subscription  a published calendar (webcal:// or https:// .ics), read-only
 *   On My Atari   calendars kept only on this computer
 */
#ifndef CALSTORE_H
#define CALSTORE_H

#include "ics.h"
#include "tz.h"

enum { AK_CALDAV, AK_SUBSCR, AK_LOCAL };

#define NCOLORS 12	/* red orange yellow green blue purple brown grey pink teal navy dark green */

typedef struct {
	char *href, *etag;	/* where the server keeps it */
	long off, len;		/* its iCalendar text in the cache file */
	short dead;		/* replaced or deleted by a record further on in the file */
} RESOURCE;

typedef struct {
	short id;		/* cache file CALxxxxx.DAT / .ICS */
	short acct;
	char name[64];		/* Atari text */
	char url[400];
	char ctag[100];
	char etag[100], lastmod[40];	/* subscriptions: for "has it changed?" */
	short color;		/* 0..NCOLORS-1 */
	unsigned char visible, readonly, alarms;
	CTIME synced;		/* UTC; 0 = never */
	EVENT **ev;
	int nev, maxev;
	int lost;		/* events read but not kept: no memory left (-1: some, the rest unread) */
	RESOURCE *res;
	int nres, maxres;
	int ndead;		/* superseded records in the cache file (rewritten when many) */
} CALENDAR;

typedef struct {
	char name[40];
	short kind;
	char url[400];
	char user[80], pass[80];
	short refresh;		/* minutes between automatic refreshes, 0 = only by hand */
	char err[120];		/* last problem, shown in the sidebar */
} ACCOUNT;

typedef struct {
	char home_tz[40];	/* the zone the Atari's clock runs in */
	char view_tz[40];	/* the zone times are shown in (time zone support) */
	short tzsupport;	/* show and edit zones per event */
	short falcon, dsp;	/* Falcon mode: TLS here, RSA on the DSP */
	char gate[64];		/* the Raspberry Pi gateway */
	unsigned short gateport;
	short weekstart;	/* 0 Sunday, 1 Monday, 6 Saturday */
	short h24;
	short hebdates;		/* show Hebrew dates */
	short view;		/* V_DAY ... */
	short daystart;		/* first hour shown in day and week views */
	short alarms;		/* show reminders */
	short alarm_default;	/* minutes before, for new events; -1 none */
	short log;
	short font_id, font_pt, hebfont;
	short win_x, win_y, win_w, win_h;
	short sidebar;
	short past_days, future_days;	/* what is kept from the servers */
	short duration;		/* default length of a new event, minutes */
	short refresh;		/* minutes between automatic refreshes of all; 0 = by hand */
	short sync_open;	/* refresh when the window opens */
	short bgsync;		/* the accessory refreshes with its window closed too */
	char start[12];		/* when the accessory reads the calendars: "now", "opened",
				   or seconds after boot (common/ui/bgwork.h) */
	short offline;		/* Work offline: no connections */
	short hebrew;		/* start with the Hebrew keyboard */
	short heapkb;		/* the accessory's memory, KB (read at boot by main.c); 0: chosen */
	char workdir[160];	/* ECal's folder in use: CACHE, ECAL.KEY, the log... */
	char folder[160];	/* FOLDER= as ECAL.INF had it (main.c decides) */
} OPT;

enum { V_DAY, V_WEEK, V_MONTH, V_YEAR };
#define HEB_ATARI 0	/* where the font has Hebrew (draw.c) */
#define HEB_ISO   1
#define HEB_DOS   2

extern OPT opt;
#define MAXACCT 12
#define MAXCAL  32
extern ACCOUNT accts[MAXACCT];
extern int naccts;
extern CALENDAR cals[MAXCAL];
extern int ncals;
extern char cal_err[200];		/* what went wrong last */
extern void (*cal_status)(const char *msg);	/* progress for the status line */
extern int store_lost_passwords;
extern int store_plain_passwords;

/* start-up: settings and the cached calendars from dir */
/* the settings from inf (NULL: dir\ECAL.INF); the rest of ECal's files in dir */
void store_init(const char *dir, const char *inf);
/* the same in steps: begin, then store_init_step() until 0 */
void store_init_begin(const char *dir, const char *inf);
int  store_init_step(void);
void store_save_settings(void);
/* the accessory and ECAL.PRG (the AERA guidelines, 2.3): with store_watch
   set, our own writes are noted (store_mark), and store_changed() says
   whether another program changed ECAL.INF, ECAL.KEY or a calendar's
   cache since; store_release() lets everything go, to be read again */
extern int store_watch;
void store_mark(void);
int  store_changed(void);
void store_release(void);
void cal_load_all(void);
void cal_load(CALENDAR *c);
/* the same in steps, between event calls: begin, then one cached event
   (or a few lines of a subscription) per step until it returns 0 */
void cal_load_begin(CALENDAR *c);
int  cal_load_step(void);
int  cal_loading(void);		/* a calendar is being read in steps */
void cal_load_stop(void);
/* 1: a refresh that brings changes only begins reading the calendar
   again (cal_load_begin), for the caller to go on in steps */
extern int cal_defer_load;
/* the files the calendars are read from (ECAL.INF, the caches): for
   ALARMS.IDX's stamp; paths in a static table, how many */
int  store_files(const char **out, int max);
/* store_mark() a file at a time: 0 when done */
int  store_mark_step(void);
/* the zones now in use */
const TZONE *home_zone(void);
const TZONE *view_zone(void);
CTIME now_utc(void);			/* from the Atari clock and the home zone */
CTIME now_view(void);			/* in the view zone's wall time */
CTIME now_clock(void);		/* the Atari's clock: the home zone's wall time */
CTIME view_to_clock(CTIME t);	/* the view's wall time -> the clock's */

/* accounts */
ACCOUNT  *acct_add(int kind);
void      acct_delete(int a);
CALENDAR *cal_add(int acct, const char *name);
void      cal_delete(int c);
CALENDAR *cal_by_id(int id);
int       cal_writable(const CALENDAR *c);
/* find the calendars of a CalDAV account and merge them into the list */
int  acct_discover(int a);

/* bring calendars up to date with their servers: one account, or all
   (a = -1). Returns how many calendars changed; cal_err on failure */
int  sync_account(int a, int force);
int  sync_all(int force);
int  sync_calendar(CALENDAR *c, int force);

/* ---- occurrences in a range (view wall time) ---- */
typedef struct {
	EVENT *ev;		/* the event, or the exception standing in for this occurrence */
	CTIME start, end;	/* wall time in the view zone */
	CTIME orig;		/* the start the series gave it (the event's representation):
				   what "only this one" changes and deletes are about */
} OCC;

typedef struct {
	OCC *o;
	int n, max;
	CTIME from, to;
	int limit;		/* at most this many (0: no limit) */
} OCCLIST;

void occ_collect(OCCLIST *l, CTIME from, CTIME to);	/* sorted: all-day first, then by start */
void occ_free(OCCLIST *l);
/* everything matching text (title, place, notes) between from and to */
void occ_search(OCCLIST *l, const char *text, CTIME from, CTIME to);

/* ---- changing events ---- */
/* the event's iCalendar text from the cache (malloc'ed), or NULL */
char *ev_raw(const EVENT *e, long *len);
/* the series an exception belongs to */
EVENT *ev_master(const EVENT *e);

enum { SAVE_ALL, SAVE_THIS, SAVE_FUTURE };
/* save e (new: e->res < 0) into calendar cal. For a repeating event,
   how says which occurrences change; occ is the original start of the
   one being edited (event representation) */
/* e's attendees and organizer, read from the cache (the editor) */
void ev_people(EVENT *e);
/* ics_me: the address of calendar ci's account (the organizer of the
   invitations made there; "" when its user name isn't one) */
void cal_me(int ci);
int  ev_save(EVENT *e, int cal, int how, CTIME occ);
/* delete; for repeating ones how/occ as above */
int  ev_delete(EVENT *e, int how, CTIME occ);

/* import an .ics file into a calendar */
int  cal_import(int cal, const char *path, int *count);

#endif
