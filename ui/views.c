/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * views.c - the day, week, month and year views.
 *
 * Everything is laid out first (layout(): where each event box goes,
 * kept in items[] for drawing and for clicks), then drawn, clipped to
 * whatever part of the window needs it. Times are wall-clock times in
 * the view's zone (calstore.c's occurrences already are).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "util.h"
#include "clip.h"
#include "../src/plat.h"
#ifdef AES_PROFILE
#include <tos.h>
#endif

long cur_day;
short cur_view = V_MONTH;
OCC sel_occ;
/* the day and week grids are scrolled by scroll_y pixels (hour_h a
   row); want_hour, when set, is where the next layout puts them */
static long scroll_y;
static short scroll_hh, want_hour = 8;

static OCCLIST occ;
static int occ_valid;
static int occ_partial;		/* a collect begun in slices (views_prepare), not finished */
static int busy_known;
static void yg_forget(void);
static int laid_out;

/* what was laid out: event boxes and "+3 more" lines */
enum { IT_EVENT, IT_MORE };
typedef struct {
	GRECT r;
	short kind;
	short occ;		/* index in occ */
	long day;		/* IT_MORE: the day it stands for */
	short cont;		/* a bar continued from the row before */
	short timed;		/* a box in the day/week time grid (not an all-day bar) */
} ITEM;
#define MAXITEMS 400
static ITEM items[MAXITEMS];
static int nitems;

/* geometry of the current layout */
static long first_day, ndays;	/* the days shown */
static short rows, cols;	/* month: weeks x 7; week: 1 x 7; day: 1 x 1 */
static GRECT grid;		/* month cells / day-week time grid */
static short head_h, allday_h, gutter, hour_h, cell_w, cell_h;
/* the columns' left edges (colx[cols]: the grid's right edge), each on a
   byte so that the text set from them is on one (the guidelines, Speed);
   they differ in width by 8 pixels at most. cell_w: the narrowest */
static short colx[8];

static void columns(void)
{
	short c;
	for (c = 0; c < cols && c < 7; c++)
		colx[c] = grid.x + (short)(((long)c * grid.w / cols) & ~7L);
	colx[c] = grid.x + grid.w;
	cell_w = 32767;
	for (c = 0; c < cols && c < 7; c++)
		if (colx[c + 1] - colx[c] < cell_w)
			cell_w = colx[c + 1] - colx[c];
}

/* the column under x, -1 for none */
static short col_at(short x)
{
	short c;
	for (c = 0; c < cols && c < 7; c++)
		if (x >= colx[c] && x < colx[c + 1])
			return c;
	return -1;
}

#define COLW(c) (colx[(c) + 1] - colx[c])
static GRECT detail;		/* day view: the selected event's details */
static long glide_left;		/* a key's hour still to glide, pixels, up when negative (views_glide) */

/* today, worked out once per drawing (see views_draw) */
static long today_cache = -1;

static long today(void)
{
	return today_cache >= 0 ? today_cache : ct_days(now_view());
}

static long week_start(long d)
{
	return d - (wday_of(d) - opt.weekstart + 7) % 7;
}

void views_scroll_to(short hour)
{
	want_hour = hour;
	laid_out = 0;		/* the event boxes were placed for the old hour */
}

void views_invalidate(void)
{
	occ_valid = 0;
	occ_partial = 0;	/* a collect under way begins again */
	busy_known = 0;
	laid_out = 0;
	yg_forget();
}

void views_select_none(void)
{
	memset(&sel_occ, 0, sizeof(sel_occ));
}

static long scroll_max(void)
{
	long m = 24L * hour_h - grid.h;
	return m < 0 ? 0 : m;
}

/* ---------------- the range and its occurrences ---------------- */

static void range(long *from, long *n)
{
	int y, m, d;
	switch (cur_view) {
	case V_DAY:
		*from = cur_day;
		*n = 1;
		break;
	case V_WEEK:
		*from = week_start(cur_day);
		*n = 7;
		break;
	case V_MONTH: {
		long first;
		civil_from_days(cur_day, &y, &m, &d);
		first = days_from_civil(y, m, 1);
		*from = week_start(first);
		*n = ((first - *from) + days_in_month(y, m) + 6) / 7 * 7;
		break;
	}
	default:
		civil_from_days(cur_day, &y, &m, &d);
		*from = days_from_civil(y, 1, 1);
		*n = days_from_civil(y + 1, 1, 1) - *from;
	}
}

static void oday_fill(void);

/* the range's occurrences: 1 when they are there, 0 when a collect is
   (re)begun or goes on, for ms (0: to the end) */
static int collect_for(unsigned long ms)
{
	long from, n;
	CTIME a, b;
	range(&from, &n);
	a = from * MIN_PER_DAY;
	b = (from + n) * MIN_PER_DAY;
	if (occ_valid && occ.from == a && occ.to == b)
		return 1;
	if (!occ_partial || occ.from != a || occ.to != b) {
		occ_valid = 0;
		occ_collect_begin(&occ, a, b);
		occ_partial = 1;
	}
	if (occ_collect_step(&occ, ms ? ms : 0xffffffffUL))
		return 0;
	occ_partial = 0;
	oday_fill();
	busy_known = 0;
	occ_valid = 1;
	laid_out = 0;
	/* keep the selection if its occurrence is still there */
	occ_find_again();
	return 1;
}

static void collect(void)
{
	collect_for(0);
}

int occ_find_again(void)
{
	int i;
	if (!sel_occ.ev)
		return 0;
	for (i = 0; i < occ.n; i++)
		if (occ.o[i].ev == sel_occ.ev && occ.o[i].start == sel_occ.start)
			return 1;
	views_select_none();
	return 0;
}

static int long_of(const OCC *o)
{
	return o->ev->allday || o->end - o->start >= MIN_PER_DAY ||
	       ct_days(o->start) != ct_days(o->end - 1);
}

/* each occurrence's first and last day, and whether it is long, worked
   out once a collect: the layouts ask for them for every row, day and
   column, and ct_days() is a division, in software on the 68000 (the
   month view's layout took 1.3 s on a 16 MHz Falcon; the guidelines,
   Speed) */
typedef struct {
	long first, last;
	char lng;
} ODAY;
static ODAY *oday;
static int oday_n;		/* occurrences it holds, 0: work them out each time */

static void oday_fill(void)
{
	static int cap;
	int i;
	oday_n = 0;
	if (occ.n > cap) {
		ODAY *n = realloc(oday, (occ.n + 32) * sizeof(ODAY));
		if (!n)
			return;
		oday = n;
		cap = occ.n + 32;
	}
	for (i = 0; i < occ.n; i++) {
		const OCC *o = &occ.o[i];
		oday[i].first = ct_days(o->start);	/* (one divu.w: common's ctime.c) */
		oday[i].last = o->end > o->start ? ct_days(o->end - 1) : oday[i].first;
		oday[i].lng = (char)(o->ev->allday || o->end - o->start >= MIN_PER_DAY ||
				     oday[i].first != oday[i].last);
	}
	oday_n = occ.n;
}

static const ODAY *oday_of(const OCC *o)
{
	return o >= occ.o && o < occ.o + oday_n ? &oday[o - occ.o] : 0;
}

static int is_long(const OCC *o)
{
	const ODAY *d = oday_of(o);
	return d ? d->lng : long_of(o);
}

/* a bar in the all-day row: in the month view every long event, in the
   day and week views only all-day ones, as the timed ones run down the
   grid from their start to their end, over midnight into the next days */
static int in_lane(const OCC *o)
{
	return cur_view == V_MONTH ? is_long(o) : o->ev->allday;
}

static long occ_first_day(const OCC *o)
{
	const ODAY *d = oday_of(o);
	return d ? d->first : ct_days(o->start);
}

/* the same for occ.o[i], read in line where the table has it */
#define OCC_FIRST(i, o) (oday_n == occ.n ? oday[i].first : occ_first_day(o))
#define OCC_LAST(i, o) (oday_n == occ.n ? oday[i].last : occ_last_day(o))

static long occ_last_day(const OCC *o)
{
	const ODAY *d = oday_of(o);
	return d ? d->last : ct_days(o->end > o->start ? o->end - 1 : o->start);
}

static int add_item(GRECT *r, short kind, short oi, long day, short cont)
{
	if (nitems >= MAXITEMS)
		return 0;
	items[nitems].r = *r;
	items[nitems].kind = kind;
	items[nitems].occ = oi;
	items[nitems].day = day;
	items[nitems].cont = cont;
	items[nitems].timed = 0;
	nitems++;
	return 1;
}

/* lanes for the long events of days [d0, d0+n): each event gets the
   lowest lane free on all its days; lane[] per occurrence index */
static short assign_lanes(long d0, long n, short *lane, short maxl)
{
	static unsigned long used[7 * 6];	/* bit l: lane l taken on that day */
	short top = 0;
	int i;
	long d;
	const ODAY *od = oday_n == occ.n ? oday : 0;	/* (read in line: called a row at a time) */
	memset(used, 0, sizeof(used));
	for (i = 0; i < occ.n; i++, od = od ? od + 1 : 0) {
		OCC *o = &occ.o[i];
		long a, b;
		short l;
		lane[i] = -1;
		if (od) {
			if (cur_view == V_MONTH ? !od->lng : !o->ev->allday)
				continue;
			a = od->first - d0;
			b = od->last - d0;
		} else {
			if (!in_lane(o))
				continue;
			a = occ_first_day(o) - d0;
			b = occ_last_day(o) - d0;
		}
		if (b < 0 || a >= n)
			continue;
		if (a < 0)
			a = 0;
		if (b >= n)
			b = n - 1;
		for (l = 0; l < maxl && l < 32; l++) {
			for (d = a; d <= b; d++)
				if (used[d] & (1UL << l))
					break;
			if (d > b)
				break;
		}
		if (l >= maxl || l >= 32)
			continue;
		for (d = a; d <= b; d++)
			used[d] |= 1UL << l;
		lane[i] = l;
		if (l + 1 > top)
			top = l + 1;
	}
	return top;
}

/* ---------------- layout ---------------- */

/* a lane per occurrence; after it, room for as many more (the month's
   lists of each day's events) */
static int lane_cap;

static short *lane_buf(void)
{
	static short *buf;
	if (!buf || occ.n > lane_cap) {	/* a week with no events too */
		free(buf);
		lane_cap = occ.n + 32;
		buf = malloc(2 * lane_cap * sizeof(short));
	}
	return buf;
}

static short day_head[42], day_tail[42], day_n[42];	/* the month's days' timed events */

