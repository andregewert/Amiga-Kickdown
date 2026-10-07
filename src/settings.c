/*
 * Kickdown - settings, stored as tool types of the program icon
 *
 * Writing replaces only our own tool types. A setting with its default
 * value is written as a disabled entry "(KEY=...)" if the icon had it
 * before, so the icon keeps showing what can be set. Other tool types,
 * among them the IM1=/IM2= lines of NewIcons, stay in their place; new
 * entries go in front of the NewIcons block.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include <stdio.h>
#include <string.h>

#include "settings.h"

/* dark enough for 4.5:1 contrast to black text on the Workbench grey */
const ULONG default_colours[NUMCOLOURS] = {
    0x0a2a8a,                       /* headings: navy */
    0x0a521e,                       /* code: dark green */
    0x3a4a66,                       /* block quotes: slate */
    0x8c1810,                       /* list and emphasis markers, rules: dark red */
    0x0038c0,                       /* link texts: blue */
    0x005266,                       /* URLs: petrol */
    0x7a1f6e,                       /* HTML tags and entities: plum */
};

const char *const colour_names[NUMCOLOURS] = {
    "COLOR_HEADING", "COLOR_CODE", "COLOR_QUOTE", "COLOR_MARKER",
    "COLOR_LINK", "COLOR_URL", "COLOR_HTML"
};

/* values of TOOLBAR= */
const char *const tbmode_names[NUMTBMODES] = { "IMAGES", "BOTH", "TEXT" };

/* values of PRINT_MODE=, PRINT_TO=, PAPER= */
const char *const prmode_names[NUMPRMODES] = { "PRINTER", "PS", "PDF" };
const char *const prdest_names[NUMPRDESTS] = { "FILE", "PRT", "PS", "DEVICE" };
const char *const paper_names[NUMPAPERS] = { "A4", "A5", "LETTER", "LEGAL" };
const short paper_sizes[NUMPAPERS][2] = { { 595, 842 }, { 420, 595 }, { 612, 792 }, { 612, 1008 } };

void settings_default(struct Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->highlight = TRUE;
    s->fmtbuttons = TRUE;
    s->splash = TRUE;
    s->margins[0] = s->margins[1] = s->margins[2] = s->margins[3] = 20;
    s->prsize = 10;
    s->prpagenumbers = TRUE;
    s->prbackgrounds = TRUE;
    s->pslevel = 2;
    s->prmode = -1;
    s->autorefresh = TRUE;
    s->syncscroll = TRUE;
    s->fitimages = FALSE;
    s->winleft = s->wintop = -1;
    strcpy(s->dialect, "GitHub");
    memcpy(s->colours, default_colours, sizeof(s->colours));
}

static void copy_str(char *dst, ULONG size, CONST_STRPTR src)
{
    strncpy(dst, (const char *)src, size - 1);
    dst[size - 1] = 0;
}

static BOOL same_text(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20)) a++, b++;
    return *a == *b;
}

static LONG hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "RRGGBB" or "#RRGGBB"; FALSE if it is not one */
static BOOL parse_colour(CONST_STRPTR v, ULONG *rgb)
{
    ULONG c = 0;
    int i;
    if (*v == '#') v++;
    for (i = 0; i < 6; i++) {
        LONG d = hex_digit(v[i]);
        if (d < 0) return FALSE;
        c = c << 4 | d;
    }
    if (v[6] && v[6] != ' ') return FALSE;
    *rgb = c;
    return TRUE;
}

