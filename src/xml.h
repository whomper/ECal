/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * xml.h - just enough XML for WebDAV answers: a pull scanner that gives
 * start tags, end tags and text. Namespace prefixes are dropped
 * ("d:href", "D:href" and "href" are all "href"): CalDAV servers name
 * the same elements with whatever prefixes they like, and no two of
 * the elements we read share a local name.
 */
#ifndef XML_H
#define XML_H

enum { X_EOF, X_START, X_END, X_TEXT, X_EMPTY };	/* X_EMPTY: <tag/> */

typedef struct {
	const char *p, *end;
	char name[48];		/* local name of the tag */
	char *text;		/* X_TEXT: decoded text (valid until the next call) */
	long textlen;
	char *buf;
	long bufcap;
	char attrs[200];	/* the raw attributes of the last start tag */
} XMLP;

void xml_init(XMLP *x, const char *s, long n);
int  xml_next(XMLP *x);
void xml_done(XMLP *x);
/* decode &amp; &lt; &#1488; ... in place (result is UTF-8); returns the new length */
long xml_decode(char *s, long n);
/* XML-escape s into out */
void xml_escape(const char *s, char *out, int size);

#endif