static void layout_month(void)
{
	short r, c, line_h = ch + 1;
	short *lane = lane_buf();
	head_h = ch + 4;
	rows = (short)(ndays / 7);
	cols = 7;
	grid.x = (r_view.x + 7) & ~7;	/* on a byte (the columns too: columns()) */
	grid.y = r_view.y + head_h;
	grid.w = r_view.x + r_view.w - grid.x;
	grid.h = r_view.h - head_h;
	columns();
	cell_h = grid.h / rows;
	if (!lane)
		return;
	{
		/* each day's timed events in a list, in the list's order (by
		   start), made in one pass: not looked for in every cell, which
		   took most of a second on an ST (the guidelines, Speed) */
		short *next = lane + lane_cap;
		int i;
		for (c = 0; c < 42; c++)
			day_head[c] = day_n[c] = 0, day_tail[c] = -1;
		for (i = 0; i < occ.n; i++) {
			long d = occ_first_day(&occ.o[i]) - first_day;
			if (is_long(&occ.o[i]) || d < 0 || d >= 42 || d >= ndays)
				continue;
			next[i] = -1;
			if (day_tail[d] < 0)
				day_head[d] = (short)i;
			else
				next[day_tail[d]] = (short)i;
			day_tail[d] = (short)i;
			day_n[d]++;
		}
	}
	for (r = 0; r < rows; r++) {
		long d0 = first_day + r * 7;
		short maxl = (cell_h - ch - 3) / line_h, used = 0;
		int i;
		short count[7];
		if (maxl < 1)
			maxl = 1;
		used = assign_lanes(d0, 7, lane, maxl);
		memset(count, 0, sizeof(count));
		/* long events: bars across the row (their days read in line: a
		   row at a time, over every occurrence) */
		for (i = 0; i < occ.n; i++) {
			OCC *o;
			long a, b, f;
			GRECT box;
			if (lane[i] < 0)
				continue;
			o = &occ.o[i];
			f = OCC_FIRST(i, o);
			a = f - d0;
			b = OCC_LAST(i, o) - d0;
			if (a < 0)
				a = 0;
			if (b > 6)
				b = 6;
			box.x = colx[a] + 1;
			box.y = grid.y + r * cell_h + ch + 3 + lane[i] * line_h;
			box.w = colx[b + 1] - colx[a] - 2;
			box.h = line_h - 1;
			add_item(&box, IT_EVENT, (short)i, d0 + a, f < d0 + a);
		}
		(void)used;
		/* lanes taken by bars on each day: the timed events go below */
		for (i = 0; i < occ.n; i++) {
			long a, b;
			if (lane[i] < 0)
				continue;
			a = OCC_FIRST(i, &occ.o[i]) - d0;
			b = OCC_LAST(i, &occ.o[i]) - d0;
			if (a < 0)
				a = 0;
			for (; a <= b && a < 7; a++)
				if (lane[i] + 1 > count[a])
					count[a] = lane[i] + 1;
		}
		/* timed events of each day, one line each */
		for (c = 0; c < 7; c++) {
			long day = d0 + c;
			short di = (short)(r * 7 + c), n = 0, total = di < 42 ? day_n[di] : 0, room = maxl - count[c];
			for (i = total ? day_head[di] : -1; i >= 0 && room > 0; i = lane[lane_cap + i]) {
				GRECT box;
				box.x = colx[c] + 1;
				box.y = grid.y + r * cell_h + ch + 3 + (count[c] + n) * line_h;
				box.w = COLW(c) - 2;
				box.h = line_h - 1;
				if (n == room - 1 && total > room) {
					add_item(&box, IT_MORE, (short)(total - n), day, 0);
					break;
				}
				add_item(&box, IT_EVENT, (short)i, day, 0);
				n++;
			}
		}
	}
}

/* timed events of one day column, side by side where they overlap; one
   that goes on over midnight has a part in each day's column */
static void layout_column(long day, GRECT *col)
{
	short idx[80], colof[80], ncol[80], n = 0, i, j;
	CTIME d0 = day * MIN_PER_DAY;
	for (i = 0; i < occ.n && n < 80; i++) {
		OCC *o = &occ.o[i];
		if (o->ev->allday)
			continue;
		if (occ_first_day(o) != day && !(o->start < d0 && o->end > d0))
			continue;
		idx[n++] = i;
	}
	/* by start: the list has the events of a day or more first */
	for (i = 1; i < n; i++) {
		short k = idx[i];
		for (j = i; j > 0 && occ.o[idx[j - 1]].start > occ.o[k].start; j--)
			idx[j] = idx[j - 1];
		idx[j] = k;
	}
	/* greedy columns within clusters of overlapping events */
	i = 0;
	while (i < n) {
		short cstart = i, maxc = 0;
		CTIME cend = occ.o[idx[i]].end;
		CTIME colend[8];
		for (j = 0; j < 8; j++)
			colend[j] = -0x7fffffffL;
		for (; i < n && (i == cstart || occ.o[idx[i]].start < cend); i++) {
			OCC *o = &occ.o[idx[i]];
			CTIME e = o->end > o->start + 15 ? o->end : o->start + 15;
			short k;
			for (k = 0; k < 8 && colend[k] > o->start; k++)
				;
			if (k == 8)
				k = 7;
			colend[k] = e;
			colof[i] = k;
			if (k + 1 > maxc)
				maxc = k + 1;
			if (e > cend)
				cend = e;
		}
		for (j = cstart; j < i; j++)
			ncol[j] = maxc;
	}
	for (i = 0; i < n; i++) {
		OCC *o = &occ.o[idx[i]];
		GRECT box;
		long s = o->start - d0, e = o->end - d0;
		short w = (col->w - 3) / ncol[i], cont = s < 0;
		if (s < 0)
			s = 0;		/* from the day before */
		if (e - s < 20)
			e = s + 20;	/* short ones stay readable */
		if (e > 1440)
			e = 1440;
		box.x = col->x + 1 + colof[i] * w;
		box.w = w - 1;
		box.y = col->y + (short)(s * hour_h / 60 - scroll_y);
		box.h = (short)((e - s) * hour_h / 60) - 1;
		if (box.h < ch / 2)
			box.h = ch / 2;
		if (add_item(&box, IT_EVENT, idx[i], day, cont))
			items[nitems - 1].timed = 1;
	}
}

static void layout_days(void)
{
	short c, line_h = ch + 2, lanes, maxl = 3;
	short *lane = lane_buf();
	head_h = (opt.hebdates ? 2 : 1) * ch + 6;
	gutter = 7 * cw;
	hour_h = ch >= 16 ? 2 * ch + 4 : 2 * ch + 4;
	cols = (short)ndays;
	rows = 1;
	detail.w = 0;
	grid.x = (r_view.x + gutter + 7) & ~7;	/* on a byte (the columns too: columns()) */
	grid.w = r_view.x + r_view.w - grid.x;
	if (cur_view == V_DAY && r_view.w > 70 * cw) {
		/* the day view shows the selected event's details on the right */
		detail.w = r_view.w * 2 / 5;
		detail.x = r_view.x + r_view.w - detail.w;
		detail.y = r_view.y;
		detail.h = r_view.h;
		grid.w -= detail.w;
	}
	columns();
	lanes = lane ? assign_lanes(first_day, ndays, lane, maxl) : 0;
	allday_h = (lanes ? lanes : 1) * line_h + 4;
	grid.y = r_view.y + head_h + allday_h;
	grid.h = r_view.h - head_h - allday_h;
	if (want_hour >= 0)
		scroll_y = (long)want_hour * hour_h;
	else if (scroll_hh && scroll_hh != hour_h)
		scroll_y = scroll_y * hour_h / scroll_hh;	/* another font */
	want_hour = -1;
	scroll_hh = hour_h;
	if (scroll_y > scroll_max())
		scroll_y = scroll_max();
	if (scroll_y < 0)
		scroll_y = 0;
	if (lane) {
		int i;
		for (i = 0; i < occ.n; i++) {
			OCC *o = &occ.o[i];
			long a, b;
			GRECT box;
			if (lane[i] < 0)
				continue;
			a = occ_first_day(o) - first_day;
			b = occ_last_day(o) - first_day;
			if (a < 0)
				a = 0;
			if (b >= ndays)
				b = ndays - 1;
			box.x = colx[a] + 1;
			box.y = r_view.y + head_h + 2 + lane[i] * line_h;
			box.w = colx[b + 1] - colx[a] - 2;
			box.h = line_h - 1;
			add_item(&box, IT_EVENT, (short)i, first_day + a, occ_first_day(o) < first_day + a);
		}
	}
	for (c = 0; c < cols; c++) {
		GRECT col;
		col.x = colx[c];
		col.y = grid.y;
		col.w = COLW(c);
		col.h = grid.h;
		layout_column(first_day + c, &col);
	}
}

static void layout(void)
{
	collect();
	if (laid_out)
		return;
	yg_forget();
	nitems = 0;
	range(&first_day, &ndays);
	allday_h = head_h = 0;
	if (cur_view == V_MONTH)
		layout_month();
	else if (cur_view == V_WEEK || cur_view == V_DAY)
		layout_days();
	laid_out = 1;
}

/* the view made ready to draw a slice at a time (win.c's view job): the
   occurrences collected for ms at most, then laid out. 1 while there is
   more */
const char *views_phase = "";	/* what views_prepare did last (the profiled build says) */

int views_prepare(unsigned long ms)
{
	long from, n;
	range(&from, &n);
	if (!occ_valid || occ.from != from * MIN_PER_DAY || occ.to != (from + n) * MIN_PER_DAY) {
		views_phase = "collect";
		collect_for(ms);
		return 1;	/* (laid out in a slice of its own) */
	}
	if (!laid_out) {
		views_phase = "layout";
		layout();
		return 1;	/* (drawing begins in the next slice) */
	}
	return 0;
}

/* ---------------- drawing ---------------- */

/* the Hebrew date of each day drawn: the next day's from the last one's
   within a month (the full conversion is a dozen divisions in software
   on a 68000: 80 ms of a month view on an ST) */
static void heb_of(long day, HDATE *h)
{
	static long hday = -0x7fffffffL;
	static HDATE hh;
	static short hdim;
	if (day == hday + 1 && hh.day < hdim) {
		hh.day++;
	} else if (day != hday) {
		hebrew_from_days(day, &hh);
		hdim = (short)hebrew_month_days(hh.year, hh.mon);
	}
	hday = day;
	h->year = hh.year;	/* (field by field: a structure assigned is a memcpy call) */
	h->mon = hh.mon;
	h->day = hh.day;
}