void settings_from_tooltypes(struct Settings *s, CONST_STRPTR *tt, BPTR dir)
{
    STRPTR v;
    LONG n;
    int i;

    if (!tt) return;
    if (FindToolType(tt, (STRPTR)"NOHIGHLIGHT")) s->highlight = FALSE;
    if (FindToolType(tt, (STRPTR)"LINENUMBERS")) s->linenumbers = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"TOOLBAR")))
        for (i = 0; i < NUMTBMODES; i++)
            if (same_text((const char *)v, tbmode_names[i])) s->tbmode = i;
    if (FindToolType(tt, (STRPTR)"TOOLBARFRAMES")) s->tbframes = TRUE;
    if (FindToolType(tt, (STRPTR)"NOFORMATBUTTONS")) s->fmtbuttons = FALSE;
    if (FindToolType(tt, (STRPTR)"NOSPLASH")) s->splash = FALSE;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_MODE")))
        for (i = 0; i < NUMPRMODES; i++) if (same_text((const char *)v, prmode_names[i])) s->prmode = i;
    if ((v = FindToolType(tt, (STRPTR)"PS_LEVEL")) && StrToLong(v, &n) > 0 && (n == 1 || n == 2)) s->pslevel = n;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_TO")))
        for (i = 0; i < NUMPRDESTS; i++) if (same_text((const char *)v, prdest_names[i])) s->prdest = i;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_DEVICE"))) copy_str(s->prdevice, sizeof(s->prdevice), v);
    if ((v = FindToolType(tt, (STRPTR)"PAPER")))
        for (i = 0; i < NUMPAPERS; i++) if (same_text((const char *)v, paper_names[i])) s->paper = i;
    if ((v = FindToolType(tt, (STRPTR)"MARGINS"))) {
        /* "left,top,right,bottom" in mm; one value for all */
        LONG m[4], k = 0, len;
        while (k < 4 && (len = StrToLong(v, &n)) > 0 && n >= 0 && n <= 100) {
            m[k++] = n;
            v += len;
            if (*v != ',') break;
            v++;
        }
        if (k == 1) m[1] = m[2] = m[3] = m[0];
        if (k == 1 || k == 4) for (k = 0; k < 4; k++) s->margins[k] = m[k];
    }
    if (FindToolType(tt, (STRPTR)"PRINT_SERIF")) s->prserif = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_SIZE")) && StrToLong(v, &n) > 0 && n >= 4 && n <= 36) s->prsize = n;
    if (FindToolType(tt, (STRPTR)"NOPAGENUMBERS")) s->prpagenumbers = FALSE;
    if (FindToolType(tt, (STRPTR)"NOPRINTBACKGROUNDS")) s->prbackgrounds = FALSE;
    if ((v = FindToolType(tt, (STRPTR)"PRINT_UNIT")) && StrToLong(v, &n) > 0 && n >= 0 && n <= 9) s->prunit = n;
    if (FindToolType(tt, (STRPTR)"TTF")) s->ttf = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"FONTSET"))) copy_str(s->fontset, sizeof(s->fontset), v);
    if ((v = FindToolType(tt, (STRPTR)"SIZE")) && StrToLong(v, &n) > 0 && n >= 0) s->fontsize = n;
    if ((v = FindToolType(tt, (STRPTR)"WIDTH")) && StrToLong(v, &n) > 0 && n >= 0) s->winwidth = n;
    if ((v = FindToolType(tt, (STRPTR)"HEIGHT")) && StrToLong(v, &n) > 0 && n >= 0) s->winheight = n;
    if ((v = FindToolType(tt, (STRPTR)"LEFT")) && StrToLong(v, &n) > 0 && n >= 0) s->winleft = n;
    if ((v = FindToolType(tt, (STRPTR)"TOP")) && StrToLong(v, &n) > 0 && n >= 0) s->wintop = n;
    if (FindToolType(tt, (STRPTR)"NOAUTOREFRESH")) s->autorefresh = FALSE;
    if (FindToolType(tt, (STRPTR)"NOSYNC")) s->syncscroll = FALSE;
    if (FindToolType(tt, (STRPTR)"FITIMAGES")) s->fitimages = TRUE;
    if ((v = FindToolType(tt, (STRPTR)"DIALECT"))) copy_str(s->dialect, sizeof(s->dialect), v);
    if ((v = FindToolType(tt, (STRPTR)"CHARSET"))) copy_str(s->charset, sizeof(s->charset), v);
    if ((v = FindToolType(tt, (STRPTR)"TEMPLATE"))) {
        /* relative to the icon's drawer */
        if (dir && !strchr((const char *)v, ':') &&
            NameFromLock(dir, (STRPTR)s->template, sizeof(s->template)))
            AddPart((STRPTR)s->template, v, sizeof(s->template));
        else
            copy_str(s->template, sizeof(s->template), v);
    }
    for (i = 0; i < NUMCOLOURS; i++)
        if ((v = FindToolType(tt, (STRPTR)colour_names[i])))
            parse_colour(v, &s->colours[i]);
}

