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
#include "../src/util.h"

long cur_day;
short cur_view = V_MONTH;
OCC sel_occ;
short day_scroll = 8;

static OCCLIST occ;
static int occ_valid;
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
static GRECT detail;		/* day view: the selected event's details */

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
	day_scroll = hour;
	laid_out = 0;		/* the event boxes were placed for the old hour */
}

void views_invalidate(void)
{
	occ_valid = 0;
	laid_out = 0;
}

void views_select_none(void)
{
	memset(&sel_occ, 0, sizeof(sel_occ));
}

long views_hours_visible(void)
{
	long h = hour_h > 0 ? (r_view.h - head_h - allday_h) / hour_h : 12;
	return h < 1 ? 1 : h;
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

static void collect(void)
{
	long from, n;
	range(&from, &n);
	if (occ_valid && occ.from == from * MIN_PER_DAY && occ.to == (from + n) * MIN_PER_DAY)
		return;
	occ_collect(&occ, from * MIN_PER_DAY, (from + n) * MIN_PER_DAY);
	occ_valid = 1;
	laid_out = 0;
	/* keep the selection if its occurrence is still there */
	occ_find_again();
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

static int is_long(const OCC *o)
{
	return o->ev->allday || o->end - o->start >= MIN_PER_DAY ||
	       ct_days(o->start) != ct_days(o->end - 1);
}

static long occ_first_day(const OCC *o)
{
	return ct_days(o->start);
}

static long occ_last_day(const OCC *o)
{
	return ct_days(o->end > o->start ? o->end - 1 : o->start);
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
	memset(used, 0, sizeof(used));
	for (i = 0; i < occ.n; i++) {
		OCC *o = &occ.o[i];
		long a, b;
		short l;
		lane[i] = -1;
		if (!is_long(o))
			continue;
		a = occ_first_day(o) - d0;
		b = occ_last_day(o) - d0;
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

static short *lane_buf(void)
{
	static short *buf;
	static int cap;
	if (occ.n > cap) {
		free(buf);
		cap = occ.n + 32;
		buf = malloc(cap * sizeof(short));
	}
	return buf;
}

static void layout_month(void)
{
	short r, c, line_h = ch + 1;
	short *lane = lane_buf();
	head_h = ch + 4;
	rows = (short)(ndays / 7);
	cols = 7;
	grid.x = r_view.x;
	grid.y = r_view.y + head_h;
	grid.w = r_view.w;
	grid.h = r_view.h - head_h;
	cell_w = grid.w / 7;
	cell_h = grid.h / rows;
	if (!lane)
		return;
	for (r = 0; r < rows; r++) {
		long d0 = first_day + r * 7;
		short maxl = (cell_h - ch - 3) / line_h, used = 0;
		int i;
		short count[7];
		if (maxl < 1)
			maxl = 1;
		used = assign_lanes(d0, 7, lane, maxl);
		memset(count, 0, sizeof(count));
		/* long events: bars across the row */
		for (i = 0; i < occ.n; i++) {
			OCC *o = &occ.o[i];
			long a, b;
			GRECT box;
			if (lane[i] < 0)
				continue;
			a = occ_first_day(o) - d0;
			b = occ_last_day(o) - d0;
			if (a < 0)
				a = 0;
			if (b > 6)
				b = 6;
			box.x = grid.x + (short)a * cell_w + 1;
			box.y = grid.y + r * cell_h + ch + 3 + lane[i] * line_h;
			box.w = (short)(b - a + 1) * cell_w - 2;
			box.h = line_h - 1;
			add_item(&box, IT_EVENT, (short)i, d0 + a, occ_first_day(o) < d0 + a);
		}
		for (c = 0; c < 7; c++)
			count[c] = used;
		/* lanes taken by bars on each day: the timed events go below */
		for (c = 0; c < 7; c++) {
			short top = 0;
			for (i = 0; i < occ.n; i++)
				if (lane[i] >= 0 && occ_first_day(&occ.o[i]) <= d0 + c &&
				    occ_last_day(&occ.o[i]) >= d0 + c && lane[i] + 1 > top)
					top = lane[i] + 1;
			count[c] = top;
		}
		/* timed events of each day, one line each */
		for (c = 0; c < 7; c++) {
			long day = d0 + c;
			short n = 0, total = 0, room = maxl - count[c];
			for (i = 0; i < occ.n; i++)
				if (!is_long(&occ.o[i]) && ct_days(occ.o[i].start) == day)
					total++;
			for (i = 0; i < occ.n && room > 0; i++) {
				GRECT box;
				if (is_long(&occ.o[i]) || ct_days(occ.o[i].start) != day)
					continue;
				box.x = grid.x + c * cell_w + 1;
				box.y = grid.y + r * cell_h + ch + 3 + (count[c] + n) * line_h;
				box.w = cell_w - 2;
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

/* timed events of one day column, side by side where they overlap */
static void layout_column(long day, GRECT *col)
{
	short idx[80], colof[80], ncol[80], n = 0, i, j;
	CTIME d0 = day * MIN_PER_DAY;
	for (i = 0; i < occ.n && n < 80; i++) {
		OCC *o = &occ.o[i];
		if (is_long(o) || ct_days(o->start) != day)
			continue;
		idx[n++] = i;
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
		short w = (col->w - 3) / ncol[i];
		if (e - s < 20)
			e = s + 20;	/* short ones stay readable */
		if (e > 1440)
			e = 1440;
		box.x = col->x + 1 + colof[i] * w;
		box.w = w - 1;
		box.y = col->y + (short)((s - day_scroll * 60L) * hour_h / 60);
		box.h = (short)((e - s) * hour_h / 60) - 1;
		if (box.h < ch / 2)
			box.h = ch / 2;
		if (add_item(&box, IT_EVENT, idx[i], day, 0))
			items[nitems - 1].timed = 1;
	}
}

static void layout_days(void)
{
	short c, line_h = ch + 2, lanes, maxl = 3;
	short *lane = lane_buf();
	long vis;
	head_h = (opt.hebdates ? 2 : 1) * ch + 6;
	gutter = 7 * cw;
	hour_h = ch >= 16 ? 2 * ch + 4 : 2 * ch + 4;
	cols = (short)ndays;
	rows = 1;
	detail.w = 0;
	grid.x = r_view.x + gutter;
	grid.w = r_view.w - gutter;
	if (cur_view == V_DAY && r_view.w > 70 * cw) {
		/* the day view shows the selected event's details on the right */
		detail.w = r_view.w * 2 / 5;
		detail.x = r_view.x + r_view.w - detail.w;
		detail.y = r_view.y;
		detail.h = r_view.h;
		grid.w -= detail.w;
	}
	cell_w = grid.w / cols;
	if (!lane)
		return;
	lanes = assign_lanes(first_day, ndays, lane, maxl);
	allday_h = (lanes ? lanes : 1) * line_h + 4;
	grid.y = r_view.y + head_h + allday_h;
	grid.h = r_view.h - head_h - allday_h;
	vis = views_hours_visible();
	if (day_scroll > 24 - vis)
		day_scroll = (short)(24 - vis);
	if (day_scroll < 0)
		day_scroll = 0;
	{
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
			box.x = grid.x + (short)a * cell_w + 1;
			box.y = r_view.y + head_h + 2 + lane[i] * line_h;
			box.w = (short)(b - a + 1) * cell_w - 2;
			box.h = line_h - 1;
			add_item(&box, IT_EVENT, (short)i, first_day + a, occ_first_day(o) < first_day + a);
		}
	}
	for (c = 0; c < cols; c++) {
		GRECT col;
		col.x = grid.x + c * cell_w;
		col.y = grid.y;
		col.w = cell_w;
		col.h = grid.h;
		layout_column(first_day + c, &col);
	}
}

static void layout(void)
{
	collect();
	if (laid_out)
		return;
	nitems = 0;
	range(&first_day, &ndays);
	allday_h = head_h = 0;
	if (cur_view == V_MONTH)
		layout_month();
	else if (cur_view == V_WEEK || cur_view == V_DAY)
		layout_days();
	laid_out = 1;
}

/* ---------------- drawing ---------------- */

static void event_box(const ITEM *it, GRECT *clip, int bar)
{
	OCC *o = &occ.o[it->occ];
	EVENT *e = o->ev;
	PEN p;
	GRECT r = it->r, strip;
	int sel = sel_occ.ev == e && sel_occ.start == o->start;
	short tx = r.x + 4, ty = r.y, cols_, fl = 0;
	char line[160], tm[16];
	const char *title = e->summary ? e->summary : "New Event";
	pen_of(e->cal, &p);
	clip_on(clip);
	if (sel) {
		fill(&r, p.color == 6 || p.color == 14 || p.color == 8 ? 1 : p.color);
		fl = TX_INVERSE;
	} else if (is_long(o)) {
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
		fl |= TX_LIGHT;
	cols_ = (r.w - 6) / cw;
	if (cols_ <= 0)
		return;
	if (bar) {
		/* one line: (time) title */
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
		text_at(tx, ty, title, (long)strlen(title), cols_, fl | TX_RIGHT);
		k++;
		if (k < lines) {
			char t2[16];
			fmt_time(a.hour, a.min, opt.h24, tm, sizeof(tm));
			fmt_time(b.hour, b.min, opt.h24, t2, sizeof(t2));
			snprintf(line, sizeof(line), "%s-%s", tm, t2);
			text_at(tx, ty + k * ch, line, (long)strlen(line), cols_, fl | TX_LTR);
			k++;
		}
		if (k < lines && e->location) {
			text_at(tx, ty + k * ch, e->location, (long)strlen(e->location), cols_, fl | TX_RIGHT);
			k++;
		}
		if (k < lines && e->rr)
			text_at(tx, ty + k * ch, "\x0F repeats", 9, cols_, fl | TX_LTR | TX_LIGHT);
	}
}

static void draw_items(GRECT *clip, int bars)
{
	int i;
	for (i = 0; i < nitems; i++) {
		GRECT r = items[i].r;
		if (!rect_and(&r, clip))
			continue;
		if (items[i].kind == IT_MORE) {
			char t[20];
			GRECT c = *clip;
			rect_and(&c, &items[i].r);
			clip_on(&c);
			snprintf(t, sizeof(t), "+%d more", items[i].occ);
			text_at(items[i].r.x + 4, items[i].r.y, t, (long)strlen(t), items[i].r.w / cw, TX_LTR | TX_LIGHT);
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
	/* weekday names */
	for (c = 0; c < 7; c++) {
		const char *wn = cell_w >= 10 * cw ? wday_names[(c + opt.weekstart) % 7] : wday_short[(c + opt.weekstart) % 7];
		short wd = (c + opt.weekstart) % 7;
		text_color(wd == 6 || (opt.weekstart == 0 && wd == 5 && 0) ? 1 : 1);
		text_at(grid.x + c * cell_w + 4, r_view.y + 2, wn, (long)strlen(wn), cell_w / cw - 1,
			TX_LTR | (wd == 6 ? TX_BOLD : 0));
	}
	line_col(r_view.x, grid.y - 1, r_view.x + r_view.w - 1, grid.y - 1, 1);
	for (r = 0; r < rows; r++)
		for (c = 0; c < 7; c++) {
			long day = first_day + r * 7 + c;
			int yy, m, d;
			GRECT cell;
			cell.x = grid.x + c * cell_w;
			cell.y = grid.y + r * cell_h;
			cell.w = c == 6 ? grid.x + grid.w - cell.x : cell_w;
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
			{
				char n[4];
				GRECT nb;
				snprintf(n, sizeof(n), "%d", d);
				nb.x = cell.x + 2;
				nb.y = cell.y + 1;
				nb.w = ((short)strlen(n) + 1) * cw;
				nb.h = ch + 1;
				if (day == today()) {
					fill(&nb, red_color());
					text_at(nb.x + cw / 2, nb.y, n, (long)strlen(n), 3, TX_INVERSE | TX_BOLD | TX_LTR);
				} else {
					text_at(nb.x + cw / 2, nb.y, n, (long)strlen(n), 3,
						TX_LTR | (m != m0 ? TX_LIGHT : 0) | (d == 1 ? TX_BOLD : 0));
				}
				if (d == 1 && cell.w >= 9 * cw)
					text_at(nb.x + nb.w, nb.y, month_short[m - 1], 3, 3, TX_LTR | TX_BOLD);
			}
			if (opt.hebdates && cell.w >= 7 * cw) {
				HDATE h;
				char hs[40];
				short n;
				hebrew_from_days(day, &h);
				if ((h.day == 1 || (r == 0 && c == 0)) && cell.w >= 13 * cw)
					hebrew_format(&h, 0, hs, sizeof(hs));
				else
					hebrew_numeral(h.day, hs, sizeof(hs));
				n = (short)strlen(hs);
				text_at(cell.x + cell.w - (n + 1) * cw, cell.y + 1, hs, n, n, TX_LIGHT);
			}
		}
	/* the grid: whole lines rather than four sides per cell (each VDI
	   call costs an 8 MHz ST milliseconds) */
	clip_on(clip);
	for (r = 1; r <= rows; r++) {
		short y = (r == rows ? grid.y + grid.h : grid.y + r * cell_h) - 1;
		line_col(grid.x, y, grid.x + grid.w - 1, y, grey_color());
	}
	for (c = 1; c < 7; c++) {
		short x = grid.x + c * cell_w - 1;
		line_col(x, grid.y, x, grid.y + grid.h - 1, grey_color());
	}
	draw_items(clip, 1);
}

static void draw_details(GRECT *clip)
{
	GRECT r = detail, c = *clip;
	short x = r.x + cw, y = r.y + ch / 2, w = (r.w - 2 * cw) / cw;
	char line[200];
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
				text_at(x, y + (4 + (short)i) * ch, hint + br[i], e - br[i], w, TX_LTR);
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
			text_at(x, y, line, (long)strlen(line), w, TX_LTR | TX_LIGHT);
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
		text_at(x, y, line, (long)strlen(line), w, TX_LTR | TX_LIGHT);
		y += ch + ch / 2;
		if (e->notes) {
			nb = wrap_text(e->notes, (long)strlen(e->notes), w, br, 40);
			for (i = 0; i < nb && y + ch <= r.y + r.h; i++) {
				long end = i + 1 < nb ? br[i + 1] : (long)strlen(e->notes);
				while (end > br[i] && (e->notes[end - 1] == '\n' || e->notes[end - 1] == ' '))
					end--;
				text_at(x, y, e->notes + br[i], end - br[i], w, TX_RIGHT);
				y += ch;
			}
		}
		if (e->url && y + ch <= r.y + r.h)
			text_at(x, y, e->url, (long)strlen(e->url), w, TX_LTR | TX_UNDER);
	}
}

static void draw_days(GRECT *clip)
{
	short c, h;
	long t = today();
	CTIME now = now_view();
	GRECT g;
	clip_on(clip);
	/* the day headers */
	for (c = 0; c < cols; c++) {
		long day = first_day + c;
		DT d;
		char s[40];
		short x = grid.x + c * cell_w, n;
		ct_split(day * MIN_PER_DAY, &d);
		if (cur_view == V_DAY)
			snprintf(s, sizeof(s), "%s %d %s", wday_names[d.wday], d.day, month_short[d.mon - 1]);
		else if (cell_w >= 8 * cw)
			snprintf(s, sizeof(s), "%s %d", wday_short[d.wday], d.day);
		else
			snprintf(s, sizeof(s), "%d", d.day);
		n = (short)strlen(s);
		if (day == t || day == cur_day) {
			/* today red, the selected day black, white letters: as in
			   the small month on the left */
			GRECT b;
			b.x = x + 2;
			b.y = r_view.y + 2;
			b.w = (n + 1) * cw;
			b.h = ch + 1;
			fill(&b, day == t ? red_color() : 1);
			text_at(x + 2 + cw / 2, r_view.y + 2, s, n, n, TX_INVERSE | TX_LTR);
		} else {
			text_at(x + 2 + cw / 2, r_view.y + 2, s, n, cell_w / cw - 1, TX_LTR);
		}
		if (opt.hebdates) {
			HDATE hd;
			char hs[40];
			hebrew_from_days(day, &hd);
			hebrew_format(&hd, cur_view == V_DAY, hs, sizeof(hs));
			n = (short)strlen(hs);
			if (n * cw > cell_w - cw) {
				hebrew_numeral(hd.day, hs, sizeof(hs));
				n = (short)strlen(hs);
			}
			text_at(x + 2 + cw / 2, r_view.y + 3 + ch, hs, n, cell_w / cw - 1, TX_LIGHT);
		}
		if (c)
			line_col(x, r_view.y, x, r_view.y + r_view.h - 1, grey_color());
	}
	line_col(r_view.x, r_view.y + head_h - 1, grid.x + grid.w - 1, r_view.y + head_h - 1, 1);
	text_at(r_view.x + 2, r_view.y + head_h + 2, "all-day", 7, gutter / cw, TX_LIGHT | TX_LTR);
	line_col(r_view.x, grid.y - 1, grid.x + grid.w - 1, grid.y - 1, 1);
	/* hours */
	g = grid;
	g.x = r_view.x;
	g.w = gutter + grid.w;
	if (rect_and(&g, clip)) {
		clip_on(&g);
		for (h = day_scroll; h <= 24; h++) {
			short y = grid.y + (h - day_scroll) * hour_h;
			char s[12];
			if (y > grid.y + grid.h)
				break;
			if (h < 24 && h > day_scroll) {
				fmt_time(h, 0, opt.h24, s, sizeof(s));
				text_at(r_view.x + 2, y - ch / 2, s, (long)strlen(s), gutter / cw - 1, TX_LTR | TX_LIGHT);
			}
			line_col(grid.x, y, grid.x + grid.w - 1, y, grey_color());
			if (hour_h >= 2 * ch && ncolors >= 16)
				line_col(grid.x, y + hour_h / 2, grid.x + grid.w - 1, y + hour_h / 2, 8);
		}
		for (c = 1; c < cols; c++)
			line_col(grid.x + c * cell_w, grid.y, grid.x + c * cell_w, grid.y + grid.h - 1, grey_color());
	}
	draw_items(clip, 0);
	/* now: a red line across today */
	if (t >= first_day && t < first_day + ndays) {
		long m = now - t * MIN_PER_DAY - day_scroll * 60L;
		short y = grid.y + (short)(m * hour_h / 60), x = grid.x + (short)(t - first_day) * cell_w;
		if (m >= 0 && y < grid.y + grid.h) {
			GRECT dot, cc = grid;
			if (rect_and(&cc, clip)) {
				clip_on(&cc);
				line_col(x, y, x + cell_w - 1, y, red_color());
				line_col(x, y + 1, x + cell_w - 1, y + 1, red_color());
				dot.x = x;
				dot.y = y - 2;
				dot.w = 5;
				dot.h = 6;
				fill(&dot, red_color());
			}
		}
	}
	if (detail.w)
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
static void year_geom(short *mw, short *mh, short *pc)
{
	int level;
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
}

static void draw_year(GRECT *clip)
{
	static unsigned char busy_[372];
	short mw, mh, pc, m;
	int y0, m0, d0, i;
	long jan1, t = today();
	civil_from_days(cur_day, &y0, &m0, &d0);
	jan1 = days_from_civil(y0, 1, 1);
	memset(busy_, 0, sizeof(busy_));
	for (i = 0; i < occ.n; i++) {
		long a = occ_first_day(&occ.o[i]) - jan1, b = occ_last_day(&occ.o[i]) - jan1;
		for (; a <= b && a < 372; a++)
			if (a >= 0)
				busy_[a] = 1;
	}
	year_geom(&mw, &mh, &pc);
	clip_on(clip);
	for (m = 0; m < 12; m++) {
		short ox = r_view.x + (m % pc) * mw + (mw - 21 * cw) / 2, oy = r_view.y + (m / pc) * mh + ch / 2;
		long first = days_from_civil(y0, m + 1, 1), d;
		short off = (short)((wday_of(first) - opt.weekstart + 7) % 7), k, dim = (short)days_in_month(y0, m + 1);
		text_at(ox, oy, month_names[m], (long)strlen(month_names[m]), 20,
			TX_BOLD | TX_LTR | (m + 1 == m0 ? TX_UNDER : 0));
		for (k = 0; k < 7; k++)
			text_at(ox + k * 3 * cw, oy + ch + 2, wday_short[(k + opt.weekstart) % 7], 2, 2, TX_LTR | (yfont ? 0 : TX_LIGHT));
		/* a week row at a time, consecutive days of the same look in one
		   piece of text: on an 8 MHz ST each text call costs milliseconds */
		{
			char run[24];
			short rl = 0, rx = 0, ry = 0, rflags = -1;
			for (k = 0; k <= dim; k++) {
				short col = (off + k) % 7, row = (off + k) / 7, flags = 0;
				short x = ox + col * 3 * cw, y = oy + (row + 2) * ch + 4;
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
						/* the small fonts are too small for light and underlined text */
						flags = busy_[d - jan1] ? (yfont ? TX_BOLD : TX_BOLD | TX_UNDER) : (yfont ? 0 : TX_LIGHT);
					}
				}
				if (rl && (k == dim || flags != rflags || col == 0 || (rflags & TX_INVERSE))) {
					text_at(rx, ry, run, rl, rl, TX_LTR | rflags);
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
				run[rl++] = (char)((k + 1) >= 10 ? '0' + (k + 1) / 10 : ' ');
				run[rl++] = (char)('0' + (k + 1) % 10);
			}
		}
	}
}

void views_draw(GRECT *clip)
{
	GRECT r = r_view;
	if (!rect_and(&r, clip))
		return;
	today_cache = ct_days(now_view());
	layout();
	clip_on(&r);
	fill(&r, 0);
	switch (cur_view) {
	case V_MONTH: draw_month(&r); break;
	case V_YEAR: draw_year(&r); year_font(0); break;
	default: draw_days(&r); break;
	}
	today_cache = -1;
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

static void changed_range(void)
{
	char t[200];
	laid_out = 0;
	win_redraw(0);
	views_title(t, sizeof(t));
	(void)t;
}

void views_goto(long day, short view)
{
	cur_day = day;
	if (view != cur_view) {
		cur_view = view;
		opt.view = view;
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
		long vis = views_hours_visible();
		if (h < day_scroll || h >= day_scroll + vis - 1)
			day_scroll = h < 0 ? 0 : h;
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
	col = (mx - (r_view.x + (m % pc) * mw + (mw - 21 * cw) / 2)) / (3 * cw);
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
		short c = (mx - grid.x) / cell_w, r = (my - grid.y) / cell_h;
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
		short c = (mx - grid.x) / cell_w;
		if (mx < grid.x || c < 0 || c >= cols)
			return -1;
		if (minute && my >= grid.y)
			*minute = (short)(day_scroll * 60L + (long)(my - grid.y) * 60 / hour_h) / 30 * 30;
		return first_day + c;
	}
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

static void context_menu(short mx, short my, int it, long day, short minute)
{
	static const char *const ev_items[] = { "  Open...", "  Duplicate", "-", "  Delete..." };
	static const char *const day_items[] = { "  New event...", "  New all-day event", "-",
						 "  Show this day", "  Show this week", "  Today" };
	short k;
	if (it >= 0 && items[it].kind == IT_EVENT) {
		select_item(it);
		win_redraw(&r_view);
		k = win_popup(mx, my, ev_items, cal_writable(&cals[sel_occ.ev->cal]) ? 4 : 1);
		if (k == 0)
			open_selected();
		else if (k == 1) {
			EVENT *c = ev_dup(sel_occ.ev);
			if (c) {
				CTIME d = c->end - c->start;
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
		} else if (k == 3)
			cmd_delete_selected();
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
	if (it >= 0) {
		int same = sel_occ.ev == occ.o[items[it].occ].ev && sel_occ.start == occ.o[items[it].occ].start;
		select_item(it);
		if (cur_view == V_MONTH || cur_view == V_WEEK)
			cur_day = items[it].day;
		laid_out = 0;
		win_redraw(&r_view);
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
		select_item(best);
		win_redraw(&r_view);
	}
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
			long vis = views_hours_visible();
			short ns = day_scroll + (scan == 0x48 ? -1 : 1);
			if (ns >= 0 && ns <= 24 - vis) {
				day_scroll = ns;
				laid_out = 0;
				win_redraw(&r_view);
			}
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