/* the loop and the bell at the right end of the line at y; the columns
   they take from that line's text */
static short event_marks(EVENT *e, const GRECT *r, short y, short color, GRECT *clip, short keep)
{
	short w = mark_w(), x, n = 0, my, cols = (r->x + r->w - 2 - text_x(r->x + 4)) / cw;	/* event_box's */
	int bell = e->alarm != -1 && opt.alarms && cals[e->cal].alarms;
	GRECT c = *clip;
	if (!rect_and(&c, r))
		return 0;
	n = (short)(bell + (e->rr != 0));
	if (w > r->h - 2 || cols - (n * (w + 2) + cw - 1) / cw < keep)
		w = 8;			/* a short or narrow box: the small ones */
	if (cols - (n * (w + 2) + cw - 1) / cw < keep)
		return 0;		/* the text first: no room for them */
	n = 0;
	x = r->x + r->w - 3 - w;
	my = y + (ch - w) / 2;		/* in the middle of the line, inside the box */
	if (my + w > r->y + r->h - 1)
		my = r->y + r->h - 1 - w;
	if (my < r->y + 1)
		my = r->y + 1;
	if (bell) {
		draw_mark(x, my, w, MARK_BELL, color, &c);
		x -= w + 2;
		n++;
	}
	if (e->rr) {
		draw_mark(x, my, w, MARK_LOOP, color, &c);
		n++;
	}
	return n ? (short)((n * (w + 2) + cw - 1) / cw) : 0;
}

static int has_marks(EVENT *e)
{
	return e->rr || (e->alarm != -1 && opt.alarms && cals[e->cal].alarms);
}

static void event_box(const ITEM *it, GRECT *clip, int bar)
{
	OCC *o = &occ.o[it->occ];
	EVENT *e = o->ev;
	PEN p;
	GRECT r = it->r, strip;
	int sel = sel_occ.ev == e && sel_occ.start == o->start;
	short tx = text_x(r.x + 4), ty = r.y, cols_, fl = 0;	/* (text on a byte, draw.c) */
	char line[160], tm[16];
	const char *title = e->summary ? e->summary : "New Event";
	pen_of(e->cal, &p);
	clip_on(clip);
	if (sel) {
		fill(&r, p.color == 6 || p.color == 14 || p.color == 8 ? 1 : p.color);
		fl = TX_INVERSE;
	} else if (bar && is_long(o)) {
		/* all-day and multi-day bars keep their tint, as on the Mac */
		fill(&r, 0);
		fill_pat(&r, p.color, p.interior, p.style);
	} else {
		/* an event: white for easy reading, a thin border in its
		   calendar's colour, and the thick strip on the left */
		fill(&r, 0);
		frame(&r, p.color);
	}
	strip = r;
	strip.w = 3;
	if (!it->cont)
		fill(&strip, sel ? 0 : p.color);
	if (!sel && (e->status == ST_TENTATIVE || e->status == ST_CANCELLED))
		fl |= TX_GREY;
	cols_ = (r.x + r.w - 2 - tx) / cw;
	if (cols_ <= 0)
		return;
	if (bar) {
		/* one line: (time) title, the marks at its end */
		if (has_marks(e))
			cols_ -= event_marks(e, &r, ty, sel ? 0 : 1, clip, 5);
		if (!is_long(o) && cur_view == V_MONTH && cols_ >= 11) {
			DT d;
			ct_split(o->start, &d);
			fmt_time(d.hour, d.min, opt.h24, tm, sizeof(tm));
			snprintf(line, sizeof(line), "%s %s", tm, title);
			text_at(tx, ty, line, (long)strlen(line), cols_, fl | TX_LTR);
		} else {
			text_at(tx, ty, title, (long)strlen(title), cols_, fl | TX_RIGHT);
		}
		return;
	}
	/* a box in the day/week grid: title, time, place as room allows */
	{
		DT a, b;
		short lines = (r.h - 1) / ch, k = 0;
		ct_split(o->start, &a);
		ct_split(o->end, &b);
		if (lines < 1)
			lines = 1;
		/* the marks on the time's line, or the title's when there's one line */
		{
			/* keep room for the start time at least, or the title's start */
			short mc = has_marks(e) ? event_marks(e, &r, ty + (lines > 1 ? ch : 0), sel ? 0 : 1, clip,
							       lines > 1 ? (short)(opt.h24 ? 5 : 7) : 4) : 0;
			text_at(tx, ty, title, (long)strlen(title), lines > 1 ? cols_ : cols_ - mc, fl | TX_RIGHT);
			k++;
			if (k < lines) {
				char t2[16];
				fmt_time(a.hour, a.min, opt.h24, tm, sizeof(tm));
				fmt_time(b.hour, b.min, opt.h24, t2, sizeof(t2));
				snprintf(line, sizeof(line), "%s-%s", tm, t2);
				if ((short)strlen(line) > cols_ - mc)
					str_copy(line, tm, sizeof(line));	/* narrow: when it starts */
				text_at(tx, ty + k * ch, line, (long)strlen(line), cols_ - mc, fl | TX_LTR);
				k++;
			}
		}
		if (k < lines && e->location) {
			text_at(tx, ty + k * ch, e->location, (long)strlen(e->location), cols_, fl | TX_RIGHT);
			k++;
		}
	}
}

static void draw_items(GRECT *clip, int bars)
{
	int i;
	short cx0 = clip->x, cy0 = clip->y, cx1 = clip->x + clip->w, cy1 = clip->y + clip->h;
	for (i = 0; i < nitems; i++) {
		const GRECT *ir = &items[i].r;
		/* (outside the clip: told in line, a call each for every item of
		   every piece drawn was a sixth of a view's time on an ST) */
		if (ir->y >= cy1 || ir->y + ir->h <= cy0 || ir->x >= cx1 || ir->x + ir->w <= cx0)
			continue;
		if (items[i].kind == IT_MORE) {
			char t[20];
			GRECT c = *clip;
			rect_and(&c, &items[i].r);
			clip_on(&c);
			snprintf(t, sizeof(t), "+%d more", items[i].occ);
			text_at(items[i].r.x + 4, items[i].r.y, t, (long)strlen(t), items[i].r.w / cw, TX_LTR | TX_GREY);
			continue;
		}
		{
			GRECT c = *clip;
			if (!bars && cur_view != V_MONTH) {
				/* grid boxes stay inside the time grid */
				GRECT g = grid;
				/* scrolled above or below: cut at the grid's edges */
				if (items[i].timed && !rect_and(&c, &g))
					continue;
			}
			event_box(&items[i], &c, !items[i].timed);
		}
	}
	(void)bars;
}

static void draw_month(GRECT *clip)
{
	short r, c;
	int y0, m0, d0;
	civil_from_days(cur_day, &y0, &m0, &d0);
	clip_on(clip);
	/* weekday names (not for a box redrawn below them: each text costs
	   an ST milliseconds, clipped or not) */
	for (c = 0; c < 7 && clip->y < grid.y; c++) {
		const char *wn = COLW(c) >= 10 * cw ? wday_names[(c + opt.weekstart) % 7] : wday_short[(c + opt.weekstart) % 7];
		short wd = (c + opt.weekstart) % 7;
		text_color(wd == 6 || (opt.weekstart == 0 && wd == 5 && 0) ? 1 : 1);
		text_at(colx[c] + 4, r_view.y + 2, wn, (long)strlen(wn), COLW(c) / cw - 1,
			TX_LTR | (wd == 6 ? TX_BOLD : 0));
	}
	line_col(r_view.x, grid.y - 1, r_view.x + r_view.w - 1, grid.y - 1, 1);
	for (r = 0; r < rows; r++)
		for (c = 0; c < 7; c++) {
			long day = first_day + r * 7 + c;
			int yy, m, d;
			GRECT cell;
			cell.x = colx[c];
			cell.y = grid.y + r * cell_h;
			cell.w = COLW(c);
			cell.h = r == rows - 1 ? grid.y + grid.h - cell.y : cell_h;
			{
				GRECT cc = cell;
				if (!rect_and(&cc, clip))
					continue;
			}
			civil_from_days(day, &yy, &m, &d);
			if (m != m0 && ncolors >= 16) {
				GRECT shade = cell;
				fill_pat(&shade, 8, 2, 1);
			}
			if (day == cur_day) {
				GRECT in = cell;
				in.x++;
				in.y++;
				in.w -= 2;
				in.h -= 2;
				frame(&in, 1);
			}
			/* the date: today in red; days of other months light */
			if (clip->y <= cell.y + ch + 1) {
				char n[4];
				GRECT nb;
				snprintf(n, sizeof(n), "%d", d);
				nb.x = text_x(cell.x + 2 + cw / 2) - cw / 2;	/* round its text, on a byte */
				nb.y = cell.y + 1;
				nb.w = ((short)strlen(n) + 1) * cw;
				nb.h = ch + 1;
				if (day == today()) {
					fill(&nb, red_color());
					text_at(nb.x + cw / 2, nb.y, n, (long)strlen(n), 3, TX_INVERSE | TX_BOLD | TX_LTR);
				} else {
					text_at(nb.x + cw / 2, nb.y, n, (long)strlen(n), 3,
						TX_LTR | (m != m0 ? TX_PALE : 0) | (d == 1 ? TX_BOLD : 0));
				}
				if (d == 1 && cell.w >= 9 * cw)
					text_at(nb.x + cw / 2 + ((short)strlen(n) + 1) * cw, nb.y, month_short[m - 1], 3, 3,
						TX_LTR | TX_BOLD);
			}
			if (opt.hebdates && cell.w >= 7 * cw && clip->y <= cell.y + ch + 1) {
				HDATE h;
				char hs[40];
				short n;
				heb_of(day, &h);
				if ((h.day == 1 || (r == 0 && c == 0)) && cell.w >= 13 * cw)
					hebrew_format(&h, 0, hs, sizeof(hs));
				else
					hebrew_numeral(h.day, hs, sizeof(hs));
				n = (short)strlen(hs);
				text_at(cell.x + cell.w - (n + 1) * cw, cell.y + 1, hs, n, n + 1, TX_GREY);	/* (to a byte rightwards, inside its margin) */
			}
		}
	/* the grid: whole lines rather than four sides per cell (each VDI
	   call costs an 8 MHz ST milliseconds) */
	clip_on(clip);
	{
		/* only the lines and their pieces that cross what is drawn (a
		   cell, when the view is drawn a piece at a time) */
		GRECT g = grid;
		if (rect_and(&g, clip)) {
			for (r = 1; r <= rows; r++) {
				short y = (r == rows ? grid.y + grid.h : grid.y + r * cell_h) - 1;
				if (y >= g.y && y < g.y + g.h)
					line_col(g.x, y, g.x + g.w - 1, y, grey_color());
			}
			for (c = 1; c < 7; c++) {
				short x = colx[c] - 1;
				if (x >= g.x && x < g.x + g.w)
					line_col(x, g.y, x, g.y + g.h - 1, grey_color());
			}
		}
	}
	draw_items(clip, 1);
}

