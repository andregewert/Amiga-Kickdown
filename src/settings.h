/*
 * MDEdit - settings, stored as tool types of the program icon
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <exec/types.h>
#include <dos/dos.h>

#define PATHLEN 512

/* colours of the syntax highlighting */
enum { C_HEADING, C_CODE, C_QUOTE, C_MARKER, C_LINK, C_URL, C_HTML, NUMCOLOURS };

struct Settings {
    /* editor */
    BOOL  highlight;
    BOOL  linenumbers;
    /* preview; ttf, fontset and fontsize take effect at the next start */
    BOOL  ttf;
    char  fontset[16];              /* "" = first one found */
    LONG  fontsize;                 /* 0 = from the screen font */
    BOOL  autorefresh;
    BOOL  syncscroll;
    /* Markdown */
    char  dialect[20];              /* "GitHub" or "CommonMark" */
    char  charset[40];              /* "" = detected */
    char  template[PATHLEN];        /* "" = built-in page */
    /* main window at the start: size 0 = from the screen size,
     * position -1 = centred                                         */
    LONG  winwidth;
    LONG  winheight;
    LONG  winleft;
    LONG  wintop;
    /* colours as 0xRRGGBB */
    ULONG colours[NUMCOLOURS];
};

extern const ULONG default_colours[NUMCOLOURS];
extern const char *const colour_names[NUMCOLOURS];

void settings_default(struct Settings *s);

/* Takes the tool types of an icon; a relative TEMPLATE is relative to
 * 'dir' (the icon's drawer, 0 = current directory).                   */
void settings_from_tooltypes(struct Settings *s, CONST_STRPTR *tt, BPTR dir);

/* Reads the tool types of 'name' (without .info). FALSE: no icon. */
BOOL settings_load_icon(struct Settings *s, CONST_STRPTR name);

/* Writes the settings into the tool types of 'name', other tool types
 * (NewIcons images, comments) stay as they are. FALSE on error.        */
BOOL settings_save_icon(const struct Settings *s, CONST_STRPTR name);

#endif /* SETTINGS_H */
