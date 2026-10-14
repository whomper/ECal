/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Erez Yaary */
/*
 * xml.c - a forgiving XML pull scanner for WebDAV.
 */
#include <stdlib.h>
#include <string.h>
#include "xml.h"
#include "util.h"

void xml_init(XMLP *x, const char *s, long n)
{
	memset(x, 0, sizeof(*x));
	x->p = s;
	x->end = s + n;
}

void xml_done(XMLP *x)
{
	free(x->buf);
	x->buf = 0;
}

static int put_utf8(char *o, unsigned long u)
{
	if (u < 0x80) {
		o[0] = (char)u;
		return 1;
	}
	if (u < 0x800) {
		o[0] = (char)(0xC0 | (u >> 6));
		o[1] = (char)(0x80 | (u & 0x3F));
		return 2;
	}
	if (u < 0x10000) {
		o[0] = (char)(0xE0 | (u >> 12));
		o[1] = (char)(0x80 | ((u >> 6) & 0x3F));
		o[2] = (char)(0x80 | (u & 0x3F));
		return 3;
	}
	o[0] = (char)(0xF0 | (u >> 18));
	o[1] = (char)(0x80 | ((u >> 12) & 0x3F));
	o[2] = (char)(0x80 | ((u >> 6) & 0x3F));
	o[3] = (char)(0x80 | (u & 0x3F));
	return 4;
}

long xml_decode(char *s, long n)
{
	long r = 0, w = 0;
	while (r < n) {
		if (s[r] == '&') {
			const char *semi = memchr(s + r, ';', n - r < 12 ? n - r : 12);
			if (semi) {
				long len = semi - (s + r);
				const char *e = s + r + 1;
				unsigned long u = 0;
				int ok = 1;
				long nl = len - 1;	/* the name, without '&' */
				if (nl == 3 && !strncmp(e, "amp", 3)) u = '&';
				else if (nl == 2 && !strncmp(e, "lt", 2)) u = '<';
				else if (nl == 2 && !strncmp(e, "gt", 2)) u = '>';
				else if (nl == 4 && !strncmp(e, "quot", 4)) u = '"';
				else if (nl == 4 && !strncmp(e, "apos", 4)) u = '\'';
				else if (nl > 1 && e[0] == '#') {
					if (e[1] == 'x' || e[1] == 'X')
						u = strtoul(e + 2, 0, 16);
					else
						u = strtoul(e + 1, 0, 10);
				} else
					ok = 0;
				if (ok) {
					w += put_utf8(s + w, u);
					r += len + 1;
					continue;
				}
			}
		}
		s[w++] = s[r++];
	}
	s[w] = 0;
	return w;
}

void xml_escape(const char *s, char *out, int size)
{
	int o = 0;
	for (; *s && o < size - 7; s++) {
		const char *rep = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" :
				  *s == '"' ? "&quot;" : 0;
		if (rep) {
			strcpy(out + o, rep);
			o += (int)strlen(rep);
		} else {
			out[o++] = *s;
		}
	}
	out[o] = 0;
}

static int text_room(XMLP *x, long need)
{
	if (need + 1 > x->bufcap) {
		char *n = realloc(x->buf, need + 256);
		if (!n)
			return 0;
		x->buf = n;
		x->bufcap = need + 256;
	}
	return 1;
}

int xml_next(XMLP *x)
{
	const char *p = x->p;
	while (p < x->end) {
		if (*p != '<' || (p + 9 <= x->end && !strncmp(p, "<![CDATA[", 9))) {
			/* text up to the next tag; CDATA sections are part of it */
			long n = 0, i;
			for (;;) {
				const char *q = memchr(p, '<', x->end - p);
				long k = q ? q - p : x->end - p;
				if (!text_room(x, n + k))
					return X_EOF;
				memcpy(x->buf + n, p, k);
				n += xml_decode(x->buf + n, k);
				p += k;
				if (p + 9 <= x->end && !strncmp(p, "<![CDATA[", 9)) {
					const char *e = p + 9, *z = e;
					while (z + 3 <= x->end && strncmp(z, "]]>", 3))
						z++;
					if (!text_room(x, n + (z - e)))
						return X_EOF;
					memcpy(x->buf + n, e, z - e);
					n += z - e;
					p = z + 3 <= x->end ? z + 3 : x->end;
					continue;
				}
				break;
			}
			x->p = p;
			x->buf[n] = 0;
			/* whitespace between tags isn't text */
			for (i = 0; i < n; i++)
				if (x->buf[i] != ' ' && x->buf[i] != '\n' && x->buf[i] != '\r' && x->buf[i] != '\t')
					break;
			if (i == n)
				continue;
			x->text = x->buf;
			x->textlen = n;
			return X_TEXT;
		}
		/* a tag */
		if (p + 4 <= x->end && !strncmp(p, "<!--", 4)) {
			const char *e = p + 4;
			while (e + 3 <= x->end && strncmp(e, "-->", 3))
				e++;
			p = e + 3;
			continue;
		}
		if (p + 1 < x->end && (p[1] == '?' || p[1] == '!')) {
			const char *e = memchr(p, '>', x->end - p);
			p = e ? e + 1 : x->end;
			continue;
		}
		{
			int end = p[1] == '/', o = 0, empty;
			const char *s = p + 1 + end, *e = memchr(p, '>', x->end - p), *colon = 0, *n;
			if (!e) {
				x->p = x->end;
				return X_EOF;
			}
			empty = e[-1] == '/';
			for (n = s; n < e && *n != ' ' && *n != '/' && *n != '\t' && *n != '\r' && *n != '\n'; n++)
				if (*n == ':')
					colon = n;
			for (s = colon ? colon + 1 : s; s < n && o < (int)sizeof(x->name) - 1; s++)
				x->name[o++] = *s;
			x->name[o] = 0;
			{
				long al = (e - empty) - n;
				if (al < 0)
					al = 0;
				if (al > (long)sizeof(x->attrs) - 1)
					al = sizeof(x->attrs) - 1;
				memcpy(x->attrs, n, al);
				x->attrs[al] = 0;
			}
			x->p = e + 1;
			return end ? X_END : empty ? X_EMPTY : X_START;
		}
	}
	x->p = x->end;
	return X_EOF;
}