BOOL settings_load_icon(struct Settings *s, CONST_STRPTR name)
{
    struct DiskObject *dob = GetDiskObject((STRPTR)name);
    char dir[PATHLEN];
    BPTR lock;

    if (!dob) return FALSE;
    copy_str(dir, sizeof(dir), name);
    *PathPart((STRPTR)dir) = 0;
    lock = Lock((STRPTR)dir, ACCESS_READ);
    settings_from_tooltypes(s, (CONST_STRPTR *)dob->do_ToolTypes, lock);
    if (lock) UnLock(lock);
    FreeDiskObject(dob);
    return TRUE;
}

/*****************************************************************************/
/* writing                                                                   */

#define NUMKEYS (30 + NUMCOLOURS)
#define ENTRYLEN (PATHLEN + 24)

struct Entry {
    const char *key;
    BOOL active;                    /* not the default value */
    BOOL done;                      /* written */
    char text[ENTRYLEN];            /* "KEY" or "KEY=value" */
};

static void entry(struct Entry *e, const char *key, BOOL active, const char *value)
{
    e->key = key;
    e->active = active;
    e->done = FALSE;
    if (value) snprintf(e->text, sizeof(e->text), "%s=%s", key, value);
    else snprintf(e->text, sizeof(e->text), "%s", key);
}

/* the key of a tool type, also of a disabled one "(KEY=...)" */
static BOOL has_key(CONST_STRPTR tt, const char *key)
{
    const char *p = (const char *)tt;
    if (*p == '(') p++;
    while (*key && (*p | 0x20) == (*key | 0x20)) p++, key++;
    return !*key && (!*p || *p == '=' || *p == ')' || *p == ' ');
}

