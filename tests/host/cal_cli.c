/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * cal_cli.c - the calendar core on Linux, for the integration tests and
 * for trying servers before the Atari does: the same calstore.c, sync,
 * CalDAV, HTTP and TLS code as ECAL.ACC.
 *
 *   cal-cli DIR caldav NAME URL USER PASS    add a CalDAV account and sync it
 *   cal-cli DIR subscribe URL [NAME]          add a subscription and fetch it
 *   cal-cli DIR local NAME                    an On My Atari calendar
 *   cal-cli DIR gateway HOST PORT             go through the Pi gateway
 *   cal-cli DIR falcon 0|1                    TLS here (CACERT=path for the roots)
 *   cal-cli DIR zone HOME [VIEW]              time zones
 *   cal-cli DIR sync [force]
 *   cal-cli DIR calendars
 *   cal-cli DIR list FROM TO                  occurrences, dates as YYYYMMDD
 *   cal-cli DIR add CAL TITLE START MINUTES [RRULE]   START as YYYYMMDDTHHMM, view zone
 *   cal-cli DIR rename TITLE NEWTITLE         every occurrence
 *   cal-cli DIR move-one TITLE OCCSTART NEWSTART       just one occurrence
 *   cal-cli DIR move-to TITLE CAL             to another calendar
 *   cal-cli DIR invite TITLE "Name <a@b>, c@d"   who is invited ("" nobody)
 *   cal-cli DIR people TITLE                  who is invited, and by whom
 *   cal-cli DIR alarms                        the reminders of the next days, checked
 *                                             against expanding every event
 *   cal-cli DIR delete TITLE [this|future OCCSTART]
 *   cal-cli DIR import CAL FILE
 * Titles are UTF-8 on the command line and in the output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calstore.h"
#include "alarms.h"
#include "charset.h"
#include "conn.h"
#include "tls.h"
#include "http.h"
#include "util.h"
#include "plat.h"

static void show_status(const char *s)
{
	if (getenv("CAL_VERBOSE") && *s)
		fprintf(stderr, "  .. %s\n", s);
}

static char *to_atari(const char *utf8)
{
	long n;
	return cs_to_atari(utf8, (long)strlen(utf8), CS_UTF8, &n);
}

static char *to_utf8(const char *atari)
{
	long n;
	return atari_to_utf8(atari ? atari : "", (long)strlen(atari ? atari : ""), &n);
}

static int fail(const char *what)
{
	fprintf(stderr, "cal-cli: %s: %s\n", what, cal_err[0] ? cal_err : "failed");
	return 1;
}

static CTIME parse_view(const char *s)
{
	CTIME t;
	char buf[24];
	if (strlen(s) == 13) {		/* YYYYMMDDTHHMM */
		snprintf(buf, sizeof(buf), "%s00", s);
		s = buf;
	}
	if (!ct_parse(s, &t)) {
		fprintf(stderr, "cal-cli: bad time %s\n", s);
		exit(2);
	}
	return t;
}

static void print_occ(const OCC *o)
{
	DT a, b;
	char *t = to_utf8(o->ev->summary), *cn = to_utf8(cals[o->ev->cal].name);
	ct_split(o->start, &a);
	ct_split(o->end, &b);
	if (o->ev->allday)
		printf("%04d%02d%02d all-day   [%s] %s\n", a.year, a.mon, a.day, cn, t);
	else
		printf("%04d%02d%02d %02d:%02d-%02d:%02d [%s] %s%s\n", a.year, a.mon, a.day, a.hour, a.min,
		       b.hour, b.min, cn, t, o->ev->recurid != CT_NONE ? " (changed)" : "");
	free(t);
	free(cn);
}

/* the event with this title, and the occurrence starting at occ */
static EVENT *find(const char *title_utf8, CTIME occ_view, CTIME *occ_rep)
{
	char *t = to_atari(title_utf8);
	OCCLIST l;
	EVENT *found = 0;
	int i;
	memset(&l, 0, sizeof(l));
	occ_collect(&l, now_view() - 800L * MIN_PER_DAY, now_view() + 800L * MIN_PER_DAY);
	for (i = 0; i < l.n; i++) {
		EVENT *e = l.o[i].ev;
		if (!e->summary || strcmp(e->summary, t))
			continue;
		if (occ_view != CT_NONE && l.o[i].start != occ_view)
			continue;
		found = e;
		if (occ_rep)
			*occ_rep = l.o[i].orig;
		break;
	}
	occ_free(&l);
	free(t);
	return found;
}

