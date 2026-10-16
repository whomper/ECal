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
/* one step of the build (calstore.c: one event); 0 when the queue is done */
int  alarms_step(void);
/* the reminders due after "after" up to "upto" (the clock's times), in order */
void alarms_due(CTIME after, CTIME upto, void (*fn)(const ALARM *a, void *ud), void *ud);

/* for the builder (calstore.c) */
void alarms_begin_build(void);
void alarms_add(CTIME fire, CTIME start, int allday, const char *title, const char *place);
void alarms_end_build(CTIME built);

/* ALARMS.IDX: the queue, the reminder settings (on, 24-hour clock), and
   the files it was built from with a stamp of them (sizes, times), so a
   change made since is noticed without reading them. alarms_load: 1 if
   the file is there, matches its files and still reaches past now */
int  alarms_save(const char *path, const char *const *files, int nfiles, int on, int h24);
int  alarms_load(const char *path, CTIME now, int *on, int *h24);

#endif