static void draw_details(GRECT *clip)
{
	GRECT r = detail, c = *clip;
	short x = r.x + cw, y = r.y + ch / 2, w = (r.w - 2 * cw) / cw;
	char line[200];
	const char *notes;
	if (!rect_and(&c, &r))
		return;
	clip_on(&c);
	fill(&r, 0);
	vline(r.x, r.y, r.y + r.h - 1);
	if (!sel_occ.ev) {
		HDATE h;
		char hs[60];
		DT d;
		ct_split(cur_day * MIN_PER_DAY, &d);
		snprintf(line, sizeof(line), "%s", wday_names[d.wday]);
		text_at(x, y, line, (long)strlen(line), w, TX_BOLD | TX_LTR);
		snprintf(line, sizeof(line), "%d %s %d", d.day, month_names[d.mon - 1], d.year);
		text_at(x, y + ch, line, (long)strlen(line), w, TX_LTR);
		hebrew_from_days(cur_day, &h);
		hebrew_format(&h, 1, hs, sizeof(hs));
		text_at(x, y + 2 * ch, hs, (long)strlen(hs), w, TX_RIGHT);
		{
			/* wrapped to the pane's width */
			static const char hint[] = "Click an event to see it here; double-click to change it.";
			long br[8], nb, i, n = (long)sizeof(hint) - 1;
			nb = wrap_text(hint, n, w, br, 8);
			for (i = 0; i < nb; i++) {
				long e = i + 1 < nb ? br[i + 1] : n;
				while (e > br[i] && hint[e - 1] == ' ')
					e--;
				text_at(x, y + (4 + (short)i) * ch, hint + br[i], e - br[i], w, TX_LTR | TX_GREY);
			}
		}
		return;
	}
	{
		EVENT *e = sel_occ.ev;
		DT a, b;
		char t1[16], t2[16];
		PEN p;
		GRECT dot;
		long br[40], nb, i;
		const char *title = e->summary ? e->summary : "New Event";
		pen_of(e->cal, &p);
		dot.x = x;
		dot.y = y + 2;
		dot.w = cw;
		dot.h = ch - 4;
		fill(&dot, p.color);
		text_at(x + 2 * cw, y, title, (long)strlen(title), w - 2, TX_BOLD | TX_RIGHT);
		y += ch;
		if (e->location) {
			text_at(x, y, e->location, (long)strlen(e->location), w, TX_RIGHT);
			y += ch;
		}
		y += ch / 2;
		ct_split(sel_occ.start, &a);
		ct_split(e->allday ? sel_occ.end - 1 : sel_occ.end, &b);
		if (e->allday) {
			if (ct_days(sel_occ.end - 1) == ct_days(sel_occ.start))
				snprintf(line, sizeof(line), "all-day, %s %d %s", wday_short[a.wday], a.day, month_short[a.mon - 1]);
			else
				snprintf(line, sizeof(line), "all-day, %d %s - %d %s", a.day, month_short[a.mon - 1],
					 b.day, month_short[b.mon - 1]);
		} else {
			fmt_time(a.hour, a.min, opt.h24, t1, sizeof(t1));
			fmt_time(b.hour, b.min, opt.h24, t2, sizeof(t2));
			snprintf(line, sizeof(line), "%s %d %s, %s to %s", wday_short[a.wday], a.day,
				 month_short[a.mon - 1], t1, t2);
		}
		text_at(x, y, line, (long)strlen(line), w, TX_LTR);
		y += ch;
		if (e->tz && !e->allday && !e->floating && opt.tzsupport) {
			char lab[16];
			tz_label(e->tz, ev_from_wall(e, sel_occ.start, view_zone()), lab, sizeof(lab));
			snprintf(line, sizeof(line), "%s (%s)", tz_city(e->tz), lab);
			text_at(x, y, line, (long)strlen(line), w, TX_LTR | TX_GREY);
			y += ch;
		}
		if (e->rr || e->recurid != CT_NONE) {
			EVENT *m = ev_master(e);
			if (m && m->rr) {
				rrule_describe(m->rr, line, sizeof(line));
				text_at(x, y, line, (long)strlen(line), w, TX_LTR);
				y += ch;
			}
		}
		if (e->alarm >= 0) {
			if (e->alarm == 0)
				snprintf(line, sizeof(line), "Alert: at the time of the event");
			else if (e->alarm % 1440 == 0)
				snprintf(line, sizeof(line), "Alert: %d day%s before", e->alarm / 1440, e->alarm == 1440 ? "" : "s");
			else if (e->alarm % 60 == 0)
				snprintf(line, sizeof(line), "Alert: %d hour%s before", e->alarm / 60, e->alarm == 60 ? "" : "s");
			else
				snprintf(line, sizeof(line), "Alert: %d minutes before", e->alarm);
			text_at(x, y, line, (long)strlen(line), w, TX_LTR);
			y += ch;
		}
		snprintf(line, sizeof(line), "Calendar: %s", cals[e->cal].name);
		text_at(x, y, line, (long)strlen(line), w, TX_LTR | TX_GREY);
		y += ch + ch / 2;
		if ((notes = ev_notes_text(e)) != 0) {
			nb = wrap_text(notes, (long)strlen(notes), w, br, 40);
			for (i = 0; i < nb && y + ch <= r.y + r.h; i++) {
				long end = i + 1 < nb ? br[i + 1] : (long)strlen(notes);
				while (end > br[i] && (notes[end - 1] == '\n' || notes[end - 1] == ' '))
					end--;
				text_at(x, y, notes + br[i], end - br[i], w, TX_RIGHT);
				y += ch;
			}
		}
		if (e->url && y + ch <= r.y + r.h)
			text_at(x, y, e->url, (long)strlen(e->url), w, TX_LTR | TX_UNDER);
	}
}

static short now_x = -1, now_y, now_w;	/* where the "now" line was drawn, -1: not */

static void draw_days(GRECT *clip)
{
	short c, h;
	long t = today();
	CTIME now = now_view();
	GRECT g;
	short cy0 = clip->y, cy1 = clip->y + clip->h;	/* what is drawn, from row to row */
	short cx0 = clip->x, cx1 = clip->x + clip->w;	/* and column to column */
	clip_on(clip);
	/* the day headers; a scroll draws only a strip of the hours, a box
	   redrawn only its columns (each text costs an ST milliseconds,
	   clipped or not) */
	for (c = 0; c < cols && cy0 < grid.y; c++) {
		long day = first_day + c;
		DT d;
		char s[40];
		short x = colx[c], n, w = COLW(c);
		if (x + w <= cx0 || x >= cx1)
			continue;
		if (c)
			line_col(x, r_view.y, x, r_view.y + r_view.h - 1, grey_color());
		if (cy0 >= r_view.y + head_h - 1)
			continue;	/* the all-day row only */
		ct_split(day * MIN_PER_DAY, &d);
		if (cur_view == V_DAY)
			snprintf(s, sizeof(s), "%s %d %s", wday_names[d.wday], d.day, month_short[d.mon - 1]);
		else if (w >= 8 * cw)
			snprintf(s, sizeof(s), "%s %d", wday_short[d.wday], d.day);
		else
			snprintf(s, sizeof(s), "%d", d.day);
		n = (short)strlen(s);
		if (day == t || day == cur_day) {
			/* today red, the selected day black, white letters: as in
			   the small month on the left */
			GRECT b;
			b.x = text_x(x + 2 + cw / 2) - cw / 2;	/* round its text, on a byte */
			b.y = r_view.y + 2;
			b.w = (n + 1) * cw;
			b.h = ch + 1;
			fill(&b, day == t ? red_color() : 1);
			text_at(x + 2 + cw / 2, r_view.y + 2, s, n, n + 1, TX_INVERSE | TX_LTR);
		} else {
			text_at(x + 2 + cw / 2, r_view.y + 2, s, n, w / cw - 1, TX_LTR);
		}
		if (opt.hebdates) {
			HDATE hd;
			char hs[40];
			heb_of(day, &hd);
			hebrew_format(&hd, cur_view == V_DAY, hs, sizeof(hs));
			n = (short)strlen(hs);
			if (n * cw > w - cw) {
				hebrew_numeral(hd.day, hs, sizeof(hs));
				n = (short)strlen(hs);
			}
			text_at(x + 2 + cw / 2, r_view.y + 3 + ch, hs, n, w / cw - 1, TX_GREY);
		}
	}
	if (cy0 < grid.y) {
		line_col(r_view.x, r_view.y + head_h - 1, grid.x + grid.w - 1, r_view.y + head_h - 1, 1);
		if (cx0 < grid.x)
			text_at(r_view.x, r_view.y + head_h + 2, "all-day", 7, gutter / cw, TX_GREY | TX_LTR);
		line_col(r_view.x, grid.y - 1, grid.x + grid.w - 1, grid.y - 1, 1);
	}
	/* hours */
	g = grid;
	g.x = r_view.x;
	g.w = gutter + grid.w;
	if (rect_and(&g, clip)) {
		short lx0 = g.x > grid.x ? g.x : grid.x;
		short lx1 = g.x + g.w < grid.x + grid.w ? g.x + g.w - 1 : grid.x + grid.w - 1;
		clip_on(&g);
		/* drawn the same wherever the hours are scrolled to, so
		   what a scroll copies matches what it draws new */
		long top = scroll_y + (cy0 > grid.y ? cy0 - grid.y : 0) - ch;
		for (h = (short)((top < 0 ? 0 : top) / hour_h); h <= 24; h++) {
			short y = grid.y + (short)((long)h * hour_h - scroll_y);
			char s[12];
			if (y > grid.y + grid.h + ch || y - ch > cy1)
				break;
			if (h > 0 && h < 24 && cx0 < grid.x) {
				fmt_time(h, 0, opt.h24, s, sizeof(s));
				text_at(r_view.x + 2, y - ch / 2, s, (long)strlen(s), gutter / cw - 1, TX_LTR | TX_GREY);
			}
			if (lx0 <= lx1) {	/* (the part of the line in what is drawn) */
				line_col(lx0, y, lx1, y, grey_color());
				if (hour_h >= 2 * ch && ncolors >= 16)
					line_col(lx0, y + hour_h / 2, lx1, y + hour_h / 2, 8);
			}
		}
		for (c = 1; c < cols; c++)
			if (colx[c] >= g.x && colx[c] < g.x + g.w)
				line_col(colx[c], g.y, colx[c], g.y + g.h - 1, grey_color());
	}
	draw_items(clip, 0);
	/* now: a red line across today */
	now_x = -1;
	if (t >= first_day && t < first_day + ndays) {
		long m = now - t * MIN_PER_DAY;
		short y = grid.y + (short)(m * hour_h / 60 - scroll_y), x = colx[t - first_day], w = COLW(t - first_day);
		now_x = x;
		now_y = y;
		now_w = w;
		if (y >= grid.y - 2 && y < grid.y + grid.h + 2) {
			GRECT dot, cc = grid;
			if (rect_and(&cc, clip)) {
				clip_on(&cc);
				line_col(x, y, x + w - 1, y, red_color());
				line_col(x, y + 1, x + w - 1, y + 1, red_color());
				dot.x = x;
				dot.y = y - 2;
				dot.w = 5;
				dot.h = 6;
				fill(&dot, red_color());
			}
		}
	}
	g = detail;
	if (detail.w && rect_and(&g, clip))
		draw_details(clip);
}

