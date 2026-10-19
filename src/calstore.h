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
void store_init_text(char *text);	/* after store_init_begin: ECAL.INF already read */
extern unsigned long store_gen;	/* goes up with each of our writes to ECAL.INF or a cache */
void store_save_settings(void);
void store_write_settings(void);	/* the same without noting the files' stamps (store_mark_step) */
void store_settings_text(SBUF *b, int part);	/* or in steps: its text in b (sb_init'ed; part 0, 1), */
void store_write_text(SBUF *b);		/* then the file written (b freed) */
/* the accessory and ECAL.PRG (the AERA guidelines, 2.3): with store_watch
   set, our own writes are noted (store_mark), and store_changed() says
   whether another program changed ECAL.INF, ECAL.KEY or a calendar's
   cache since; store_release() lets everything go, to be read again */
extern int store_watch;
void store_mark(void);
int  store_changed(void);
int  store_changed_step(int *changed);	/* the same a file a step: 1 while there is more */
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
extern long cal_piece;		/* a file read this much at a time (guidelines 6.5) */
extern long cal_gather;		/* a job gathers this much before it writes */
/* the files store_changed looks at (ECAL.INF, ECAL.KEY, the CACHE
   folder): for ALARMS.IDX's stamp, which is the same; paths in a static
   table, how many (3) */
int  store_files(const char **out, int max);
/* store_mark() a file at a time: 0 when done */
int  store_mark_step(void);
/* their stamp as last looked at, if nothing was written since (1) */
int  store_known(unsigned long *h);
/* the files as they are now (stamp h, a look the caller took) are ours:
   store_mark_known always, store_adopt only after our writes, as
   store_changed does */
void store_mark_known(unsigned long h);
void store_adopt(unsigned long h);
unsigned long store_inf_seen(long *size);	/* ECAL.INF at the last look: date, size */
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
   (a = -1). Returns how many calendars changed; cal_err on failure.
   force: SYNC_ALL checks every calendar, and the calendar lists, even
   what the server says hasn't changed; SYNC_AGAIN also fetches every
   event again, keeping none of the copy (the calendars on the Atari
   itself stay as they are) */
enum { SYNC_CHANGED, SYNC_ALL, SYNC_AGAIN };
int  sync_account(int a, int force);
int  sync_all(int force);
int  sync_calendar(CALENDAR *c, int force);

/* The accessory's background refresh of a calendar, in steps
   (calstore.c explains). sync_begin: 0 if it isn't done that way (a
   subscription: sync_calendar, with cal_defer_load). sync_step: 1 while
   there is more; sync_net_step: the next step waits for the server.
   sync_changes (the caller clears it): 1 events changed, 2 a ctag to
   save in ECAL.INF */
int  sync_begin(CALENDAR *c, int force);
int  sync_step(void);
int  sync_busy(void);
int  sync_net_step(void);
const char *sync_phase_name(void);	/* what the next step does, for the profile build */
void sync_stop(void);
extern int sync_changes;

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
