/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people.c - EContact's people for EMail and ECal (see people.h).
 * Kept in EContact's repository (share/) and copied into the others.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "people.h"
#include "plat.h"
#include "util.h"

static char drive = 'C';
static char inf_override[200];

void people_drive(char letter)
{
	if (letter >= 'A' && letter <= 'Z')
		drive = letter;
}

void people_inf(const char *path)
{
	str_copy(inf_override, path, sizeof(inf_override));
}

/* CONTACTS.TXT, found through ECONTACT.INF's FOLDER= */
static int list_path(char *out, int size)
{
	char inf[20], *t, *k;
	long len;
	snprintf(inf, sizeof(inf), "%c:%cECONTACT.INF", drive, PF_SEP);
	t = pf_load(inf_override[0] ? inf_override : inf, &len);
	if (!t)
		return 0;
	out[0] = 0;
	for (k = t; (k = (char *)str_istr(k, "folder=")) != 0; k += 7)
		if (k == t || k[-1] == '\n') {
			char dir[160];
			int n = (int)strcspn(k + 7, "\r\n");
			if (n > (int)sizeof(dir) - 1)
				n = sizeof(dir) - 1;
			memcpy(dir, k + 7, n);
			dir[n] = 0;
			while (n > 3 && (dir[n - 1] == PF_SEP || dir[n - 1] == ' '))
				dir[--n] = 0;
			path_join(out, size, dir, "CONTACTS.TXT");
			break;
		}
	free(t);
	return out[0] != 0;
}

int people_available(void)
{
	char path[200];
	return list_path(path, sizeof(path)) && pf_exists(path);
}

static int matches(const char *a, const char *b, const char *text)
{
	return !*text || (a && str_istr(a, text)) || (b && str_istr(b, text));
}

static int add(PPICK **l, int *n, int *max, const char *label, const char *value, short group)
{
	PPICK *p;
	if (*n >= *max) {
		int m = *max ? *max * 2 : 32;
		PPICK *nl = realloc(*l, m * sizeof(PPICK));
		if (!nl)
			return 0;
		*l = nl;
		*max = m;
	}
	p = &(*l)[*n];
	str_copy(p->label, label, sizeof(p->label));
	p->value = strdup(value);
	p->group = group;
	if (!p->value)
		return 0;
	(*n)++;
	return 1;
}

static int cmp(const void *a, const void *b)
{
	const PPICK *x = a, *y = b;
	if (x->group != y->group)
		return y->group - x->group;
	return strcasecmp(x->label, y->label);
}

int people_pick_list(const char *text, PPICK **out)
{
	char path[200], *t, *line;
	long len;
	PPICK *l = 0;
	int n = 0, max = 0;
	*out = 0;
	if (!list_path(path, sizeof(path)) || !(t = pf_load(path, &len)))
		return 0;
	for (line = t; line && *line;) {
		char *nl = strchr(line, '\n'), *f[4];
		int k;
		if (nl)
			*nl = 0;
		if (nl && nl > line && nl[-1] == '\r')
			nl[-1] = 0;
		/* the fields between tabs */
		f[0] = line;
		for (k = 1; k < 4; k++) {
			f[k] = f[k - 1] ? strchr(f[k - 1], '\t') : 0;
			if (f[k])
				*f[k]++ = 0;
		}
		if (line[0] == 'P' && f[1] && f[2] && matches(f[1], f[2], text)) {
			char lab[160], val[200];
			if (f[1][0] && strcmp(f[1], f[2])) {
				snprintf(lab, sizeof(lab), "%s <%s>", f[1], f[2]);
				/* a name with a comma would split the To: line: quoted */
				if (strchr(f[1], ',') || strchr(f[1], '"'))
					snprintf(val, sizeof(val), "\"%s\" <%s>", f[1], f[2]);
				else
					snprintf(val, sizeof(val), "%s <%s>", f[1], f[2]);
			} else {
				snprintf(lab, sizeof(lab), "%s", f[2]);
				snprintf(val, sizeof(val), "%s", f[2]);
			}
			if (!add(&l, &n, &max, lab, val, 0))
				break;
		} else if (line[0] == 'G' && f[1] && f[2] && matches(f[1], f[2], text)) {
			char lab[120];
			int members = 1;
			const char *p;
			for (p = f[2]; *p; p++)
				members += *p == '>' && p[1] == ',';
			snprintf(lab, sizeof(lab), "%s (%d %s)", f[1], members, members == 1 ? "person" : "people");
			if (!add(&l, &n, &max, lab, f[2], 1))
				break;
		}
		line = nl ? nl + 1 : 0;
	}
	free(t);
	if (n > 1)
		qsort(l, n, sizeof(PPICK), cmp);
	*out = l;
	return n;
}

void people_pick_free(PPICK *l, int n)
{
	int i;
	for (i = 0; i < n; i++)
		free(l[i].value);
	free(l);
}

/* where the last address on the line starts (after the last comma
   outside quotes and <>) */
static const char *last_part(const char *line)
{
	const char *p, *start = line;
	int q = 0, a = 0;
	for (p = line; *p; p++) {
		if (*p == '"')
			q = !q;
		else if (*p == '<' && !q)
			a = 1;
		else if (*p == '>' && !q)
			a = 0;
		else if (*p == ',' && !q && !a)
			start = p + 1;
	}
	while (*start == ' ')
		start++;
	return start;
}

void people_partial(const char *line, char *out, int size)
{
	const char *s = last_part(line);
	str_copy(out, s, size);
	str_trim(out);
	/* a whole address already: nothing to complete */
	if (strchr(out, '<') || strchr(out, '@'))
		out[0] = 0;
}

char *people_complete(const char *line, const char *value)
{
	const char *s = last_part(line);
	char part[100];
	long keep;
	SBUF b;
	people_partial(line, part, sizeof(part));
	if (part[0])
		keep = (long)(s - line);
	else
		keep = (long)strlen(line);
	sb_init(&b);
	sb_add(&b, line, keep);
	/* trailing spaces off, then ", " between addresses */
	while (b.len && b.s[b.len - 1] == ' ')
		b.len--;
	if (b.len && b.s[b.len - 1] != ',' && b.s[b.len - 1] != ':' && !part[0])
		sb_addc(&b, ',');
	if (b.len && (b.s[b.len - 1] == ',' || b.s[b.len - 1] == ':'))
		sb_addc(&b, ' ');
	sb_adds(&b, value);
	return sb_steal(&b);
}