/* the year: twelve small months; days with events in bold */
/* the year: twelve small months. When they don't fit in the system font
   (a 640x400 screen holds only six), the small 8x8 (or 6x6) font draws
   them, as Atari programs always did */
static short yfont, ysave_cw, ysave_ch;

/* level 0: the system font; 1, 2: the next smaller ones (8x8, 6x6) */
static void year_font(int level)
{
	static const short heights[3] = { 13, 6, 4 };
	short c, h, base;
	if (!yfont && !level)
		return;
	if (!yfont) {
		ysave_cw = cw;
		ysave_ch = ch;
	}
	base = ysave_ch >= 16 ? 0 : 1;		/* ST medium's system font is already 8x8 */
	if (base + level > 2)
		level = 2 - base;
	if (level) {
		vst_font(vdi_h, 1);
		heb_mode = HM_MAP;	/* the system font has the Atari codes */
		vst_height(vdi_h, heights[base + level], &c, &h);
		cw = c;
		ch = h;
	} else {
		font_apply();		/* back to the chosen font */
	}
	yfont = (short)level;
}

static int year_fits(short pc)
{
	short rows = (12 + pc - 1) / pc;
	return r_view.w >= pc * 22 * cw && r_view.h >= rows * (8 * ch + ch / 2 + 2);
}

/* pick columns (and font) for the year; leaves the font chosen in force */
static struct {
	short ok, mw, mh, pc, level;
} yg;				/* year_geom's answer, kept until the next layout */

static void yg_forget(void)
{
	yg.ok = 0;
}

static void year_geom(short *mw, short *mh, short *pc)
{
	int level;
	if (yg.ok) {
		/* (worked out once a layout: each piece drawn asks) */
		year_font(yg.level);
		*mw = yg.mw;
		*mh = yg.mh;
		*pc = yg.pc;
		return;
	}
	for (level = 0; level < 3; level++) {
		year_font(level);
		for (*pc = 4; *pc >= 2; (*pc)--)
			if (year_fits(*pc))
				break;
		if (*pc >= 2)
			break;
	}
	if (*pc < 2)
		*pc = 2;
	*mw = r_view.w / *pc;
	*mh = r_view.h / ((12 + *pc - 1) / *pc);
	yg.ok = 1;
	yg.mw = *mw;
	yg.mh = *mh;
	yg.pc = *pc;
	yg.level = yfont;
}

#define YBUSY 0x4000		/* (draw_year's own: a day with events) */

static unsigned char busy_[372];	/* the year's days with events, for the occurrences
					   collected (busy_known: once, not each piece drawn) */

static void draw_year(GRECT *clip)
{
	short mw, mh, pc, m;
	int y0, m0, d0, i;
	long jan1, t = today();
	short bytes_was = text_bytes;
	civil_from_days(cur_day, &y0, &m0, &d0);
	jan1 = days_from_civil(y0, 1, 1);
	if (!busy_known) {
		memset(busy_, 0, sizeof(busy_));
		for (i = 0; i < occ.n; i++) {
			long a = occ_first_day(&occ.o[i]) - jan1, b = occ_last_day(&occ.o[i]) - jan1;
			for (; a <= b && a < 372; a++)
				if (a >= 0)
					busy_[a] = 1;
		}
		busy_known = 1;
	}
	year_geom(&mw, &mh, &pc);
	/* a day is three columns: where three aren't a byte (the 6x6 font),
	   its text goes where it falls, not on a byte (rounded, a run of
	   days would lose its last figure) */
	if ((3 * cw) & 7)
		text_bytes = 0;
	clip_on(clip);
	for (m = 0; m < 12; m++) {
		short ox = (r_view.x + (m % pc) * mw + (mw - 21 * cw) / 2) & ~7, oy = r_view.y + (m / pc) * mh + ch / 2;
		long first, d;
		short off, k, dim;
		{
			/* a month outside what is drawn (a band, a box): skipped,
			   its texts cost an ST milliseconds clipped or not */
			GRECT mr;
			mr.x = r_view.x + (m % pc) * mw;
			mr.y = r_view.y + (m / pc) * mh;
			mr.w = mw;
			mr.h = mh;
			if (!rect_and(&mr, clip))
				continue;
		}
		first = days_from_civil(y0, m + 1, 1);
		off = (short)((wday_of(first) - opt.weekstart + 7) % 7);
		dim = (short)days_in_month(y0, m + 1);
		if (clip->y < oy + 2 * ch + 4) {	/* (the month's head, unless only its days are drawn) */
			text_at(ox, oy, month_names[m], (long)strlen(month_names[m]), 20,
				TX_BOLD | TX_LTR | (m + 1 == m0 ? TX_UNDER : 0));
			for (k = 0; k < 7; k++)
				text_at(ox + k * 3 * cw, oy + ch + 2, wday_short[(k + opt.weekstart) % 7], 2, 3,
					TX_LTR | TX_GREY);
		}
		/* a week row at a time, consecutive days of the same look in one
		   piece of text: on an 8 MHz ST each text call costs milliseconds.
		   */
		{
			char run[24];
			short rl = 0, rx = 0, ry = 0, rflags = -1, col = off - 1, row = 0;
			for (k = 0; k <= dim; k++) {
				short flags = 0, x, y, tens;
				if (++col == 7) {	/* (counted: a division a day is slow on a 68000) */
					col = 0;
					row++;
				}
				x = ox + col * 3 * cw;
				y = oy + (row + 2) * ch + 4;
				d = first + k;
				if (k < dim) {
					if (d == t || d == cur_day) {
						GRECT b;
						b.x = x - 1;
						b.y = y;
						b.w = 2 * cw + 2;
						b.h = ch;
						fill(&b, d == t ? red_color() : 1);
						flags = TX_INVERSE | (d == t ? TX_BOLD : 0);
					} else {
						/* a day with events: blue where there are colours, and
						   underlined in the system font; not bold (the
						   guidelines, Speed: 9 times plain text's time on an ST),
						   except in a small font without colours, where nothing
						   else can be read (an underline runs into the next row).
						   The other days light, but not in the small fonts */
						flags = !busy_[d - jan1] ? (yfont ? TX_GREY : TX_PALE) :
							!yfont ? TX_UNDER | YBUSY : ncolors >= 16 ? YBUSY : TX_BOLD;
					}
				}
				if (rl && (k == dim || flags != rflags || col == 0 || (rflags & TX_INVERSE))) {
					if (ry < clip->y + clip->h && ry + ch > clip->y) {	/* (its row drawn) */
						if ((rflags & YBUSY) && ncolors >= 16)
							text_color(4);
						text_at(rx, ry, run, rl, rl, TX_LTR | (rflags & ~YBUSY));
					}
					rl = 0;
				}
				if (k == dim)
					break;
				if (!rl) {
					rx = x;
					ry = y;
					rflags = flags;
				} else {
					run[rl++] = ' ';
				}
				tens = (short)div16(k + 1, 10);
				run[rl++] = (char)(tens ? '0' + tens : ' ');
				run[rl++] = (char)('0' + (k + 1) - tens * 10);
			}
		}
	}
	text_bytes = bytes_was;
}


void views_draw(GRECT *clip)
{
	GRECT r = r_view;
	if (!rect_and(&r, clip))
		return;
#ifdef AES_PROFILE
	extern long prof_texts, prof_texts_off, prof_texts_bold;
	unsigned long t0 = pf_ms(), t1, t2;
	int fresh = !occ_valid || !laid_out;
	long tx0 = prof_texts, to0 = prof_texts_off, tb0 = prof_texts_bold;
	collect();
	t1 = pf_ms();
#endif
	today_cache = ct_days(now_view());
	layout();
#ifdef AES_PROFILE
	t2 = pf_ms();
#endif
	clip_on(&r);
	fill(&r, 0);
	switch (cur_view) {
	case V_MONTH: draw_month(&r); break;
	case V_YEAR: draw_year(&r); year_font(0); break;
	default: draw_days(&r); break;
	}
	today_cache = -1;
#ifdef AES_PROFILE
	if (pf_ms() - t0 >= 20 && !win_view_busy()) {
		/* where a view's time goes, for the speed rig (not each piece
		   of one drawn in slices: VIEWSLICE says those) */
		char b[160];
		snprintf(b, sizeof(b), "VIEWMS view %d %dx%d%s: collect %lu layout %lu draw %lu (%d occurrences; "
			 "%ld texts, %ld off a byte, %ld bold or light)\n",
			 cur_view, r.w, r.h, fresh ? "" : " again", t1 - t0, t2 - t1, pf_ms() - t2, occ.n,
			 prof_texts - tx0, prof_texts_off - to0, prof_texts_bold - tb0);
		nf_debug(b);
	}
#endif
}

/* the minute's tick: the "now" line where it has moved to, and only it
   (the whole view drawn again every half minute held an ST for 0.9 s) */
