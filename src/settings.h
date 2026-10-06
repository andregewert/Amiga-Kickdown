/*
 * Kickdown - settings, stored as tool types of the program icon
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <exec/types.h>
#include <dos/dos.h>

#define PATHLEN 512

/* toolbar: what the buttons show */
enum { TBMODE_IMAGES, TBMODE_BOTH, TBMODE_TEXT, NUMTBMODES };

/* printing and export */
enum { PRMODE_PRINTER, PRMODE_PS, PRMODE_PDF, NUMPRMODES };
enum { PRDEST_FILE, PRDEST_PRT, PRDEST_PS, PRDEST_DEVICE, NUMPRDESTS };
enum { PAPER_A4, PAPER_A5, PAPER_LETTER, PAPER_LEGAL, NUMPAPERS };

/* colours of the syntax highlighting */
enum { C_HEADING, C_CODE, C_QUOTE, C_MARKER, C_LINK, C_URL, C_HTML, NUMCOLOURS };

struct Settings {
    /* editor */
    BOOL  highlight;
    BOOL  linenumbers;
    /* toolbar, takes effect at the next start */
    LONG  tbmode;                   /* TBMODE_... */
    BOOL  tbframes;                 /* frames around the buttons */
    BOOL  fmtbuttons;               /* formatting buttons shown (at once) */
    /* preview; ttf, fontset and fontsize take effect at the next start */
    BOOL  ttf;
    char  fontset[16];              /* "" = first one found */
    LONG  fontsize;                 /* 0 = from the screen font */
    BOOL  autorefresh;
    BOOL  syncscroll;
    BOOL  fitimages;                /* pictures no wider than the preview */
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
    BOOL  splash;                   /* splash window while starting */
    /* printing and export */
    LONG  prmode;                   /* PRMODE_... */
    LONG  pslevel;                  /* PostScript level 1 or 2 */
    LONG  prdest;                   /* PostScript to: PRDEST_... */
    char  prdevice[40];             /* PRDEST_DEVICE: "PAR:" etc. */
    LONG  paper;                    /* PAPER_... */
    LONG  margins[4];               /* left, top, right, bottom in mm */
    BOOL  prserif;                  /* PostScript/PDF text in Times */
    LONG  prsize;                   /* normal text in points */
    BOOL  prpagenumbers;
    BOOL  prbackgrounds;
    LONG  prunit;                   /* printer.device unit 0-9 */
    LONG  prdensity;                /* 1-7, 0 = as in the printer settings */
    /* colours as 0xRRGGBB */
    ULONG colours[NUMCOLOURS];
};

extern const ULONG default_colours[NUMCOLOURS];
extern const char *const colour_names[NUMCOLOURS];
extern const char *const tbmode_names[NUMTBMODES];
extern const char *const prmode_names[NUMPRMODES], *const prdest_names[NUMPRDESTS];
extern const char *const paper_names[NUMPAPERS];
extern const short paper_sizes[NUMPAPERS][2];   /* width, height in points */

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
