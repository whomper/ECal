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
 *   cal-cli DIR sync [force|again]            again: every event fetched anew
 *   cal-cli DIR calendars
 *   cal-cli DIR list FROM TO                  occurrences, dates as YYYYMMDD
 *   cal-cli DIR add CAL TITLE START MINUTES [RRULE]   START as YYYYMMDDTHHMM, view zone
 *   cal-cli DIR rename TITLE NEWTITLE         every occurrence
 *   cal-cli DIR move-one TITLE OCCSTART NEWSTART       just one occurrence
 *   cal-cli DIR move-to TITLE CAL             to another calendar
 *   cal-cli DIR invite TITLE "Name <a@b>, c@d"   who is invited ("" nobody)
 *   cal-cli DIR people TITLE                  who is invited, and by whom
 *   cal-cli DIR notes TITLE                   its notes, as the day's details read them
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

/* With CAL_FILECHECK set, no file may be open where the accessory makes
   an event call: in pf_idle (the network's waits) and between steps
   (guidelines 2.2) */
extern int pf_open_files;

static void file_check(const char *where)
{
	if (pf_open_files && getenv("CAL_FILECHECK")) {
		fprintf(stderr, "cal-cli: %d file%s open %s\n", pf_open_files, pf_open_files == 1 ? "" : "s", where);
		exit(3);
	}
}

static void idle_check(void)
{
	file_check("across an event call (pf_idle)");
}

/* every field of every event and record, as text (snaptest) */
static void dump_rule(FILE *f, const RRULE *r)
{
	int i;
	fprintf(f, " rule %d %d %d %ld %ld %d %d %d %lu %lu %lu %lu %lu %u %u", r->freq, r->wkst, r->interval, r->count,
		r->until, r->until_kind, r->rscale, r->skip, r->byhour, r->byminute[0], r->byminute[1], r->bysecond[0],
		r->bysecond[1], r->bymonth, r->bymonth_leap);
	for (i = 0; i < r->nbyday; i++)
		fprintf(f, " d%d", r->byday[i]);
	for (i = 0; i < r->nbymd; i++)
		fprintf(f, " m%d", r->bymd[i]);
	for (i = 0; i < r->nbyyd; i++)
		fprintf(f, " y%d", r->byyd[i]);
	for (i = 0; i < r->nbywn; i++)
		fprintf(f, " w%d", r->bywn[i]);
	for (i = 0; i < r->nbysetpos; i++)
		fprintf(f, " p%d", r->bysetpos[i]);
}