int main(int argc, char **argv)
{
	const char *dir, *cmd;
	if (argc < 3) {
		fprintf(stderr, "usage: cal-cli DIR command ... (see cal_cli.c)\n");
		return 2;
	}
	dir = argv[1];
	cmd = argv[2];
	cal_status = show_status;
	if (getenv("CAL_LOG"))
		str_copy(conn_logfile, getenv("CAL_LOG"), sizeof(conn_logfile));
	if (getenv("CACERT")) {
		str_copy(conn_cacert, getenv("CACERT"), sizeof(conn_cacert));
		path_join(tls_roots_cache, sizeof(tls_roots_cache), dir, "ROOTS.DAT");
	}
	path_join(tls_seed_path, sizeof(tls_seed_path), dir, "SEED.DAT");
	pf_mkdir(dir);
	store_init(dir, 0);
	tls_tz_minutes = tz_offset(home_zone(), now_utc());
	cal_load_all();

	if (!strcmp(cmd, "caldav") && argc == 7) {
		ACCOUNT *a = acct_add(AK_CALDAV);
		str_copy(a->name, argv[3], sizeof(a->name));
		str_copy(a->url, argv[4], sizeof(a->url));
		str_copy(a->user, argv[5], sizeof(a->user));
		str_copy(a->pass, argv[6], sizeof(a->pass));
		store_save_settings();
		if (sync_account(naccts - 1, 1) < 0)
			return fail("sync");
		store_save_settings();
		return 0;
	}
	if (!strcmp(cmd, "subscribe") && argc >= 4) {
		ACCOUNT *a = acct_add(AK_SUBSCR);
		CALENDAR *c;
		str_copy(a->name, argc > 4 ? argv[4] : "Subscription", sizeof(a->name));
		str_copy(a->url, argv[3], sizeof(a->url));
		c = cal_add(naccts - 1, argc > 4 ? argv[4] : "Subscription");
		str_copy(c->url, argv[3], sizeof(c->url));
		if (sync_calendar(c, 1) < 0)
			return fail("subscribe");
		store_save_settings();
		printf("%s: %d events\n", c->name, c->nev);
		return 0;
	}
	if (!strcmp(cmd, "local") && argc == 4) {
		int i, ai = -1;
		for (i = 0; i < naccts; i++)
			if (accts[i].kind == AK_LOCAL)
				ai = i;
		if (ai < 0) {
			ACCOUNT *a = acct_add(AK_LOCAL);
			str_copy(a->name, "On My Atari", sizeof(a->name));
			ai = naccts - 1;
		}
		cal_add(ai, argv[3]);
		store_save_settings();
		return 0;
	}
	if (!strcmp(cmd, "gateway") && argc == 5) {
		str_copy(opt.gate, argv[3], sizeof(opt.gate));
		opt.gateport = (unsigned short)atoi(argv[4]);
		opt.falcon = 0;
		store_save_settings();
		return 0;
	}
	if (!strcmp(cmd, "falcon") && argc == 4) {
		opt.falcon = (short)atoi(argv[3]);
		store_save_settings();
		return 0;
	}
	if (!strcmp(cmd, "zone") && argc >= 4) {
		str_copy(opt.home_tz, argv[3], sizeof(opt.home_tz));
		str_copy(opt.view_tz, argc > 4 ? argv[4] : argv[3], sizeof(opt.view_tz));
		opt.tzsupport = argc > 4;
		store_save_settings();
		return 0;
	}
	if (!strcmp(cmd, "sync")) {
		int r = sync_all(argc > 3);
		if (r < 0)
			return fail("sync");
		printf("%d calendar%s changed\n", r, r == 1 ? "" : "s");
		return 0;
	}
	if (!strcmp(cmd, "bgsync") && argc >= 3) {
		/* the accessory's background refresh, in steps; then (with
		   FROM TO) the events in memory, to compare with a fresh read;
		   with STOP, stopped after that many steps (the window opened) */
		int i, n = 0, steps = 0, stop = argc == 6 ? atoi(argv[5]) : -1;
		for (i = 0; i < naccts; i++)
			accts[i].err[0] = 0;
		for (i = 0; i < ncals; i++) {
			sync_changes = 0;
			if (sync_begin(&cals[i], 0)) {
				while (steps != stop && sync_step())
					steps++;
				if (steps == stop)
					sync_stop();
				n += sync_changes & 1;
			} else if (sync_calendar(&cals[i], 0) > 0) {
				n++;
			}
			if (accts[cals[i].acct].err[0])
				return fail(accts[cals[i].acct].err);
		}
		store_save_settings();
		printf("%d calendar%s changed in %d steps\n", n, n == 1 ? "" : "s", steps);
		if (argc >= 5) {
			OCCLIST l;
			memset(&l, 0, sizeof(l));
			occ_collect(&l, parse_view(argv[3]), parse_view(argv[4]));
			for (i = 0; i < l.n; i++)
				print_occ(&l.o[i]);
			occ_free(&l);
		}
		return 0;
	}
	if (!strcmp(cmd, "calendars")) {
		int i;
		for (i = 0; i < ncals; i++) {
			char *n = to_utf8(cals[i].name);
			printf("%d %s (%s, %d events%s)\n", i, n, accts[cals[i].acct].name, cals[i].nev,
			       cal_writable(&cals[i]) ? "" : ", read-only");
			free(n);
		}
		return 0;
	}
	if (!strcmp(cmd, "list") && argc == 5) {
		OCCLIST l;
		int i;
		memset(&l, 0, sizeof(l));
		occ_collect(&l, parse_view(argv[3]), parse_view(argv[4]));
		for (i = 0; i < l.n; i++)
			print_occ(&l.o[i]);
		occ_free(&l);
		return 0;
	}
	if (!strcmp(cmd, "find") && argc == 4) {
		/* as the Find dialog: a year back to two years ahead */
		OCCLIST l;
		CTIME now = now_view();
		int i;
		memset(&l, 0, sizeof(l));
		occ_search(&l, argv[3], now - 366L * MIN_PER_DAY, now + 731L * MIN_PER_DAY);
		printf("%d found (%d slots)\n", l.n, l.max);
		for (i = 0; i < l.n && i < 5; i++)
			printf("  %s\n", l.o[i].ev->summary ? l.o[i].ev->summary : "");
		occ_free(&l);
		return 0;
	}
	if (!strcmp(cmd, "add") && argc >= 7) {
		EVENT *e = ev_new();
		int ci = atoi(argv[3]);
		CTIME start = parse_view(argv[5]);
		e->summary = to_atari(argv[4]);
		if (atoi(argv[6]) == 0) {
			e->allday = 1;
			e->start = ct_day_start(start);
			e->end = e->start + MIN_PER_DAY;
		} else {
			e->tz = view_zone();
			e->start = tz_to_utc(e->tz, start);
			e->end = e->start + atoi(argv[6]);
		}
		if (argc > 7) {
			e->rr = calloc(1, sizeof(RRULE));
			if (!rrule_parse(argv[7], e->rr))
				return fail("rrule");
		}
		e->alarm = 10;
		if (!ev_save(e, ci, SAVE_ALL, CT_NONE))
			return fail("add");
		ev_free(e);
		return 0;
	}
	if (!strcmp(cmd, "rename") && argc == 5) {
		EVENT *e = find(argv[3], CT_NONE, 0), *m, *c;
		if (!e)
			return fail("no such event");
		m = ev_master(e);
		c = ev_dup(m ? m : e);
		{ char *t_ = to_atari(argv[4]); ev_set(&c->summary, t_); free(t_); }
		if (!ev_save(c, c->cal, SAVE_ALL, CT_NONE))
			return fail("rename");
		ev_free(c);
		return 0;
	}
	if (!strcmp(cmd, "alarms") && argc == 3) {
		/* the queue, and the old way: every event over nine days */
		OCCLIST l;
		CTIME now = now_view();
		int i, n = 0, bad = 0;
		unsigned long t0 = pf_ms();
		alarms_changed();
		while (alarms_step())
			;
		printf("queue: %d reminders in %lu ms\n", alarm_n, pf_ms() - t0);
		memset(&l, 0, sizeof(l));
		t0 = pf_ms();
		occ_collect(&l, now - MIN_PER_DAY, now + 9 * MIN_PER_DAY);
		for (i = 0; i < l.n; i++) {
			EVENT *e = l.o[i].ev;
			CTIME fire;
			int k, found = 0;
			if (e->alarm == -1 || !cals[e->cal].alarms)
				continue;
			fire = l.o[i].start - e->alarm;
			if (fire <= now - MIN_PER_DAY || fire > now + ALARM_DAYS * MIN_PER_DAY)
				continue;
			n++;
			for (k = 0; k < alarm_n && !found; k++)
				found = alarm_q[k].fire == view_to_clock(fire) && alarm_q[k].start == l.o[i].start;
			if (!found) {
				char *t = to_utf8(e->summary ? e->summary : "");
				printf("missing: %s at %ld\n", t, (long)l.o[i].start);
				free(t);
				bad++;
			}
		}
		printf("expanded: %d reminders in %lu ms\n", n, pf_ms() - t0);
		occ_free(&l);
		for (i = 0; i < alarm_n; i++) {
			char *t = to_utf8(alarm_q[i].title);
			DT d;
			ct_split(alarm_q[i].start, &d);
			printf("%04d%02d%02d %02d:%02d %s\n", d.year, d.mon, d.day, d.hour, d.min, t);
			free(t);
		}
		if (n != alarm_n)
			bad++;
		return bad ? fail("the queue and the expansion differ") : 0;
	}
	if (!strcmp(cmd, "invite") && argc == 5) {
		EVENT *e = find(argv[3], CT_NONE, 0), *m, *c;
		if (!e)
			return fail("no such event");
		m = ev_master(e);
		c = ev_dup(m ? m : e);
		{ char *t_ = to_atari(argv[4]); ev_set(&c->attendees, t_); free(t_); }
		c->att_edited = 1;
		if (!ev_save(c, c->cal, SAVE_ALL, CT_NONE))
			return fail("invite");
		ev_free(c);
		return 0;
	}
	if (!strcmp(cmd, "people") && argc == 4) {
		EVENT *e = find(argv[3], CT_NONE, 0);
		if (!e)
			return fail("no such event");
		cal_me(e->cal);
		ev_people(e);
		printf("organizer: %s\ninvited: %s\ninvitation: %s\n", e->organizer ? e->organizer : "",
		       e->attendees ? e->attendees : "", ev_invited(e) ? "yes" : "no");
		return 0;
	}
	if (!strcmp(cmd, "move-to") && argc == 5) {
		/* an event to another calendar (its number in "calendars") */
		EVENT *e = find(argv[3], CT_NONE, 0), *m, *c;
		if (!e)
			return fail("no such event");
		m = ev_master(e);
		c = ev_dup(m ? m : e);
		if (!ev_save(c, atoi(argv[4]), SAVE_ALL, CT_NONE))
			return fail("move-to");
		ev_free(c);
		return 0;
	}
	if (!strcmp(cmd, "move-one") && argc == 6) {
		CTIME occ, ns = parse_view(argv[5]);
		EVENT *e = find(argv[3], parse_view(argv[4]), &occ), *c;
		if (!e)
			return fail("no such occurrence");
		c = ev_dup(e);
		c->start = ev_from_wall(e, ns, view_zone());
		c->end = c->start + (e->end - e->start);
		if (!ev_save(c, c->cal, SAVE_THIS, occ))
			return fail("move-one");
		ev_free(c);
		return 0;
	}
	if (!strcmp(cmd, "delete") && argc >= 4) {
		CTIME occ = CT_NONE;
		int how = SAVE_ALL;
		EVENT *e;
		if (argc == 6) {
			how = !strcmp(argv[4], "this") ? SAVE_THIS : SAVE_FUTURE;
			e = find(argv[3], parse_view(argv[5]), &occ);
		} else {
			e = find(argv[3], CT_NONE, 0);
		}
		if (!e)
			return fail("no such event");
		if (!ev_delete(e, how, occ))
			return fail("delete");
		return 0;
	}
	if (!strcmp(cmd, "import") && argc == 5) {
		int n;
		if (!cal_import(atoi(argv[3]), argv[4], &n))
			return fail("import");
		printf("%d imported\n", n);
		return 0;
	}
	fprintf(stderr, "cal-cli: unknown command %s\n", cmd);
	return 2;
}
