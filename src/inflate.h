/*
 * inflate.h - gzip/DEFLATE decompression of HTTP answers (inflate.c).
 * Each appends what it unpacks to out; 1 when the data was whole.
 */
#ifndef INFLATE_H
#define INFLATE_H

#include "util.h"

int inflate_raw(const unsigned char *in, long n, SBUF *out);
int inflate_gzip(const unsigned char *in, long n, SBUF *out);
int inflate_zlib(const unsigned char *in, long n, SBUF *out);

#endif