BOOL settings_save_icon(const struct Settings *s, CONST_STRPTR name)
{
    struct DiskObject *dob;
    struct Entry *e;
    STRPTR *old, *tt;
    ULONG nold = 0, n = 0, i, k, nicons;
    char num[12], wnum[12], hnum[12], lnum[12], tnum[12], buf[NUMCOLOURS][8], *disabled;
    char mnum[24], snum[12], unum[12];
    BOOL ok;

    if (!(dob = GetDiskObject((STRPTR)name))) {
        /* no icon yet: a default tool icon */
        if (!(dob = IconBase->lib_Version >= 44 ? GetDiskObjectNew((STRPTR)name)
                                                : GetDefDiskObject(WBTOOL)))
            return FALSE;
    }
    if (!(e = AllocVec(NUMKEYS * sizeof(*e), MEMF_ANY))) {
        FreeDiskObject(dob);
        return FALSE;
    }

    k = 0;
    entry(&e[k++], "NOHIGHLIGHT", !s->highlight, NULL);
    entry(&e[k++], "LINENUMBERS", s->linenumbers, NULL);
    entry(&e[k++], "TOOLBAR", s->tbmode != TBMODE_IMAGES,
          tbmode_names[s->tbmode >= 0 && s->tbmode < NUMTBMODES ? s->tbmode : 0]);
    entry(&e[k++], "TOOLBARFRAMES", s->tbframes, NULL);
    entry(&e[k++], "NOFORMATBUTTONS", !s->fmtbuttons, NULL);
    entry(&e[k++], "NOSPLASH", !s->splash, NULL);
    entry(&e[k++], "PRINT_MODE", s->prmode >= 0, prmode_names[s->prmode >= 0 && s->prmode < NUMPRMODES ? s->prmode : 0]);
    entry(&e[k++], "PS_LEVEL", s->pslevel != 2, s->pslevel == 1 ? "1" : "2");
    entry(&e[k++], "PRINT_TO", s->prdest != PRDEST_FILE, prdest_names[s->prdest >= 0 && s->prdest < NUMPRDESTS ? s->prdest : 0]);
    entry(&e[k++], "PRINT_DEVICE", s->prdevice[0] != 0, s->prdevice);
    entry(&e[k++], "PAPER", s->paper != PAPER_A4, paper_names[s->paper >= 0 && s->paper < NUMPAPERS ? s->paper : 0]);
    sprintf(mnum, "%ld,%ld,%ld,%ld", (long)s->margins[0], (long)s->margins[1], (long)s->margins[2], (long)s->margins[3]);
    entry(&e[k++], "MARGINS", s->margins[0] != 20 || s->margins[1] != 20 || s->margins[2] != 20 || s->margins[3] != 20, mnum);
    entry(&e[k++], "PRINT_SERIF", s->prserif, NULL);
    sprintf(snum, "%ld", (long)s->prsize);
    entry(&e[k++], "PRINT_SIZE", s->prsize != 10, snum);
    entry(&e[k++], "NOPAGENUMBERS", !s->prpagenumbers, NULL);
    entry(&e[k++], "NOPRINTBACKGROUNDS", !s->prbackgrounds, NULL);
    sprintf(unum, "%ld", (long)s->prunit);
    entry(&e[k++], "PRINT_UNIT", s->prunit != 0, unum);
    entry(&e[k++], "TTF", s->ttf, NULL);
    entry(&e[k++], "FONTSET", s->fontset[0] != 0, s->fontset);
    sprintf(num, "%ld", (long)s->fontsize);
    entry(&e[k++], "SIZE", s->fontsize > 0, num);
    entry(&e[k++], "NOAUTOREFRESH", !s->autorefresh, NULL);
    entry(&e[k++], "NOSYNC", !s->syncscroll, NULL);
    entry(&e[k++], "FITIMAGES", s->fitimages, NULL);
    entry(&e[k++], "DIALECT", !same_text(s->dialect, "GitHub"), s->dialect);
    entry(&e[k++], "CHARSET", s->charset[0] != 0, s->charset);
    entry(&e[k++], "TEMPLATE", s->template[0] != 0, s->template);
    sprintf(wnum, "%ld", (long)s->winwidth);
    sprintf(hnum, "%ld", (long)s->winheight);
    entry(&e[k++], "WIDTH", s->winwidth > 0, wnum);
    entry(&e[k++], "HEIGHT", s->winheight > 0, hnum);
    sprintf(lnum, "%ld", (long)s->winleft);
    sprintf(tnum, "%ld", (long)s->wintop);
    entry(&e[k++], "LEFT", s->winleft >= 0, lnum);
    entry(&e[k++], "TOP", s->wintop >= 0, tnum);
    for (i = 0; i < NUMCOLOURS; i++) {
        sprintf(buf[i], "%06lX", (unsigned long)s->colours[i]);
        entry(&e[k++], colour_names[i], s->colours[i] != default_colours[i], buf[i]);
    }

    old = (STRPTR *)dob->do_ToolTypes;
    while (old && old[nold]) nold++;
    /* new entries go in front of the NewIcons block */
    for (nicons = 0; nicons < nold; nicons++)
        if (!strncmp((const char *)old[nicons], "*** DON'T EDIT", 14)) {
            if (nicons && !strcmp((const char *)old[nicons - 1], " ")) nicons--;
            break;
        }

    /* old entries + ours + disabled copies ("(" old ")") + NULL */
    ok = FALSE;
    if ((tt = AllocVec((nold + NUMKEYS + 1) * sizeof(STRPTR), MEMF_CLEAR)) &&
        (disabled = AllocVec(NUMKEYS * (ENTRYLEN + 2), MEMF_ANY))) {
        char *dp = disabled;
        for (i = 0; i <= nold; i++) {
            if (i == nicons)                    /* not yet in the icon */
                for (k = 0; k < NUMKEYS; k++)
                    if (!e[k].done && e[k].active) {
                        tt[n++] = (STRPTR)e[k].text;
                        e[k].done = TRUE;
                    }
            if (i == nold) break;
            for (k = 0; k < NUMKEYS && !has_key(old[i], e[k].key); k++) ;
            if (k == NUMKEYS) {                 /* not ours */
                tt[n++] = old[i];
            } else if (!e[k].done) {
                e[k].done = TRUE;
                if (e[k].active) tt[n++] = (STRPTR)e[k].text;
                else if (old[i][0] == '(') tt[n++] = old[i];
                else {                          /* default now: disable it */
                    snprintf(dp, ENTRYLEN + 2, "(%s)", (const char *)old[i]);
                    tt[n++] = (STRPTR)dp;
                    dp += strlen(dp) + 1;
                }
            }                                   /* duplicates are dropped */
        }
        dob->do_ToolTypes = tt;
        ok = PutDiskObject((STRPTR)name, dob);
        dob->do_ToolTypes = old;
        FreeVec(disabled);
    }
    if (tt) FreeVec(tt);
    FreeVec(e);
    FreeDiskObject(dob);
    return ok;
}
