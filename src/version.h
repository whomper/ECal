/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/*
 * version.h - ECal's version: VERSION holds the last release (major.minor,
 * e.g. "1.97"), which every build shows as it is.
 * The Makefile writes version.c; tools/bump.py starts a release.
 */
#ifndef VERSION_H
#define VERSION_H
extern const char cal_version[];	/* "1.25" */
#endif
