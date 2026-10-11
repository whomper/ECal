/*
 * version.h - ECal's version: CAL_VERSION is set by hand for each
 * release; the build number after it counts every build (BUILD, advanced
 * by tools/bump.py from the Makefile), e.g. "1.1.17".
 */
#ifndef VERSION_H
#define VERSION_H
#define CAL_VERSION "1.1"
extern const char cal_version[];	/* "1.1.17" */
extern const int cal_build;
extern const char cal_built[];		/* the date of the build */
#endif
