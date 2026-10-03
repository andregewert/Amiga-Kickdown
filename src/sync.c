/*
 * Kickdown - keeps editor and preview at the same place in the document
 *
 * The headings are fixed points: their source line is known from the
 * Markdown text, their position in the preview from the anchors that
 * mdconv gives them (HTML_Anchor). Between them the position is
 * interpolated linearly. The editor wraps long lines, so its first
 * visible row is first mapped to a source line; the number of rows of
 * each line is estimated by word wrapping at a column count that is
 * calibrated against GA_TEXTEDITOR_Prop_Entries.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <gadgets/texteditor.h>
#include <gadgets/scroller.h>
#include <gadgets/html.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include <string.h>

#include "kickdown.h"

#define FRAC 1024                   /* fixed point for line fractions */

struct Knot {
    LONG line;                      /* source line * FRAC */
    LONG y;                         /* preview position */
};

static ULONG *cum;                  /* cum[i]: editor rows before source line i */
static ULONG nlines;
static struct Knot *knots;
static ULONG nknots;
static BOOL valid;

static LONG last_first = -1, last_top = -1;

void sync_free(void)
{
    if (cum) FreeVec(cum);
    if (knots) FreeVec(knots);
    cum = NULL;
    knots = NULL;
    nlines = nknots = 0;
    valid = FALSE;
}

/*****************************************************************************/
/* source lines                                                              */

static const char *skip_spaces(const char *p, int max)
{
    while (max-- > 0 && *p == ' ') p++;
    return p;
}

static BOOL is_blank(const char *p, const char *end)
{
    for (; p < end; p++)
        if (*p != ' ' && *p != '\t' && *p != '\r') return FALSE;
    return TRUE;
}

/* a line of one character only (and spaces), at least 'min' times */
static BOOL is_rule(const char *p, const char *end, char c, int min)
{
    int n = 0;
    for (; p < end; p++) {
        if (*p == c) n++;
        else if (*p != ' ' && *p != '\t' && *p != '\r') return FALSE;
    }
    return n >= min;
}

static BOOL is_list_item(const char *p, const char *end)
{
    if ((*p == '-' || *p == '*' || *p == '+') && p + 1 < end && (p[1] == ' ' || p[1] == '\t'))
        return TRUE;
    while (p < end && *p >= '0' && *p <= '9') p++;
    return p < end - 1 && (*p == '.' || *p == ')') && (p[1] == ' ' || p[1] == '\t');
}

/* Rows the editor needs for a line of 'len' characters with word wrap at
 * 'cols' columns. Tabs count as one column, close enough here.         */
static ULONG wrap_rows(const char *p, ULONG len, ULONG cols)
{
    ULONG rows = 1, col = 0, i = 0;

    while (i < len) {
        ULONG w = 0;
        while (i + w < len && p[i + w] != ' ') w++;   /* word */
        if (col && col + w > cols) {                   /* does not fit: next row */
            rows++;
            col = 0;
        }
        while (w > cols) {                             /* longer than a row */
            rows++;
            w -= cols;
        }
        col += w;
        i += w;
        if (i < len) {                                 /* the space */
            col++;
            i++;
            if (col >= cols) { rows++; col = 0; }
        }
    }
    return rows;
}

static ULONG count_rows(const char *text, ULONG cols, ULONG *store)
{
    const char *p = text, *e;
    ULONG total = 0, i = 0;

    for (;;) {
        ULONG r;
        if (!(e = strchr(p, '\n'))) e = p + strlen(p);
        r = wrap_rows(p, e - p, cols);
        if (store) store[i] = total;
        total += r;
        i++;
        if (!*e) break;
        p = e + 1;
    }
    if (store) store[i] = total;
    return total;
}

/* Fills cum[] with the editor row of every source line. The column count
 * is the smallest that gives the editor's real number of rows.          */
static void build_rows(const char *text)
{
    ULONG entries = 0, lo = 8, hi = 512, total;

    GetAttr(GA_TEXTEDITOR_Prop_Entries, gui.editor, &entries);
    total = count_rows(text, hi, NULL);
    if (entries > total) {
        while (lo < hi) {
            ULONG mid = (lo + hi) / 2;
            if (count_rows(text, mid, NULL) <= entries) hi = mid;
            else lo = mid + 1;
        }
    }
    count_rows(text, hi, cum);
}

/*****************************************************************************/
/* headings                                                                  */