static char *dump_all(void)
{
	char *out = 0;
	size_t n = 0;
	FILE *f = open_memstream(&out, &n);
	int ci, i, k;
	for (ci = 0; ci < ncals; ci++) {
		CALENDAR *c = &cals[ci];
		fprintf(f, "calendar %d %s: %d events %d records lost %d\n", c->id, c->name, c->nev, c->nres, c->lost);
		for (i = 0; i < c->nres; i++)
			fprintf(f, " res %d %s %s %ld %ld %d %ld\n", i, c->res[i].href ? c->res[i].href : "-",
				c->res[i].etag ? c->res[i].etag : "-", c->res[i].off, c->res[i].len, c->res[i].dead,
				c->res[i].beyond);
		for (i = 0; i < c->nev; i++) {
			EVENT *e = c->ev[i];
			fprintf(f, " ev %d res %d [%s] [%s] [%s] [%s] [%s] [%s] [%s] tz %s %ld %ld %ld", i, e->res,
				e->uid ? e->uid : "-", e->summary ? e->summary : "-", e->location ? e->location : "-",
				e->notes ? e->notes : "-", e->url ? e->url : "-", e->attendees ? e->attendees : "-",
				e->organizer ? e->organizer : "-", e->tz ? e->tz->name : "-", e->start, e->end, e->recurid);
			if (e->tz)
				fprintf(f, " (%d %d %d/%d/%d/%d/%d %d/%d/%d/%d/%d)", e->tz->std, e->tz->dst, e->tz->on.mon,
					e->tz->on.wday, e->tz->on.day, e->tz->on.utc, e->tz->on.min, e->tz->off.mon,
					e->tz->off.wday, e->tz->off.day, e->tz->off.utc, e->tz->off.min);
			if (e->rr)
				dump_rule(f, e->rr);
			for (k = 0; k < e->nexdate; k++)
				fprintf(f, " x%ld", e->exdate[k]);
			for (k = 0; k < e->nrdate; k++)
				fprintf(f, " r%ld/%ld", e->rdate[k], e->rdate_end ? e->rdate_end[k] : -1L);
			for (k = 0; k < e->nexrule; k++)
				dump_rule(f, &e->exrule[k]);
			fprintf(f, " %d %d %d %d %d %d %d %d %d %d %d %d\n", e->range_future, e->alarm, e->allday,
				e->floating, e->status, e->hasexc, e->gone, e->seq, e->att_edited, e->notes_later,
				e->cal, e->res);
		}
	}
	fclose(f);
	return out;
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
	pf_idle = idle_check;
	if (getenv("CAL_LOG"))
		str_copy(conn_logfile, getenv("CAL_LOG"), sizeof(conn_logfile));
	if (getenv("CACERT")) {
		str_copy(conn_cacert, getenv("CACERT"), sizeof(conn_cacert));
		path_join(tls_roots_cache, sizeof(tls_roots_cache), dir, "ROOTS.DAT");
	}
	path_join(tls_seed_path, sizeof(tls_seed_path), dir, "SEED.DAT");
	tls_read_at = pf_read_at;	/* as the accessory: CAL_FILECHECK holds in Falcon mode too */
	tls_file_info = pf_info;
	pf_size_open = pf_handle_size;
	pf_mkdir(dir);
	store_init(dir, 0);
	tls_tz_minutes = tz_offset(home_zone(), now_utc());
	{
		/* each calendar read in steps, as the accessory reads them */
		int i;
		for (i = 0; i < ncals; i++) {
			cal_load_begin(&cals[i]);
			while (cal_load_step())
				file_check("between a calendar's reading steps");
		}
	}

	if (!strcmp(cmd, "snaptest")) {
		/* the snapshot gives what the text gave: each calendar read from
		   its text, its snapshot written, then read again from that */
		/* snaptest DAYS: the snapshots written with a window of DAYS ahead,
		   read with the settings' own: what the window now reaches read
		   again from the text */
		char *a, *b;
		int i, ndat = 0, reads, same, days = argc > 3 ? atoi(argv[3]) : 0, keep = opt.future_days;
		if (days)
			opt.future_days = days;
		for (i = 0; i < ncals; i++) {
			cal_snap_forget(&cals[i]);
			cal_load(&cals[i]);
			ndat += accts[cals[i].acct].kind != AK_SUBSCR;
		}
		cal_snap_flush();
		opt.future_days = keep;
		if (days) {
			cal_snap_off = 1;	/* what the text gives with the whole window */
			for (i = 0; i < ncals; i++)
				cal_load(&cals[i]);
			cal_snap_off = 0;
		}
		a = dump_all();
		reads = cal_snap_reads;
		for (i = 0; i < ncals; i++)
			cal_load(&cals[i]);
		b = dump_all();
		reads = cal_snap_reads - reads;
		same = !strcmp(a, b);
		if (!same) {
			char p[300];
			FILE *f;
			snprintf(p, sizeof(p), "%s/snap_text.txt", dir);
			if ((f = fopen(p, "w")))
				fputs(a, f), fclose(f);
			snprintf(p, sizeof(p), "%s/snap_snap.txt", dir);
			if ((f = fopen(p, "w")))
				fputs(b, f), fclose(f);
		}
		{
			/* a cache file changed since its snapshot (an event added
			   by another program): its text read, the same as without */
			char *c1, *c2;
			int stale;
			for (i = 0; i < ncals; i++)
				if (accts[cals[i].acct].kind != AK_SUBSCR) {
					char p[300];
					FILE *f;
					snprintf(p, sizeof(p), "%s/CACHE/CAL%05d.DAT", dir, cals[i].id);
					if ((f = fopen(p, "ab")))
						fputs("X nothing-here\n", f), fclose(f);	/* a deletion of nothing */
				}
			cal_snap_off = 1;
			for (i = 0; i < ncals; i++)
				cal_load(&cals[i]);
			c1 = dump_all();
			cal_snap_off = 0;
			stale = cal_snap_reads;
			for (i = 0; i < ncals; i++)
				cal_load(&cals[i]);
			c2 = dump_all();
			stale = cal_snap_reads - stale;
			if (strcmp(c1, c2) || stale) {
				printf("snaptest: with the cache files changed, %d snapshots read, %s\n", stale,
				       strcmp(c1, c2) ? "DIFFERENT" : "the same");
				same = 0;
			}
			free(c1);
			free(c2);
			cal_snap_flush();	/* (left as a start would leave them) */
		}
		printf("snaptest: %d calendars, %d read from their snapshots, %ld bytes of events: %s\n", ndat, reads,
		       (long)strlen(a), same && reads == ndat ? "the same" : "DIFFERENT");
		free(a);
		free(b);
		return same && reads == ndat ? 0 : 1;
	}
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
		int r = sync_all(argc < 4 ? SYNC_CHANGED : !strcmp(argv[3], "again") ? SYNC_AGAIN : SYNC_ALL);
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
				while (steps != stop && sync_step()) {
					file_check("between the refresh's steps");
					steps++;
				}
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
	if (!strcmp(cmd, "sliced") && argc == 5) {
		/* as list, collected in the smallest steps a view drawn in
		   slices takes (each 16 events, each merge of the sort) */
		OCCLIST l;
		int i, steps = 0;
		memset(&l, 0, sizeof(l));
		occ_collect_begin(&l, parse_view(argv[3]), parse_view(argv[4]));
		while (occ_collect_step(&l, 0))
			steps++;
		for (i = 0; i < l.n; i++)
			print_occ(&l.o[i]);
		fprintf(stderr, "%d steps\n", steps);
		occ_free(&l);
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
	if (!strcmp(cmd, "notes") && argc == 4) {
		/* as the day's details show them: read from the cache */
		EVENT *e = find(argv[3], CT_NONE, 0);
		const char *n;
		if (!e)
			return fail("no such event");
		n = ev_notes_text(e);
		printf("kept %s\nnotes %s\n", e->notes ? "yes" : "no", n ? n : "");
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
	if (!strcmp(cmd, "read-ics") && argc == 4) {
		/* what the New Event dialog gets from an invitation */
		EVENT *e;
		char method[16], t[24];
		int n = cal_read_ics(argv[3], &e, method, sizeof(method));
		if (n < 0)
			return fail("read-ics");
		printf("events %d\nmethod %s\n", n, method[0] ? method : "-");
		if (e) {
			printf("uid %s\nsummary %s\n", e->uid ? e->uid : "", e->summary ? e->summary : "");
			ct_format_ics(e->start, e->allday ? 1 : 2, t, sizeof(t));
			printf("start %s\n", t);
			ct_format_ics(e->end, e->allday ? 1 : 2, t, sizeof(t));
			printf("end %s\n", t);
			printf("zone %s\nallday %d\nlocation %s\nnotes %s\norganizer %s\nattendees %s\nalarm %d\nrule %s\n",
			       e->tz ? e->tz->name : "-", e->allday, e->location ? e->location : "",
			       e->notes ? e->notes : "", e->organizer ? e->organizer : "",
			       e->attendees ? e->attendees : "", e->alarm, e->rr ? "yes" : "no");
			ev_free(e);
		}
		return 0;
	}
	if (!strcmp(cmd, "accept-ics") && argc == 5) {
		/* the New Event dialog's OK on an invitation, unchanged: a new
		   event with its UID, or the one it updates */
		EVENT *e;
		char method[16];
		int ci = atoi(argv[3]), oc;
		EVENT *old;
		if (cal_read_ics(argv[4], &e, method, sizeof(method)) != 1 || !e)
			return fail("accept-ics: not one event");
		if ((old = cal_find_uid(e->uid, &oc)) != 0) {
			ci = oc;
			e->res = old->res;
			e->cal = (short)oc;
		}
		if (!ev_save(e, ci, SAVE_ALL, CT_NONE))
			return fail("accept-ics");
		printf("%s %s\n", old ? "updated" : "saved", e->uid);
		ev_free(e);
		cal_invite_done();
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
