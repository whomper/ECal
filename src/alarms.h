/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * alarms.h - the reminders of the next days, in the order they come.
 *
 * Looking for them every minute by expanding every event over nine days
 * held the machine for seconds (1.5 s on a Falcon, 14 s on an ST with
 * 580 events). Instead the queue is built once, a step (one event) at a
 * time between event calls, after the calendars are read and after
 * anything changes, and each minute only the clock is compared with it.
 *
 * It is kept in ALARMS.IDX next to the caches too, with what it was
 * built from, so the accessory reminds at boot without reading a single
 * calendar: those are read later, when it may work (START=) or is opened.
 */
#ifndef ALARMS_H
#define ALARMS_H

#include "ctime.h"

#define ALARM_DAYS 8		/* how far ahead the queue reaches */

/* Times are the Atari's own clock's (the home zone's wall time): the
   accessory compares them with it at boot, before it knows any zone */
typedef struct {
	CTIME fire;		/* when to remind, by the Atari's clock */
	CTIME start;		/* the occurrence's start, in the view's wall time (shown) */
	unsigned char allday;
	char title[64];		/* Atari text */
	char place[48];
} ALARM;

extern ALARM *alarm_q;		/* by fire time */
extern int alarm_n;
extern CTIME alarm_built;	/* when the queue was built, by the clock (0: none yet) */

/* the queue is out of date: the next steps build it again */
void alarms_changed(void);
int  alarms_pending(void);	/* a build to start or to go on with */
int  alarms_stale(void);	/* changed since the build in progress began */
/* one step of the build (calstore.c: one event); 0 when the queue is done */
int  alarms_step(void);
int  alarms_sorting(void);	/* the next step sorts the queue (calstore.c) */
/* the reminders due after "after" up to "upto" (the clock's times), in order */
void alarms_due(CTIME after, CTIME upto, void (*fn)(const ALARM *a, void *ud), void *ud);

/* for the builder (calstore.c) */
void alarms_begin_build(void);
void alarms_add(CTIME fire, CTIME start, int allday, const char *title, const char *place);
void alarms_end_build(CTIME built);

/* ALARMS.IDX: the queue, the reminder settings (on, 24-hour clock), and
   the files it was built from with a stamp of them (sizes, times), so a
   change made since is noticed without reading them. Each file's look at
   the disk is a step of its own (one directory search each), so neither
   the stamp nor the check at boot holds the machine:
     h = ALARM_STAMP0; for each file: h = alarms_stamp_add(h, file);
     alarms_save(path, files, n, h, on, h24);
   alarms_load: the queue at once, if the file is there and still reaches
   past now; then alarms_verify_step() until 0 checks its files, one per
   call, and empties the queue if one changed since */
#define ALARM_STAMP0 5381UL
unsigned long alarms_stamp_add(unsigned long h, const char *file);
int  alarms_save(const char *path, const char *const *files, int nfiles, unsigned long stamp, int on, int h24);
/* the same in steps: begin, then alarms_save_step() until it isn't 1 (0 written, -1 not) */
void alarms_save_begin(const char *path, const char *const *files, int nfiles, unsigned long stamp, int on, int h24);
int  alarms_save_step(void);
int  alarms_load(const char *path, CTIME now, int *on, int *h24);
int  alarms_verify_step(void);

#endif