void views_now_moved(void)
{
	long t, m;
	short y, x;
	GRECT a;
	if (cur_view != V_DAY && cur_view != V_WEEK)
		return;
	layout();
	t = ct_days(now_view());
	if (t < first_day || t >= first_day + ndays) {
		if (now_x >= 0)
			win_redraw(&r_view);	/* it left the view */
		return;
	}
	m = now_view() - t * MIN_PER_DAY;
	y = grid.y + (short)(m * hour_h / 60 - scroll_y);
	x = colx[t - first_day];
	if (x == now_x && y == now_y)
		return;
	a.x = x;
	a.y = y - 2;
	a.w = COLW(t - first_day);
	a.h = 6;
	if (now_x == x && now_y < y && y - now_y < 4 * ch) {
		/* a minute or few on, in the same column: one redraw for the
		   old line and the new (each costs an ST some 70 ms) */
		a.h += y - now_y;
		a.y = now_y - 2;
	} else if (now_x >= 0) {
		GRECT o;
		o.x = now_x;
		o.y = now_y - 2;
		o.w = now_w;
		o.h = 6;
		win_redraw(&o);
	}
	win_redraw(&a);
}

/* the view in pieces along its own lines, for drawing a piece at a time
   (win.c): the month's header and cells, the week's and day's columns
   (their heads, then their hours), the year's months; each piece's
   texts drawn once. Top to bottom, left to right; n at most max */
#define YTILE_ROWS 48
int views_tiles(GRECT *t, int max)
{
	short xs[12], ys[YTILE_ROWS + 2];
	int nx = 0, ny = 0, i, j, n = 0;
	short c, r;
	layout();
	xs[nx++] = r_view.x;
	ys[ny++] = r_view.y;
	if (cur_view == V_MONTH) {
		for (c = 1; c < cols && c < 7; c++)
			xs[nx++] = colx[c];
		ys[ny++] = grid.y;
		for (r = 1; r < rows && ny < 9; r++)
			ys[ny++] = grid.y + r * cell_h;
	} else if (cur_view == V_WEEK || cur_view == V_DAY) {
		xs[nx++] = grid.x;
		for (c = 1; c < cols && nx < 10; c++)
			xs[nx++] = colx[c];
		if (detail.w)
			xs[nx++] = detail.x;
		/* the hours in quarters: a column of a busy day is too much for
		   one slice on an ST */
		for (r = 0; r < 4; r++)
			ys[ny++] = grid.y + r * (grid.h / 4);
	} else {
		/* a month a row of weeks at a time (the year's small text is off
		   a byte, a month too much for one slice on an ST) */
		short mw, mh, pc, ych;
		year_geom(&mw, &mh, &pc);
		ych = ch;		/* (the year's font's) */
		year_font(0);
		for (c = 1; c < pc && nx < 10; c++)
			xs[nx++] = r_view.x + c * mw;
		for (r = 0; r < (12 + pc - 1) / pc && ny < YTILE_ROWS; r++) {
			short oy = r_view.y + r * mh, k;
			if (r)
				ys[ny++] = oy;
			for (k = 0; k < 6 && ny < YTILE_ROWS; k++)
				ys[ny++] = oy + ych / 2 + (k + 2) * ych + 4;
		}
	}
	xs[nx++] = r_view.x + r_view.w;
	ys[ny++] = r_view.y + r_view.h;
	/* (each line below the one before: a year that doesn't fit overlaps) */
	for (j = 1, i = 1; j < ny; j++)
		if (ys[j] > ys[i - 1] && (ys[j] <= r_view.y + r_view.h))
			ys[i++] = ys[j];
	ny = i;
	if (ys[ny - 1] < r_view.y + r_view.h)
		ys[ny++] = r_view.y + r_view.h;
	for (j = 0; j + 1 < ny; j++)
		for (i = 0; i + 1 < nx && n < max; i++) {
			short y0 = ys[j], y1 = ys[j + 1], x0 = xs[i], x1 = xs[i + 1];
			if (y1 > r_view.y + r_view.h)
				y1 = r_view.y + r_view.h;
			if (x1 > r_view.x + r_view.w)
				x1 = r_view.x + r_view.w;
			if (x1 <= x0 || y1 <= y0)
				continue;
			t[n].x = x0;
			t[n].y = y0;
			t[n].w = x1 - x0;
			t[n].h = y1 - y0;
			n++;
		}
	return n;
}

/* ---------------- titles and moving around ---------------- */

void views_title(char *out, int size)
{
	DT a, b;
	ct_split(cur_day * MIN_PER_DAY, &a);
	switch (cur_view) {
	case V_DAY:
		snprintf(out, size, "%s, %d %s %d", wday_names[a.wday], a.day, month_names[a.mon - 1], a.year);
		break;
	case V_WEEK: {
		long ws = week_start(cur_day);
		ct_split(ws * MIN_PER_DAY, &a);
		ct_split((ws + 6) * MIN_PER_DAY, &b);
		if (a.mon == b.mon)
			snprintf(out, size, "%d - %d %s %d", a.day, b.day, month_names[a.mon - 1], a.year);
		else if (a.year == b.year)
			snprintf(out, size, "%d %s - %d %s %d", a.day, month_short[a.mon - 1], b.day,
				 month_short[b.mon - 1], b.year);
		else
			snprintf(out, size, "%d %s %d - %d %s %d", a.day, month_short[a.mon - 1], a.year,
				 b.day, month_short[b.mon - 1], b.year);
		break;
	}
	case V_MONTH:
		snprintf(out, size, "%s %d", month_names[a.mon - 1], a.year);
		break;
	default:
		snprintf(out, size, "%d", a.year);
	}
}

static int view_changed;	/* (changed_range: the view buttons too) */

static void changed_range(void)
{
	char t[200];
	laid_out = 0;
	glide_left = 0;
	/* at once the title (and the view buttons when the view changed);
	   the view in slices, the old page shown meanwhile, then the small
	   month (win.c); not the calendars' list (the whole window took
	   0.4 s more on an ST) */
	win_view_later();	/* (first: the title's redraw then leaves the sliders to it) */
	win_redraw_title(view_changed);
	view_changed = 0;
	views_title(t, sizeof(t));
	(void)t;
}

void views_goto(long day, short view)
{
	cur_day = day;
	if (view != cur_view) {
		cur_view = view;
		opt.view = view;
		view_changed = 1;
	}
	laid_out = 0;
	changed_range();
}

void views_today(void)
{
	long t = today();
	CTIME now = now_view();
	if (cur_view == V_DAY || cur_view == V_WEEK) {
		short h = (short)((now - t * MIN_PER_DAY) / 60) - 1;
		long y = (long)h * hour_h;
		if (!hour_h || y < scroll_y || y + 2 * hour_h > scroll_y + grid.h)
			views_scroll_to(h < 0 ? 0 : h);
	}
	views_goto(t, cur_view);
}

void views_step(int dir)
{
	long d = cur_day;
	switch (cur_view) {
	case V_DAY: d += dir; break;
	case V_WEEK: d += 7 * dir; break;
	case V_MONTH: d = days_add_months(d, dir); break;
	default: d = days_add_months(d, 12 * dir); break;
	}
	views_goto(d, cur_view);
}

/* ---------------- clicks ---------------- */

static int item_at(short mx, short my)
{
	int i;
	for (i = nitems - 1; i >= 0; i--)
		if (rect_hit(&items[i].r, mx, my)) {
			/* a box scrolled out of the time grid isn't there to click */
			if (items[i].timed && (my < grid.y || my >= grid.y + grid.h))
				continue;
			return i;
		}
	return -1;
}

/* what a tooltip shows for the event under the mouse: its title and its
   time; 0 when there is none */
int views_tip(short mx, short my, char *title, int tn, char *when, int wn)
{
	int it;
	OCC *o;
	DT a, b;
	char t1[16], t2[16];
	if (!rect_hit(&r_view, mx, my) || cur_view == V_YEAR)
		return 0;
	layout();
	it = item_at(mx, my);
	if (it < 0 || items[it].kind != IT_EVENT)
		return 0;
	o = &occ.o[items[it].occ];
	str_copy(title, o->ev->summary ? o->ev->summary : "New Event", tn);
	ct_split(o->start, &a);
	ct_split(o->end, &b);
	if (o->ev->allday) {
		if (o->end - o->start <= MIN_PER_DAY)
			snprintf(when, wn, "all day, %d %s", a.day, month_short[a.mon - 1]);
		else {
			ct_split(o->end - MIN_PER_DAY, &b);
			snprintf(when, wn, "%d %s - %d %s", a.day, month_short[a.mon - 1], b.day, month_short[b.mon - 1]);
		}
	} else {
		fmt_time(a.hour, a.min, opt.h24, t1, sizeof(t1));
		fmt_time(b.hour, b.min, opt.h24, t2, sizeof(t2));
		if (ct_days(o->start) == ct_days(o->end) || (b.hour == 0 && b.min == 0 && o->end - o->start <= MIN_PER_DAY))
			snprintf(when, wn, "%s - %s", t1, t2);
		else
			snprintf(when, wn, "%d %s %s - %d %s %s", a.day, month_short[a.mon - 1], t1,
				 b.day, month_short[b.mon - 1], t2);
	}
	return 1;
}

static long year_day_at(short mx, short my, short *minute)
{
	short mw, mh, pc, m, col, row;
	int y0, m0, d0;
	long first;
	year_geom(&mw, &mh, &pc);
	m = ((my - r_view.y) / mh) * pc + (mx - r_view.x) / mw;
	if (m < 0 || m > 11)
		return -1;
	civil_from_days(cur_day, &y0, &m0, &d0);
	first = days_from_civil(y0, m + 1, 1);
	col = (mx - ((r_view.x + (m % pc) * mw + (mw - 21 * cw) / 2) & ~7)) / (3 * cw);	/* (on a byte, as drawn) */
	row = (my - (r_view.y + (m / pc) * mh + ch / 2) - 4) / ch - 2;
	if (row < 0 || col < 0 || col > 6) {
		if (minute)
			*minute = -2;	/* the month's name: show that month */
		return first;
	}
	{
		long d = first - (wday_of(first) - opt.weekstart + 7) % 7 + row * 7 + col;
		if (d < first || d >= first + days_in_month(y0, m + 1))
			return -1;
		return d;
	}
}

static long day_at(short mx, short my, short *minute)
{
	if (minute)
		*minute = -1;
	if (cur_view == V_MONTH) {
		short c = col_at(mx), r = (my - grid.y) / cell_h;
		if (my < grid.y || c < 0 || c > 6 || r < 0 || r >= rows)
			return -1;
		return first_day + r * 7 + c;
	}
	if (cur_view == V_YEAR) {
		long d = year_day_at(mx, my, minute);
		year_font(0);
		return d;
	}
	/* day, week */
	{
		short c = col_at(mx);
		if (mx < grid.x || c < 0 || c >= cols)
			return -1;
		if (minute && my >= grid.y)
			*minute = (short)((scroll_y + my - grid.y) * 60 / hour_h) / 30 * 30;
		return first_day + c;
	}
}

