/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * people.h - EContact's people for other programs: EMail's address book
 * and ECal's invitations read them from here. Copy people.c and people.h
 * into the program's src/; they need only its util.c and plat.h.
 *
 * EContact writes CONTACTS.TXT in its folder whenever the contacts
 * change; ECONTACT.INF in the root of the boot drive says where that
 * folder is (FOLDER=). The file is Atari text, one line each:
 *   P<tab>name<tab>email<tab>label      a person's address
 *   G<tab>group<tab>Name <email>, Name <email>...
 */
#ifndef PEOPLE_H
#define PEOPLE_H

/* one choice in a list: what it shows, and what goes into the To: or
   Invitees line ("Name <email>", or all of a group's) */
typedef struct {
	char label[100];
	char *value;		/* malloc'ed */
	short group;		/* a group: every member at once */
} PPICK;

/* where to look: the root of the boot drive, e.g. 'C' */
void  people_drive(char letter);
/* ECONTACT.INF somewhere else (the tests) */
void  people_inf(const char *path);
/* 1 if EContact's list is there */
int   people_available(void);
/* everyone (and every group) matching text in name or address, groups
   first, then by name; text "" for all. *out is malloc'ed; returns how
   many (0 if none, or no EContact) */
int   people_pick_list(const char *text, PPICK **out);
void  people_pick_free(PPICK *l, int n);
/* the unfinished name at the end of a To: line, "Bob <b@x>, gr" -> "gr";
   "" when the line ends with a whole address or a comma */
void  people_partial(const char *line, char *out, int size);
/* the line with that unfinished name replaced by value (or value added
   after a comma); malloc'ed */
char *people_complete(const char *line, const char *value);

#endif