/* source lines of the headings in the order md4c writes them */
static ULONG find_headings(const char *text, LONG *lines, ULONG max)
{
    const char *p = text, *e, *q;
    ULONG n = 0, line = 0;
    BOOL para = FALSE, fence = FALSE;
    char fchar = 0;

    for (;; p = e + 1, line++) {
        if (!(e = strchr(p, '\n'))) e = p + strlen(p);
        q = skip_spaces(p, 3);
        while (q < e && *q == '>') q = skip_spaces(q + 1, 4);   /* block quotes */

        if (fence) {
            if (q + 2 < e && q[0] == fchar && q[1] == fchar && q[2] == fchar) fence = FALSE;
            para = FALSE;
        } else if (q + 2 < e && (*q == '`' || *q == '~') && q[1] == *q && q[2] == *q) {
            fence = TRUE;
            fchar = *q;
            para = FALSE;
        } else if (is_blank(q, e)) {
            para = FALSE;
        } else if (*q == '#') {
            const char *h = q;
            while (h < e && *h == '#') h++;
            if (h - q <= 6 && (h == e || *h == ' ' || *h == '\t' || *h == '\r')) {
                if (n < max) lines[n] = line;
                n++;
                para = FALSE;
            } else para = TRUE;
        } else if (para && (is_rule(q, e, '=', 1) || is_rule(q, e, '-', 1))) {
            if (n < max) lines[n] = line - 1;           /* setext heading */
            n++;
            para = FALSE;
        } else if (is_rule(q, e, '-', 3) || is_rule(q, e, '*', 3) || is_rule(q, e, '_', 3)) {
            para = FALSE;
        } else if (is_list_item(q, e)) {
            para = FALSE;
        } else if (!para && q - p > 3) {
            /* indented code */
        } else {
            para = TRUE;
        }
        if (!*e) break;
    }
    return n;
}

/* the anchors mdconv gave the headings, in document order */
static ULONG find_ids(const char *html, char **ids, ULONG max)
{
    const char *p = html;
    ULONG n = 0;

    while ((p = strstr(p, "<h"))) {
        p += 2;
        if (*p >= '1' && *p <= '6' && !strncmp(p + 1, " id=\"", 5)) {
            const char *s = p + 6, *q = strchr(s, '"');
            if (!q) break;
            if (n < max && (ids[n] = AllocVec(q - s + 1, MEMF_ANY))) {
                memcpy(ids[n], s, q - s);
                ids[n][q - s] = 0;
            }
            n++;
        }
    }
    return n;
}

void sync_rebuild(CONST_STRPTR mdtext, const char *html)
{
    const char *text = (const char *)mdtext, *s;
    ULONG nhead, nids, i, top = 0, total = 0;
    LONG *hlines = NULL;
    char **ids = NULL;

    sync_free();
    last_first = last_top = -1;

    for (nlines = 1, s = text; (s = strchr(s, '\n')); s++) nlines++;
    if (!(cum = AllocVec((nlines + 1) * sizeof(ULONG), MEMF_ANY))) return;
    build_rows(text);

    nhead = find_headings(text, NULL, 0);
    nids = find_ids(html, NULL, 0);
    if (nhead != nids) nhead = 0;       /* not sure which belongs to which */

    /* knots: start, headings, end */
    if (!(knots = AllocVec((nhead + 2) * sizeof(struct Knot), MEMF_ANY))) return;
    knots[0].line = 0;
    knots[0].y = 0;
    nknots = 1;

    if (nhead && (hlines = AllocVec(nhead * sizeof(LONG), MEMF_ANY)) &&
        (ids = AllocVec(nhead * sizeof(char *), MEMF_ANY | MEMF_CLEAR))) {
        find_headings(text, hlines, nhead);
        find_ids(html, ids, nhead);
        /* the anchor moves the preview, its position is the y of the heading */
        GetAttr(HTML_Top, gui.html, &top);
        for (i = 0; i < nhead; i++) {
            ULONG y = 0;
            if (!ids[i]) continue;
            SetAttrs(gui.html, HTML_Anchor, (ULONG)ids[i], TAG_DONE);
            GetAttr(HTML_Top, gui.html, &y);
            if (hlines[i] * FRAC > knots[nknots - 1].line && (LONG)y >= knots[nknots - 1].y) {
                knots[nknots].line = hlines[i] * FRAC;
                knots[nknots].y = y;
                nknots++;
            }
        }
        SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Top, top, TAG_DONE);
    }
    if (ids) {
        for (i = 0; i < nhead; i++) if (ids[i]) FreeVec(ids[i]);
        FreeVec(ids);
    }
    if (hlines) FreeVec(hlines);

    GetAttr(HTML_Total, gui.html, &total);
    if ((LONG)nlines * FRAC > knots[nknots - 1].line) {
        knots[nknots].line = nlines * FRAC;
        knots[nknots].y = (LONG)total > knots[nknots - 1].y ? (LONG)total : knots[nknots - 1].y;
        nknots++;
    }
    valid = TRUE;
}

