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

#define NCOLORS 8	/* red orange yellow green blue purple brown grey, as the Mac offers */

typedef struct {
	char *href, *etag;	/* where the server keeps it */
	long off, len;		/* its iCalendar text in the cache file */
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
	RESOURCE *res;
	int nres, maxres;
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
	char workdir[160];
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

/* start-up: settings and the cached calendars from dir */
void store_init(const char *dir);
void store_save_settings(void);
void cal_load_all(void);
void cal_load(CALENDAR *c);
/* the zones now in use */
const TZONE *home_zone(void);
const TZONE *view_zone(void);
CTIME now_utc(void);			/* from the Atari clock and the home zone */
CTIME now_view(void);			/* in the view zone's wall time */

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
int  ev_save(EVENT *e, int cal, int how, CTIME occ);
/* delete; for repeating ones how/occ as above */
int  ev_delete(EVENT *e, int how, CTIME occ);

/* import an .ics file into a calendar */
int  cal_import(int cal, const char *path, int *count);

#endif
