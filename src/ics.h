/*
 * ics.h - iCalendar (RFC 5545): events in, events out, and the
 * expansion of repeating events into the days they fall on.
 *
 * Text is kept in the Atari character set (Hebrew at 0xC2-0xDC), in
 * logical order, like EMail keeps mail; the calendar data itself is
 * UTF-8.
 */
#ifndef ICS_H
#define ICS_H

#include "ctime.h"
#include "tz.h"
#include "util.h"

/* the frequencies; the first four keep their old numbers (ECAL.INF
   and the editor use them) */
enum { F_NONE, F_DAILY, F_WEEKLY, F_MONTHLY, F_YEARLY, F_HOURLY, F_MINUTELY, F_SECONDLY };
enum { RS_GREGORIAN, RS_HEBREW };		/* RSCALE (RFC 7529) */
enum { SKIP_OMIT, SKIP_BACKWARD, SKIP_FORWARD };

#define RR_MAXBY 32

/* a recurrence rule (RFC 5545 RRULE, with RFC 7529's RSCALE and SKIP).
   Lists hold what the rule names; masks are bit n for value n */
typedef struct {
	unsigned char freq;
	unsigned char wkst;		/* 0 Sunday, 1 Monday ... */
	short interval;
	long count;			/* 0: no limit */
	CTIME until;			/* CT_NONE, or the last start, in the event's wall time */
	unsigned char until_kind;	/* as read: 1 date, 2 wall clock, 3 UTC (rrule_parse) */
	unsigned char rscale, skip;
	unsigned char nbyday, nbymd, nbysetpos, nbyyd, nbywn;
	short byday[RR_MAXBY];		/* ordinal * 8 + weekday; ordinal 0 = every */
	signed char bymd[RR_MAXBY];	/* days of the month, negative from the end */
	short byyd[RR_MAXBY];		/* days of the year, -366..366 */
	signed char bywn[RR_MAXBY];	/* weeks of the year, -53..53 */
	short bysetpos[RR_MAXBY];	/* positions in each period's set */
	unsigned short bymonth;		/* bit m-1 for month m; 0 = any */
	unsigned short bymonth_leap;	/* RSCALE: bit m-1 for "mL", the leap month */
	unsigned long byhour;		/* bit h; 0 = any */
	unsigned long byminute[2];	/* bits 0-59 */
	unsigned long bysecond[2];	/* bits 0-60 */
} RRULE;

/* ordinal and weekday of a BYDAY entry */
void rr_byday(short v, int *ord, int *wd);

enum { ST_CONFIRMED, ST_TENTATIVE, ST_CANCELLED };

typedef struct {
	char *uid;
	char *summary, *location, *notes, *url;	/* Atari text, may be NULL */
	const TZONE *tz;	/* zone of the times; NULL: floating or all-day */
	/* timed events: UTC, or the wall clock when floating; all-day:
	   00:00 of the first day and of the day after the last */
	CTIME start, end;
	CTIME recurid;		/* an exception to a series: the start it replaces, else CT_NONE */
	RRULE *rr;		/* NULL: happens once */
	CTIME *exdate;		/* starts left out of the series (same kind as start) */
	short nexdate;
	CTIME *rdate;		/* starts added to it (RDATE), same kind as start */
	CTIME *rdate_end;	/* their ends when given as periods, else CT_NONE */
	short nrdate;
	RRULE *exrule;		/* rules of starts left out (EXRULE, from RFC 2445) */
	short nexrule;
	unsigned char range_future;	/* an exception for this and all later occurrences */
	short alarm;		/* minutes before the start (negative: after it, for
				   all-day events), -1: none */
	unsigned char allday, floating, status;
	unsigned char hasexc;	/* a series with exceptions: 1, 2 = some for all later ones too (calstore.c) */
	unsigned char gone;	/* not shown: "Canceled:" in the title, or an invitation declined
				   (read only; the event is written back as it was) */
	short seq;
	short cal;		/* the calendar it belongs to (calstore.c) */
	short res;		/* its resource in that calendar */
} EVENT;