/*****************************************************************************/
/* mapping                                                                   */

/* editor row -> source line * FRAC */
static LONG row_to_line(ULONG row)
{
    ULONG lo = 0, hi = nlines;
    while (lo + 1 < hi) {                   /* last line with cum[line] <= row */
        ULONG mid = (lo + hi) / 2;
        if (cum[mid] <= row) lo = mid;
        else hi = mid;
    }
    {
        ULONG rows = cum[lo + 1] - cum[lo];
        ULONG part = row > cum[lo] ? row - cum[lo] : 0;
        return (LONG)(lo * FRAC + (rows ? part * FRAC / rows : 0));
    }
}

static ULONG line_to_row(LONG line)
{
    ULONG l = line / FRAC, rows;
    if (l >= nlines) return cum[nlines];
    rows = cum[l + 1] - cum[l];
    return cum[l] + ((ULONG)(line % FRAC) * rows + FRAC / 2) / FRAC;
}

/* interpolation between the knots, in both directions */
static LONG line_to_y(LONG line)
{
    ULONG i;
    for (i = 1; i < nknots; i++) {
        if (line <= knots[i].line) {
            LONG dl = knots[i].line - knots[i - 1].line;
            LONG f = dl ? (line - knots[i - 1].line) / (dl / FRAC ? dl / FRAC : 1) : 0;
            /* f: position between the knots in 1/FRAC */
            if (f > FRAC) f = FRAC;
            return knots[i - 1].y + ((knots[i].y - knots[i - 1].y) * f + FRAC / 2) / FRAC;
        }
    }
    return knots[nknots - 1].y;
}

static LONG y_to_line(LONG y)
{
    ULONG i;
    for (i = 1; i < nknots; i++) {
        if (y < knots[i].y) {
            LONG dy = knots[i].y - knots[i - 1].y;
            LONG f = ((y - knots[i - 1].y) * FRAC + dy / 2) / dy;
            return knots[i - 1].line + (knots[i].line - knots[i - 1].line) / FRAC * f;
        }
    }
    return knots[nknots - 1].line;
}

/*****************************************************************************/

/* The next sync_poll() moves the preview to the editor's position. */
void sync_reset(void)
{
    last_top = -1;
}

/* About ten times a second: if one side was scrolled, the other follows. */
void sync_poll(void)
{
    ULONG first = 0, entries = 0, visible = 0, top = 0, total = 0, vis = 0;

    if (!valid || !nknots) return;
    GetAttr(GA_TEXTEDITOR_Prop_First, gui.editor, &first);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, gui.editor, &entries);
    GetAttr(GA_TEXTEDITOR_Prop_Visible, gui.editor, &visible);
    GetAttr(HTML_Top, gui.html, &top);
    GetAttr(HTML_Total, gui.html, &total);
    GetAttr(HTML_Visible, gui.html, &vis);

    if ((LONG)first != last_first || last_top < 0) {
        /* editor -> preview; at the editor's end the preview goes to its end */
        LONG y;
        if (entries > visible && first + visible >= entries) y = (LONG)total - (LONG)vis;
        else y = line_to_y(row_to_line(first));
        if (y < 0) y = 0;
        if (y != (LONG)top)
            SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Top, y, TAG_DONE);
        GetAttr(HTML_Top, gui.html, &top);
    } else if ((LONG)top != last_top) {
        /* preview -> editor */
        LONG row;
        if (total > vis && top + vis >= total) row = (LONG)entries - (LONG)visible;
        else row = line_to_row(y_to_line(top));
        if (row < 0) row = 0;
        if (row != (LONG)first) {
            SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                           GA_TEXTEDITOR_Prop_First, row, TAG_DONE);
            SetGadgetAttrs((struct Gadget *)gui.escroll, gui.win, NULL, SCROLLER_Top, row, TAG_DONE);
        }
        GetAttr(GA_TEXTEDITOR_Prop_First, gui.editor, &first);
    }
    last_first = first;
    last_top = top;
}
