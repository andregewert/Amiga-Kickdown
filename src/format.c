/*
 * Kickdown - formatting commands (toolbar and Format menu)
 *
 * The text changes are made by mdformat.c; this is the glue to
 * texteditor.gadget. Positions in the editor are (x, y): character in the
 * paragraph and paragraph number, a paragraph is a line of the file. The
 * selection (GM_TEXTEDITOR_BlockInfo) ends before stop x.
 *
 * Inline formats replace the selection (or are inserted at the cursor),
 * line formats replace the complete lines of the selection (or the cursor
 * line). Each command is one undo step.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <gadgets/texteditor.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include <stdlib.h>
#include <string.h>

#include "kickdown.h"
#include "mdformat.h"

static ULONG method(ULONG id, ULONG a, ULONG b, ULONG c, ULONG d)
{
    return DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, id, 0, a, b, c, d);
}

/* start of line y in the exported text, NULL if there is no such line */
static const char *line_start(const char *text, ULONG y)
{
    while (y) {
        const char *nl = strchr(text, '\n');
        if (!nl) return NULL;
        text = nl + 1;
        y--;
    }
    return text;
}

static ULONG line_len(const char *line)
{
    const char *nl = strchr(line, '\n');
    return nl ? (ULONG)(nl - line) : strlen(line);
}

/* position (x, y) in the text, NULL if outside */
static const char *at(const char *text, ULONG x, ULONG y)
{
    const char *l = line_start(text, y);
    if (!l) return NULL;
    return l + (x < line_len(l) ? x : line_len(l));
}

/* the position after inserting the first n bytes of s at (*x, *y) */
static void advance(ULONG *x, ULONG *y, const char *s, size_t n)
{
    while (n--) {
        if (*s++ == '\n') {
            (*y)++;
            *x = 0;
        } else (*x)++;
    }
}

static void set_cursor(ULONG x, ULONG y)
{
    /* separately: x is cut to the length of the current paragraph */
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL, GA_TEXTEDITOR_CursorY, y, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL, GA_TEXTEDITOR_CursorX, x, TAG_DONE);
}

/* replaces the area (x0, y0) - (x1, y1) of the editor with s */
static void replace(ULONG x0, ULONG y0, ULONG x1, ULONG y1, const char *s)
{
    if (x0 == x1 && y0 == y1) {
        set_cursor(x0, y0);
        if (*s) method(GM_TEXTEDITOR_InsertText, (ULONG)s, GV_TEXTEDITOR_InsertText_Cursor, 0, 0);
    } else {
        method(GM_TEXTEDITOR_MarkText, x0, y0, x1, y1);
        method(GM_TEXTEDITOR_Replace, (ULONG)s, 0, 0, 0);
    }
}

void format_apply(int kind)
{
    STRPTR text;
    const char *p0, *p1;
    char *res = NULL;
    ULONG x0 = 0, y0 = 0, x1, y1, marked = FALSE;

    if (!gui.win || kind < 0 || kind >= FMT_COUNT) return;
    if (!(text = (STRPTR)method(GM_TEXTEDITOR_ExportText, 0, 0, 0, 0))) return;

    GetAttr(GA_TEXTEDITOR_AreaMarked, gui.editor, &marked);
    if (marked) {
        x0 = y0 = x1 = y1 = 0;
        method(GM_TEXTEDITOR_BlockInfo, (ULONG)&x0, (ULONG)&y0, (ULONG)&x1, (ULONG)&y1);
        if (y1 < y0 || (y1 == y0 && x1 < x0)) {
            ULONG t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
        }
    } else {
        GetAttr(GA_TEXTEDITOR_CursorX, gui.editor, &x0);
        GetAttr(GA_TEXTEDITOR_CursorY, gui.editor, &y0);
        x1 = x0;
        y1 = y0;
    }

    method(GM_TEXTEDITOR_ARexxCmd, (ULONG)"BEGINMULTICHANGE", 0, 0, 0);
    if (FMT_IS_INLINE(kind)) {
        size_t m0, m1;
        if ((p0 = at((const char *)text, x0, y0)) && (p1 = at((const char *)text, x1, y1)) &&
            (res = mdfmt_inline(kind, p0, (size_t)(p1 - p0), &m0, &m1))) {
            ULONG ax = x0, ay = y0, bx = x0, by = y0;
            replace(x0, y0, x1, y1, res);
            advance(&ax, &ay, res, m0);
            advance(&bx, &by, res, m1);
            if (m0 == m1) set_cursor(ax, ay);
            else method(GM_TEXTEDITOR_MarkText, ax, ay, bx, by);
        }
    } else {
        /* a selection up to the start of a line does not include it */
        if (marked && y1 > y0 && x1 == 0) y1--;
        if ((p0 = line_start((const char *)text, y0)) && (p1 = line_start((const char *)text, y1))) {
            ULONG len1 = line_len(p1);
            if ((res = mdfmt_lines(kind, p0, (size_t)(p1 + len1 - p0)))) {
                ULONG ex = 0, ey = y0;
                replace(0, y0, len1, y1, res);
                advance(&ex, &ey, res, strlen(res));
                /* a selection covers the new lines, else the cursor goes
                 * to the end of the line                               */
                if (marked) method(GM_TEXTEDITOR_MarkText, 0, y0, ex, ey);
                else set_cursor(ex, ey);
            }
        }
    }
    method(GM_TEXTEDITOR_ARexxCmd, (ULONG)"ENDMULTICHANGE", 0, 0, 0);
    free(res);
    FreeVec(text);
    editor_changed();
    gui_activate_editor();
}