void   ev_free(EVENT *e);
EVENT *ev_new(void);
EVENT *ev_dup(const EVENT *e);
void   ev_set(char **field, const char *atari);	/* replace a text field */

/* ---- reading ----
 * The parser is fed one line at a time (CRLF or LF, folded or not), so
 * a feed of several megabytes can come from a file in small pieces.
 * Every VEVENT found is handed to cb, which owns it from then on
 * (return 0 to stop). Events that happen once and lie entirely outside
 * [from, to) (wall time, days around it are fine) are dropped on the
 * way; pass CT_NONE for both to keep everything. */
typedef int (*ICS_CB)(EVENT *e, void *ud);

typedef struct ICSP ICSP;
ICSP *ics_begin(ICS_CB cb, void *ud, CTIME from, CTIME to);
void  ics_line(ICSP *p, const char *s, long n);	/* one physical line, without the newline */
int   ics_end(ICSP *p);				/* finish; how many events were handed out */
/* the whole text at once */
int   ics_parse(const char *text, long n, ICS_CB cb, void *ud, CTIME from, CTIME to);
/* a file, read in pieces */
int   ics_parse_file(const char *path, ICS_CB cb, void *ud, CTIME from, CTIME to);
/* now, UTC, for DTSTAMP and LAST-MODIFIED (set by the program) */
extern CTIME ics_now_utc;
/* the calendar's own name (X-WR-CALNAME) seen by the last parse, Atari text */
extern char ics_calname[64];

/* RRULE value -> rule; 0 if it can't be understood */
int  rrule_parse(const char *s, RRULE *r);
void rrule_format(const RRULE *r, const TZONE *tz, int allday, char *out, int size);
/* "Every week on Monday, Wednesday", "Every 2 months", "Every year" */
void rrule_describe(const RRULE *r, char *out, int size);

/* "-PT15M" -> -15; 1 on success */
int  ics_duration(const char *s, long *minutes);

/* ---- expanding ----
 * Calls add for every occurrence of e that overlaps [from, to), in the
 * wall time of the zone view (all-day and floating events keep their
 * dates and clock times). skip, if given, says whether an occurrence
 * (by its original start, in the event's own representation) is
 * replaced by an exception and must not be shown. */
typedef void (*OCC_CB)(EVENT *e, CTIME start, CTIME end, void *ud);
typedef int  (*SKIP_CB)(EVENT *master, CTIME orig_start, void *ud);
void ics_expand(EVENT *e, CTIME from, CTIME to, const TZONE *view,
		OCC_CB add, SKIP_CB skip, void *ud);
/* the start of e in the wall time of view (for the editor and lists) */
CTIME ev_wall(const EVENT *e, CTIME t, const TZONE *view);
/* wall time in view -> the event's own representation */
CTIME ev_from_wall(const EVENT *e, CTIME wall, const TZONE *view);

/* ---- writing ----
 * The iCalendar text for e, UTF-8, CRLF lines folded at 75 bytes.
 * orig: the text the event came in (or NULL for a new one): everything
 * this program doesn't edit (attendees, attachments, other clients'
 * properties, exceptions) is kept. An event with recurid set is written
 * as an exception inside orig. Returns malloc'ed text. */
char *ics_build(const EVENT *e, const char *orig, long origlen, long *outlen);
/* orig without the exception for the start recurid (or all of them
   with CT_NONE); malloc'ed */
char *ics_drop_exceptions(const char *orig, long origlen, CTIME recurid, long *outlen);
/* a new UID: 32 hex digits and "@atari" */
void ics_new_uid(char *out, int size);

/* text helpers, exposed for the tests */
char *ics_unescape(const char *s, long n);	/* \n \, \; \\ -> text, UTF-8 -> Atari */
void  ics_escape(SBUF *b, const char *atari);	/* Atari text -> escaped UTF-8 */
/* the user's own address (an account's Apple ID): an invitation they
   declined is not shown */
extern char ics_me[100];

#endif
