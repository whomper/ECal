/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* A stand-in for EContact in the Hatari tests (the guidelines, 1.2: an
   app tests with no other AERA program; 4.1: PEOPLE). An accessory called
   ECONTACT.ACC, so the AES knows it as "ECONTACT" and ECal asks it, with
   common's own EContact side of the PEOPLE messages (people_ask.c):
     names:  the names it knows from the list below, "" for the rest;
     pick:   the people whose name starts with what ECal sent (the
             first one when it sent nothing), as a user choosing them
             in EContact's window would.
   Every question goes into C:\FAKECONT.TXT, a line each, for the test. */
#include <string.h>
#include <tos.h>
#include "gem.h"
#include "av.h"
#include "people_ask.h"

static const char *const people[][2] = {
	{ "grace.baker@example.net", "Grace Baker" },
	{ "omer.levi@example.org", "Omer Levi" },
};
enum { NPEOPLE = sizeof(people) / sizeof(people[0]) };

static void note(const char *kind, const char *asker, const char *rest)
{
	short h = (short)Fopen("C:\\FAKECONT.TXT", 1);
	if (h < 0)
		h = (short)Fcreate("C:\\FAKECONT.TXT", 0);
	if (h < 0)
		return;
	Fseek(0, h, 2);
	Fwrite(h, (long)strlen(kind), kind);
	Fwrite(h, (long)strlen(asker), asker);
	Fwrite(h, 1, " ");
	Fwrite(h, (long)strcspn(rest, "\r\n"), rest);
	Fwrite(h, 2, "\r\n");
	Fclose(h);
}

static const char *known(const char *addr)
{
	int i;
	for (i = 0; i < NPEOPLE; i++)
		if (!strcmp(people[i][0], addr))
			return people[i][1];
	return "";
}

static int starts(const char *name, const char *partial)
{
	while (*partial && *partial != '\r' && *partial != '\n') {
		char a = *name++, b = *partial++;
		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z')
			b += 'a' - 'A';
		if (a != b)
			return 0;
	}
	return 1;
}

static void question(const char *s, short from)
{
	char asker[16], addr[PEOPLE_ADDR];
	const char *rest, *p;
	int i;
	switch (people_question(s, asker, sizeof(asker), &rest)) {
	case PQ_NAMES:
		note("names:", asker, rest);
		people_answer(from, "names:", 0, 0);
		for (p = rest; (p = people_next(p, addr, sizeof(addr))) != 0; )
			people_answer(from, 0, addr, known(addr));
		people_answer(from, 0, 0, 0);
		break;
	case PQ_PICK:
		note("pick:", asker, rest);
		people_answer(from, "pick:", 0, 0);
		for (i = 0; i < NPEOPLE; i++)
			if (starts(people[i][1], rest)) {
				people_answer(from, 0, people[i][0], people[i][1]);
				if (!*rest || *rest == '\r' || *rest == '\n')
					break;		/* nothing typed: the first one */
			}
		people_answer(from, 0, 0, 0);
		break;
	}
}

int main(void)
{
	short msg[8], id = appl_init();
	AEVENT ev;
	const char *s;
	menu_register(id, "  EContact (test)");
	av_init("ECONTACT");
	people_init(0, 4096, 0, 0);
	for (;;) {
		short w = evnt_multi_(MU_MESAG | MU_TIMER, 0, 0, 0, 100, msg, &ev);
		if ((w & MU_MESAG) && av_message(msg, &s) == AV_PEOPLE && s)
			question(s, msg[1]);
		people_poll();
	}
	return 0;
}
