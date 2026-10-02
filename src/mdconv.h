/*
 * mdconv - Markdown to HTML conversion shared by MDEdit and mdtohtml
 *
 * Plain ANSI C on top of md4c, so it builds for the Amiga and for the
 * host (make check). Input is treated byte by byte (MD4C_USE_ASCII):
 * Latin-1 and UTF-8 texts pass through unchanged.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef MDCONV_H
#define MDCONV_H

#include <stddef.h>

struct MDConvOptions {
    unsigned    flags;          /* md4c parser flags, see mdconv_dialect() */
    const char *title;          /* NULL: text of the first <h1>, else fallback_title */
    const char *fallback_title; /* NULL: "Untitled" */
    const char *charset;        /* NULL: "UTF-8" for valid UTF-8 input, else "ISO-8859-1" */
    const char *tmpl;           /* page template, NULL: built-in HTML 4 frame */
};

/* Parser flags of a dialect name ("GitHub", "CommonMark"; case is
 * ignored). Returns 0 for an unknown name and leaves *flags alone.      */
int mdconv_dialect(const char *name, unsigned *flags);

/* Default dialect: GitHub flavoured Markdown (tables, strikethrough,
 * task lists, autolinks, footnotes)                                       */
unsigned mdconv_default_flags(void);

/* Converts 'size' bytes of Markdown into a complete HTML page. The
 * template may contain $title$, $encoding$ (or $charset$) and $body$
 * (case is ignored). Returns a NUL terminated string to be released with
 * mdconv_free(), NULL if out of memory. *len (optional) receives the
 * length without the NUL.                                                */
char *mdconv_html(const char *md, size_t size, const struct MDConvOptions *opt, size_t *len);

void mdconv_free(char *html);

/* "UTF-8" if the text contains non-ASCII bytes that form valid UTF-8,
 * otherwise "ISO-8859-1"                                                  */
const char *mdconv_guess_charset(const char *text, size_t size);

/* e.g. "md4c 0.6.0" */
const char *mdconv_version(void);

#endif /* MDCONV_H */