static void select_item(int i);

/* every box of an occurrence (a bar over several days has several) */
static void redraw_occ(const OCC *o)
{
	int i;
	if (!o->ev)
		return;
	for (i = 0; i < nitems; i++)
		if (items[i].kind == IT_EVENT && occ.o[items[i].occ].ev == o->ev && occ.o[items[i].occ].start == o->start) {
			GRECT r = items[i].r;
			r.w++;
			r.h++;
			win_redraw(&r);
		}
}

/* Dragging an event: an outline follows the mouse, snapped to 15 minutes
   in the day and week grids (to whole days for all-day events and in the
   month), the info line says where it will go, and letting go moves it.
   0 when the button came up without the mouse moving: a plain click */
static int drag_event(int it, short mx, short my)
{
	ITEM *item = &items[it];
	OCC o = occ.o[item->occ];
	GRECT ghost, last;
	short x = mx, y = my, b, k, shown = 0, m[8];
	long grab_day = item->day, to_day = grab_day;
	short start_min = (short)(o.start - ct_day_start(o.start));
	CTIME to = o.start;
	int timed = item->timed && cur_view != V_MONTH, moved = 0;
	AEVENT e;
	/* only a press that moves a few pixels is a drag */
	for (;;) {
		graf_mkstate(&x, &y, &b, &k);
		if (!(b & 1))
			return 0;
		if (x - mx > 3 || mx - x > 3 || y - my > 3 || my - y > 3)
			break;
		evnt_timer_(10);
	}
	wind_update(BEG_UPDATE);
	wind_update(3);			/* the mouse is ours until the button comes up */
	graf_mouse(4, 0);		/* the flat hand */
	clip_on(&r_view);
	for (;;) {
		long d = day_at(x, y, 0);
		char line[80], tm[16];
		DT a;
		if (d >= first_day && d < first_day + ndays)
			to_day = d;
		ghost = item->r;
		if (timed && item->cont) {
			/* the part after midnight of an event from the day before:
			   it moves by as much as the mouse does */
			long dm = (long)(y - my) * 60 / hour_h;
			dm = (dm + (dm >= 0 ? 7 : -7)) / 15 * 15;
			to = o.start + (to_day - grab_day) * MIN_PER_DAY + dm;
			ghost.x = colx[to_day - first_day] + 1;
			ghost.w = COLW(to_day - first_day) - 2;
			ghost.y = item->r.y + (short)(dm * hour_h / 60);
		} else if (timed) {
			short mins = start_min + (short)((long)(y - my) * 60 / hour_h);
			mins = (short)((mins + (mins >= 0 ? 7 : -7)) / 15 * 15);	/* the nearest quarter */
			if (mins < 0)
				mins = 0;
			if (mins > MIN_PER_DAY - 15)
				mins = MIN_PER_DAY - 15;
			to = (CTIME)to_day * MIN_PER_DAY + mins;
			ghost.x = colx[to_day - first_day] + 1;
			ghost.w = COLW(to_day - first_day) - 2;
			ghost.y = grid.y + (short)((long)mins * hour_h / 60 - scroll_y);
		} else {
			/* whole days: the outline jumps from cell to cell */
			long dd = to_day - grab_day;
			to = o.start + dd * MIN_PER_DAY;
			if (cur_view == V_MONTH) {
				short gc = (short)((grab_day - first_day) % 7), gr = (short)((grab_day - first_day) / 7);
				short tc = (short)((to_day - first_day) % 7), tr = (short)((to_day - first_day) / 7);
				ghost.x += colx[tc] - colx[gc];
				ghost.y += (tr - gr) * cell_h;
			} else {
				/* the bar's first column moved by dd, within the view */
				short a = col_at(item->r.x), b = a + (short)dd;
				if (a >= 0) {
					if (b < 0)
						b = 0;
					if (b >= cols)
						b = cols - 1;
					ghost.x += colx[b] - colx[a];
				}
			}
		}
		if (!shown || memcmp(&ghost, &last, sizeof(ghost))) {
			graf_mouse(M_OFF, 0);
			if (shown)
				xor_frame(&last);
			xor_frame(&ghost);
			graf_mouse(M_ON, 0);
			last = ghost;
			shown = 1;
			ct_split(to, &a);
			fmt_time(a.hour, a.min, opt.h24, tm, sizeof(tm));
			snprintf(line, sizeof(line), "Move to %s %d %s%s%s", wday_short[a.wday], a.day,
				 month_short[a.mon - 1], o.ev->allday ? "" : ", ", o.ev->allday ? "" : tm);
			win_status(line);
		}
		/* till the mouse moves or the button comes up */
		evnt_set_m1(1, x, y, 1, 1);
		evnt_multi_(MU_M1 | MU_BUTTON, 1, 1, 0, 0, m, &e);
		graf_mkstate(&x, &y, &b, &k);
		if (!(b & 1))
			break;
	}
	graf_mouse(M_OFF, 0);
	xor_frame(&last);
	graf_mouse(M_ON, 0);
	clip_off();
	graf_mouse(ARROW, 0);
	wind_update(2);
	wind_update(END_UPDATE);
	if (to != o.start)
		moved = editor_move(o.ev, o.start, o.orig, to);
	if (moved) {
		views_select_none();
		after_change();
		status_updated();
	} else {
		select_item(it);
		win_redraw(&r_view);
	}
	return 1;
}

static void select_item(int i)
{
	OCC *o = &occ.o[items[i].occ];
	char line[200], t1[16];
	DT a;
	sel_occ = *o;
	ct_split(o->start, &a);
	fmt_time(a.hour, a.min, opt.h24, t1, sizeof(t1));
	snprintf(line, sizeof(line), "%s %d %s%s%s  %.60s  (%s)", wday_short[a.wday], a.day, month_short[a.mon - 1],
		 o->ev->allday ? "" : ", ", o->ev->allday ? "" : t1,
		 o->ev->summary ? o->ev->summary : "", cals[o->ev->cal].name);
	win_status(line);
}

static void open_selected(void)
{
	EVENT *e = sel_occ.ev;
	if (!e)
		return;
	if (editor_open(e, sel_occ.start, sel_occ.orig))
		after_change();
}

/* ^C: the event chosen, as text: its title, when, where, then its notes */
void views_copy(void)
{
	EVENT *e = sel_occ.ev;
	char when[80], t1[16], t2[16], *t;
	DT a, b;
	const char *notes;
	long n;
	if (!e)
		return;
	ct_split(sel_occ.start, &a);
	ct_split(e->allday ? sel_occ.end - MIN_PER_DAY : sel_occ.end, &b);
	fmt_time(a.hour, a.min, opt.h24, t1, sizeof(t1));
	fmt_time(b.hour, b.min, opt.h24, t2, sizeof(t2));
	if (e->allday && a.day == b.day && a.mon == b.mon && a.year == b.year)
		snprintf(when, sizeof(when), "%s %d %s %d, all day", wday_short[a.wday], a.day, month_short[a.mon - 1], a.year);
	else if (e->allday)
		snprintf(when, sizeof(when), "%d %s %d - %d %s %d", a.day, month_short[a.mon - 1], a.year,
			 b.day, month_short[b.mon - 1], b.year);
	else if (a.day == b.day && a.mon == b.mon && a.year == b.year)
		snprintf(when, sizeof(when), "%s %d %s %d, %s - %s", wday_short[a.wday], a.day, month_short[a.mon - 1],
			 a.year, t1, t2);
	else
		snprintf(when, sizeof(when), "%d %s %d %s - %d %s %d %s", a.day, month_short[a.mon - 1], a.year, t1,
			 b.day, month_short[b.mon - 1], b.year, t2);
	notes = ev_notes_text(e);
	n = 120 + (long)strlen(when) + (e->summary ? (long)strlen(e->summary) : 0) +
	    (e->location ? (long)strlen(e->location) : 0) + (notes ? (long)strlen(notes) : 0);
	if (!(t = malloc(n)))
		return;
	snprintf(t, n, "%s\n%s%s%s%s%s", e->summary ? e->summary : "New Event", when,
		 e->location && *e->location ? "\n" : "", e->location ? e->location : "",
		 notes && *notes ? "\n\n" : "", notes ? notes : "");
	win_status(clip_put(t, (long)strlen(t)) ? "Copied to the clipboard." : "Couldn't write the clipboard.");
	free(t);
}

/* the event chosen: 0 none, 1 one that can't be changed, 2 one that can
   (the Event menu greys what doesn't apply) */
int views_selected(void)
{
	return !sel_occ.ev ? 0 : cal_writable(&cals[sel_occ.ev->cal]) ? 2 : 1;
}

/* the Event menu's commands on the event chosen, and its context menu's */
void views_event(int what)
{
	EVENT *c;
	CTIME d;
	if (!sel_occ.ev)
		return;
	if (what == EV_OPEN) {
		open_selected();
		return;
	}
	if (what == EV_DEL) {
		cmd_delete_selected();
		return;
	}
	if (views_selected() != 2 || !(c = ev_dup(sel_occ.ev)))
		return;
	/* Duplicate: a new event at the same time, on its own */
	d = c->end - c->start;
	free(c->uid);
	c->uid = 0;
	c->res = -1;
	c->recurid = CT_NONE;
	c->start = ev_from_wall(c, sel_occ.start, view_zone());
	c->end = c->start + d;
	free(c->rr);
	c->rr = 0;
	c->nexdate = 0;
	busy(1);
	if (!ev_save(c, c->cal, SAVE_ALL, CT_NONE))
		alert(1, "[1][Can't copy the event:|%s][ OK ]", cal_err);
	busy(0);
	ev_free(c);
	after_change();
}

static void context_menu(short mx, short my, int it, long day, short minute)
{
	static const char *const ev_items[] = { "  Open...", "  Copy        ^C", "  Duplicate", "-", "  Delete..." };
	static const char *const day_items[] = { "  New event...", "  New all-day event", "-",
						 "  Show this day", "  Show this week", "  Today" };
	short k;
	if (it >= 0 && items[it].kind == IT_EVENT) {
		select_item(it);
		win_redraw(&r_view);
		k = win_popup(mx, my, ev_items, cal_writable(&cals[sel_occ.ev->cal]) ? 5 : 2);
		if (k == 0)
			views_event(EV_OPEN);
		else if (k == 1)
			views_copy();
		else if (k == 2)
			views_event(EV_DUP);
		else if (k == 4)
			views_event(EV_DEL);
		return;
	}
	if (day < 0)
		return;
	k = win_popup(mx, my, day_items, 6);
	switch (k) {
	case 0: editor_new(day, minute >= 0 ? minute : -1, 0); break;
	case 1: editor_new(day, -1, 1); break;
	case 3: views_goto(day, V_DAY); break;
	case 4: views_goto(day, V_WEEK); break;
	case 5: views_today(); break;
	}
}

int views_click(short mx, short my, short clicks, short kstate, short button)
{
	int it;
	short minute;
	long day;
	layout();
	it = item_at(mx, my);
	day = day_at(mx, my, &minute);
	if (button & 2) {
		context_menu(mx, my, it, day, minute);
		return 1;
	}
	if (cur_view == V_DAY && detail.w && rect_hit(&detail, mx, my)) {
		if (clicks == 2)
			open_selected();
		return 1;
	}
	if (it >= 0 && items[it].kind == IT_MORE) {
		views_goto(items[it].day, V_DAY);
		return 1;
	}
	if (it >= 0 && clicks == 1 && cur_view != V_YEAR && cal_writable(&cals[occ.o[items[it].occ].ev->cal]) &&
	    drag_event(it, mx, my))
		return 1;
	if (it >= 0) {
		int same = sel_occ.ev == occ.o[items[it].occ].ev && sel_occ.start == occ.o[items[it].occ].start;
		OCC was = sel_occ;
		long day0 = cur_day;
		select_item(it);
		if (cur_view == V_MONTH || cur_view == V_WEEK)
			cur_day = items[it].day;
		if (cur_day != day0) {
			laid_out = 0;
			win_redraw(&r_view);	/* the day's highlight moves too */
		} else if (!same) {
			/* only what changed: the two events' boxes, the day's details */
			redraw_occ(&was);
			redraw_occ(&sel_occ);
			if (cur_view == V_DAY && detail.w)
				win_redraw(&detail);
		}
		if (clicks == 2 || (same && clicks == 2))
			open_selected();
		return 1;
	}
	if (day < 0)
		return 1;
	if (cur_view == V_YEAR) {
		if (minute == -2)
			views_goto(day, V_MONTH);
		else if (clicks == 2)
			views_goto(day, V_DAY);
		else {
			cur_day = day;
			win_redraw(&r_view);
			win_redraw(&r_side);
		}
		return 1;
	}
	views_select_none();
	cur_day = day;
	win_status("");
	if (clicks == 2) {
		editor_new(day, minute, cur_view == V_MONTH ? 0 : (minute < 0 && my < grid.y));
		return 1;
	}
	laid_out = 0;
	win_redraw(&r_view);
	win_redraw(&r_side);
	(void)kstate;
	return 1;
}

/* the next/previous event in time, from the selected one */
static void select_next(int dir)
{
	int i, best = -1;
	if (!nitems)
		return;
	for (i = 0; i < nitems; i++) {
		OCC *o;
		if (items[i].kind != IT_EVENT)
			continue;
		o = &occ.o[items[i].occ];
		if (!sel_occ.ev) {
			best = i;
			if (dir > 0)
				break;
			continue;
		}
		if (o->ev == sel_occ.ev && o->start == sel_occ.start) {
			int j = i + dir;
			while (j >= 0 && j < nitems && items[j].kind != IT_EVENT)
				j += dir;
			if (j >= 0 && j < nitems)
				best = j;
			break;
		}
	}
	if (best >= 0) {
		/* only what changed, as a click does: the two events' boxes and
		   the day's details (the whole view took 0.8 s on an ST) */
		OCC was = sel_occ;
		select_item(best);
		redraw_occ(&was);
		redraw_occ(&sel_occ);
		if (cur_view == V_DAY && detail.w)
			win_redraw(&detail);
	}
}

/* ---------------- scrolling the hours ---------------- */

/* the hours by dy pixels: what is on the screen is moved, and only the
   strip that comes in is drawn; 0 at the end */
static int scroll_by(long dy)
{
	long ny = scroll_y + dy;
	GRECT a;
	if (ny > scroll_max())
		ny = scroll_max();
	if (ny < 0)
		ny = 0;
	dy = ny - scroll_y;
	if (!dy)
		return 0;
	scroll_y = ny;
	if (laid_out) {
		/* the timed boxes move with the hours (laid out again they
		   would come to the same: 20 ms of a line's 50 on an ST) */
		int i;
		for (i = 0; i < nitems; i++)
			if (items[i].timed)
				items[i].r.y -= (short)dy;
	}
	a = grid;
	a.x = r_view.x;		/* the hours on the left too */
	a.w = gutter + grid.w;
	win_scroll(&a, (short)dy);
	return 1;
}

static int button_held(void)
{
	short x, y, b, k;
	graf_mkstate(&x, &y, &b, &k);
	return b & 1;
}

/* an hour up (dir -1) or down, gliding; with hold, on for as long as
   the mouse button stays down. Five hours a second, each step as far as
   the time the one before took to draw, so a slow machine takes bigger
   steps rather than longer */
static void glide(int dir, int hold)
{
	long moved = 0, step;
	unsigned long last, now;
	layout();
	last = pf_ms() - 40;
	for (;;) {
		int held = hold && button_held();
		if (!held && moved >= hour_h)
			break;
		while ((now = pf_ms()) - last < 20UL)
			;		/* no more than 50 steps a second */
		step = (long)(now - last) * hour_h * 5 / 1000;
		last = now;
		if (step < 1)
			step = 1;
		if (step > hour_h)
			step = hour_h;
		if (!held && moved + step > hour_h)
			step = hour_h - moved;	/* a click: just the hour */
		if (!scroll_by(dir * step))
			break;
		moved += step;
	}
}

/* A key's hour glides a step at a time from the event loop
   (views_glide): each step its own short stretch, the keys and the other
   programs answered between them; the whole hour in one held an ST for
   half a second (the guidelines, Speed) */
static unsigned long glide_last;

int views_gliding(void)
{
	return glide_left != 0;
}

void views_glide(void)
{
	unsigned long now = pf_ms();
	long step, left = glide_left < 0 ? -glide_left : glide_left;
	if (!left)
		return;
	if (cur_view != V_DAY && cur_view != V_WEEK) {
		glide_left = 0;
		return;
	}
	layout();
	step = (long)(now - glide_last) * hour_h * 5 / 1000;	/* five hours a second */
	glide_last = now;
	if (step < 1)
		step = 1;
	if (step > hour_h)
		step = hour_h;
	if (step > left)
		step = left;
	if (!scroll_by(glide_left < 0 ? -step : step))
		glide_left = 0;		/* the top or the bottom */
	else
		glide_left += glide_left < 0 ? step : -step;
	if (!glide_left)
		win_sliders();		/* (not at each step: win_redraw) */
}

static void glide_key(int dir)
{
	if (!glide_left || (glide_left < 0) != (dir < 0)) {
		glide_left = 0;
		glide_last = pf_ms() - 40;
	}
	/* a key held (repeated) goes on gliding, no more than two hours ahead */
	if ((glide_left < 0 ? -glide_left : glide_left) < 2L * hour_h)
		glide_left += dir * (long)hour_h;
	views_glide();
}

/* the AES sends WM_ARROWED again while an arrow is held: those that
   came while we scrolled are left alone */
static unsigned long arrow_done;
static int arrow_was;

/* the window's scroll bar: 0 page up, 1 page down, 2 line up, 3 line
   down. The hours glide while an arrow is held, and go a page at a time
   while the bar is */
void views_arrow(short what)
{
	int dir = what == 1 || what == 3 ? 1 : -1;
	if (cur_view != V_DAY && cur_view != V_WEEK) {
		if (what <= 3)
			views_step(dir);
		return;
	}
	if (arrow_was && pf_ms() - arrow_done < 250UL)
		return;
	layout();
	if (what >= 2) {
		glide(dir, 1);
	} else {
		while (scroll_by(dir * (long)(grid.h - hour_h))) {
			unsigned long t = pf_ms();
			while (button_held() && pf_ms() - t < 250UL)
				;
			if (!button_held())
				break;
		}
	}
	arrow_was = 1;
	arrow_done = pf_ms();
}

/* the slider moved to pos (0..1000) */
void views_slide(short pos)
{
	layout();
	if (cur_view == V_DAY || cur_view == V_WEEK)
		scroll_by(scroll_max() * pos / 1000 - scroll_y);
}

/* the slider's size and place, 0..1000 */
void views_slider(long *size, long *pos)
{
	*size = 1000;
	*pos = 0;
	if (cur_view != V_DAY && cur_view != V_WEEK)
		return;
	layout();
	if (hour_h <= 0)
		return;
	*size = grid.h * 1000L / (24L * hour_h);
	if (*size > 1000)
		*size = 1000;
	if (scroll_max())
		*pos = scroll_y * 1000 / scroll_max();
}

int views_key(short kstate, short key)
{
	short scan = KEY_SCAN(key);
	int shift = (kstate & (K_LSHIFT | K_RSHIFT)) != 0;
	layout();
	switch (scan) {
	case 0x4b:		/* left */
	case 0x4d:		/* right */
		if (shift || cur_view == V_YEAR) {
			views_step(scan == 0x4b ? -1 : 1);
		} else {
			views_select_none();
			cur_day += scan == 0x4b ? -1 : 1;
			if (cur_day < first_day || cur_day >= first_day + ndays)
				views_goto(cur_day, cur_view);
			else
				win_redraw(0);
		}
		return 1;
	case 0x48:		/* up */
	case 0x50:		/* down */
		if (cur_view == V_DAY || cur_view == V_WEEK) {
			layout();
			glide_key(scan == 0x48 ? -1 : 1);
		} else {
			views_select_none();
			cur_day += scan == 0x48 ? -7 : 7;
			if (cur_day < first_day || cur_day >= first_day + ndays)
				views_goto(cur_day, cur_view);
			else
				win_redraw(0);
		}
		return 1;
	case 0x47:		/* Clr/Home */
		views_today();
		return 1;
	case 0x1c:		/* Return */
	case 0x72:
		if (sel_occ.ev)
			open_selected();
		else
			editor_new(cur_day, -1, 0);
		return 1;
	case 0x53:		/* Delete */
	case 0x0e:		/* Backspace */
		cmd_delete_selected();
		return 1;
	case 0x0f:		/* Tab: the next event */
		select_next(shift ? -1 : 1);
		return 1;
	case 0x01:		/* Esc */
		views_select_none();
		win_status("");
		win_redraw(&r_view);
		return 1;
	}
	return 0;
}
