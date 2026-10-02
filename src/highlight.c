/*
 * MDEdit - Markdown syntax highlighting for texteditor.gadget (V47)
 *
 * GA_TEXTEDITOR_HighlighterHook is called for every line (block) with a
 * struct HighlightMessage; HighlightSetFormat(object, pos, end, style)
 * formats the characters pos..end-1 (styles are ORed, a colour replaces
 * the previous one). The value the hook returns is handed to the next line
 * as StatusOfPrevBlock; when it changes, the gadget calls the hook for the
 * following lines as well. Here it carries open code fences.
 *
 * The hook runs in the input.device context while the user types: no
 * DOS, no memory allocation, little stack.
 *
 * With TBSTYLE_SETCOLOR the high byte of a style is a screen pen, used as
 * it is (GA_TEXTEDITOR_ColorMap is the gadget's own pen table: normal,
 * shine, ..., text, fill, mark - not set here). The pens are obtained
 * with ObtainBestPen(). The colours are dark enough for black text on the
 * Workbench grey (#BDBDBD): contrast at least 4.5:1.
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/view.h>
#include <gadgets/texteditor.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/texteditor.h>
#include <clib/alib_protos.h>

#include "mdedit.h"
#include "settings.h"

static UBYTE pens[NUMCOLOURS];      /* screen pens of the colours (settings.h) */
static LONG obtained[NUMCOLOURS];   /* pens from ObtainBestPen(), -1: none */
static struct ColorMap *pencm;

#define COLOUR(c)   (TBSTYLE_SETCOLOR | (ULONG)pens[c] << 8)

/* status of a line: an open code fence (char and length) or nothing */
#define ST_FENCE        0x01000000UL
#define FENCE_CHAR(s)   ((char)((s) >> 8 & 0xff))
#define FENCE_LEN(s)    ((s) & 0xff)

/*****************************************************************************/
/* pens                                                                      */

static void release_pens(struct ColorMap *cm, LONG *got)
{
    int i;
    for (i = 0; i < NUMCOLOURS; i++)
        if (cm && got[i] >= 0) {
            ReleasePen(cm, got[i]);
            got[i] = -1;
        }
}

/* Gets the pens for the colours (0xRRGGBB) on the editor's screen. Also
 * for a change of colours: the old pens are released afterwards, the
 * text has to be formatted anew (set the hook again).                 */
void highlight_colours(struct Screen *scr, const ULONG *rgb)
{
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD bg = dri ? dri->dri_Pens[BACKGROUNDPEN] : 0, text = dri ? dri->dri_Pens[TEXTPEN] : 1;
    struct ColorMap *oldcm = pencm;
    LONG old[NUMCOLOURS];
    int i;

    for (i = 0; i < NUMCOLOURS; i++) old[i] = oldcm ? obtained[i] : -1;
    pencm = scr->ViewPort.ColorMap;
    for (i = 0; i < NUMCOLOURS; i++) {
        ULONG r = rgb[i] >> 16 & 0xff, g = rgb[i] >> 8 & 0xff, b = rgb[i] & 0xff;
        LONG pen = ObtainBestPen(pencm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL,
                                 OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        obtained[i] = pen;
        /* few colours: rather the text pen than an invisible one */
        if (pen < 0 || pen == bg || pen > 255) pen = text;
        pens[i] = (UBYTE)pen;
    }
    if (dri) FreeScreenDrawInfo(scr, dri);
    release_pens(oldcm, old);
}

void highlight_release(void)
{
    release_pens(pencm, obtained);
    pencm = NULL;
}

/*****************************************************************************/
/* scanning                                                                  */

static APTR obj;                    /* only valid during the hook call */

static void fmt(ULONG pos, ULONG end, ULONG style)
{
    if (end > pos) HighlightSetFormat(obj, pos, end, style);
}

static BOOL is_space(char c)
{
    return c == ' ' || c == '\t';
}

static BOOL is_alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           (unsigned char)c >= 0xc0;
}

static ULONG run(const char *t, ULONG i, ULONG len, char c)
{
    ULONG n = 0;
    while (i + n < len && t[i + n] == c) n++;
    return n;
}

/* a line of 'c' only (spaces allowed), at least 'min' times */
static BOOL is_rule(const char *t, ULONG i, ULONG len, char c, ULONG min)
{
    ULONG n = 0;
    for (; i < len; i++) {
        if (t[i] == c) n++;
        else if (!is_space(t[i])) return FALSE;
    }
    return n >= min;
}

/* position of the closing run of exactly n characters c after i, or 0 */
static ULONG find_close(const char *t, ULONG i, ULONG len, char c, ULONG n)
{
    while (i < len) {
        if (t[i] == '\\') { i += 2; continue; }
        if (t[i] == c) {
            ULONG r = run(t, i, len, c);
            if (r == n) return i;
            i += r;
        } else i++;
    }
    return 0;
}

static ULONG find_char(const char *t, ULONG i, ULONG len, char c)
{
    for (; i < len; i++) {
        if (t[i] == '\\') i++;
        else if (t[i] == c) return i;
    }
    return 0;
}

/* URL characters up to the next space or closing bracket */
static ULONG url_end(const char *t, ULONG i, ULONG len)
{
    while (i < len && !is_space(t[i]) && t[i] != '>' && t[i] != ')' && t[i] != ']') i++;
    while (i > 0 && (t[i - 1] == '.' || t[i - 1] == ',' || t[i - 1] == ';')) i--;
    return i;
}

static BOOL starts(const char *t, ULONG i, ULONG len, const char *s)
{
    while (*s) {
        if (i >= len || t[i] != *s) return FALSE;
        i++, s++;
    }
    return TRUE;
}

/* links: [text](url), [text][ref], [^note]; images with a leading '!' */
static ULONG link(const char *t, ULONG i, ULONG len)
{
    ULONG start = i, close;

    if (t[i] == '!') i++;
    if (!(close = find_char(t, i + 1, len, ']'))) return start + 1;
    fmt(start, close + 1, COLOUR(C_LINK) | TBSTYLE_UNDERLINE);
    if (close + 1 < len && t[close + 1] == '(') {
        ULONG paren = find_char(t, close + 2, len, ')');
        if (paren) {
            fmt(close + 1, paren + 1, COLOUR(C_URL));
            return paren + 1;
        }
    } else if (close + 1 < len && t[close + 1] == '[') {
        ULONG ref = find_char(t, close + 2, len, ']');
        if (ref) {
            fmt(close + 1, ref + 1, COLOUR(C_URL));
            return ref + 1;
        }
    }
    return close + 1;
}

/* inline elements from position i on */
static void inline_span(const char *t, ULONG i, ULONG len)
{
    ULONG skip[8], nskip = 0, k;     /* closing emphasis runs already used */

    while (i < len) {
        char c = t[i];

        for (k = 0; k < nskip; ) {
            if (skip[k] == i) {                     /* end of an emphasis */
                i += run(t, i, len, c);
                skip[k] = skip[--nskip];
                k = nskip + 1;
            } else if (skip[k] < i) skip[k] = skip[--nskip];    /* jumped over */
            else k++;
        }
        if (k > nskip) continue;

        if (c == '\\' && i + 1 < len) {
            i += 2;
        } else if (c == '`') {
            ULONG n = run(t, i, len, '`'), close = find_close(t, i + n, len, '`', n);
            if (close) {
                fmt(i, close + n, COLOUR(C_CODE));
                i = close + n;
            } else i += n;
        } else if (c == '*' || c == '_' || (c == '~' && i + 1 < len && t[i + 1] == '~')) {
            ULONG n = run(t, i, len, c), close;
            /* opener: not followed by a space; '_' not inside a word */
            if (i + n >= len || is_space(t[i + n]) || (c == '_' && i > 0 && is_alnum(t[i - 1])) ||
                !(close = find_close(t, i + n, len, c, n)) || is_space(t[close - 1])) {
                i += n;
                continue;
            }
            if (c == '~') fmt(i + n, close, COLOUR(C_QUOTE));
            else fmt(i + n, close, n == 1 ? TBSTYLE_ITALIC :
                                   n == 2 ? TBSTYLE_BOLD : TBSTYLE_BOLD | TBSTYLE_ITALIC);
            fmt(i, i + n, COLOUR(C_MARKER));
            fmt(close, close + n, COLOUR(C_MARKER));
            if (nskip < 8) skip[nskip++] = close;
            i += n;
        } else if (c == '[' || (c == '!' && i + 1 < len && t[i + 1] == '[')) {
            i = link(t, i, len);
        } else if (c == '<') {
            ULONG close = find_char(t, i + 1, len, '>');
            if (close && (starts(t, i + 1, len, "http") || starts(t, i + 1, len, "mailto:") ||
                          find_char(t, i + 1, close, '@'))) {
                fmt(i, close + 1, COLOUR(C_URL) | TBSTYLE_UNDERLINE);
                i = close + 1;
            } else if (close && i + 1 < len &&
                       (t[i + 1] == '/' || t[i + 1] == '!' || is_alnum(t[i + 1]))) {
                fmt(i, close + 1, COLOUR(C_HTML));
                i = close + 1;
            } else i++;
        } else if (c == '&') {
            ULONG j = i + 1;
            if (j < len && t[j] == '#') j++;
            while (j < len && is_alnum(t[j]) && j - i < 10) j++;
            if (j < len && t[j] == ';' && j > i + 1) {
                fmt(i, j + 1, COLOUR(C_HTML));
                i = j + 1;
            } else i++;
        } else if ((c == 'h' && (starts(t, i, len, "http://") || starts(t, i, len, "https://"))) ||
                   (c == 'w' && starts(t, i, len, "www."))) {
            if (i == 0 || !is_alnum(t[i - 1])) {
                ULONG e = url_end(t, i, len);
                fmt(i, e, COLOUR(C_URL) | TBSTYLE_UNDERLINE);
                i = e;
            } else i++;
        } else i++;
    }
}

/* list marker at i: "- ", "* ", "+ ", "1. ", "1) "; returns its end or 0 */
static ULONG list_marker(const char *t, ULONG i, ULONG len)
{
    ULONG j = i;
    if (t[i] == '-' || t[i] == '*' || t[i] == '+') j = i + 1;
    else {
        while (j < len && j - i < 9 && t[j] >= '0' && t[j] <= '9') j++;
        if (j == i || j >= len || (t[j] != '.' && t[j] != ')')) return 0;
        j++;
    }
    return j == len || is_space(t[j]) ? j : 0;
}

static ULONG highlight_line(const char *t, ULONG status)
{
    ULONG len = 0, i, j;

    while (t[len] && t[len] != '\n') len++;
    if (len && t[len - 1] == '\r') len--;

    /* inside a code fence: everything is code up to the closing fence */
    if (status & ST_FENCE) {
        fmt(0, len, COLOUR(C_CODE));
        for (i = 0; i < 3 && i < len && t[i] == ' '; i++) ;
        if (run(t, i, len, FENCE_CHAR(status)) >= FENCE_LEN(status) &&
            is_rule(t, i, len, FENCE_CHAR(status), FENCE_LEN(status)))
            return 0;
        return status;
    }

    for (i = 0; i < 3 && i < len && t[i] == ' '; i++) ;

    /* block quote markers */
    j = i;
    while (j < len && t[j] == '>') {
        j++;
        while (j < len && is_space(t[j])) j++;
    }
    if (j > i) {
        fmt(i, j, COLOUR(C_MARKER) | TBSTYLE_BOLD);
        fmt(j, len, COLOUR(C_QUOTE));
        i = j;
    }

    /* opening code fence */
    if (i < len && (t[i] == '`' || t[i] == '~')) {
        ULONG n = run(t, i, len, t[i]);
        if (n >= 3 && (t[i] == '~' || !find_char(t, i + n, len, '`'))) {
            fmt(i, len, COLOUR(C_CODE));
            return ST_FENCE | (ULONG)(UBYTE)t[i] << 8 | (n > 255 ? 255 : n);
        }
    }

    /* ATX heading */
    if (i < len && t[i] == '#') {
        ULONG n = run(t, i, len, '#');
        if (n <= 6 && (i + n == len || is_space(t[i + n]))) {
            fmt(i, len, COLOUR(C_HEADING) | TBSTYLE_BOLD);
            return 0;
        }
    }

    /* setext underline, horizontal rule */
    if (i < len && is_rule(t, i, len, '=', 1)) {
        fmt(i, len, COLOUR(C_HEADING) | TBSTYLE_BOLD);
        return 0;
    }
    if (i < len && (is_rule(t, i, len, '-', 2) || is_rule(t, i, len, '*', 3) ||
                    is_rule(t, i, len, '_', 3))) {
        fmt(i, len, COLOUR(C_MARKER) | TBSTYLE_BOLD);
        return 0;
    }

    /* indented code needs the previous line, so here just list items etc. */
    while (i < len && is_space(t[i])) i++;
    if (i < len && (j = list_marker(t, i, len))) {
        fmt(i, j, COLOUR(C_MARKER) | TBSTYLE_BOLD);
        i = j;
        while (i < len && is_space(t[i])) i++;
        /* task list box */
        if (i + 2 < len && t[i] == '[' && t[i + 2] == ']' &&
            (t[i + 1] == ' ' || t[i + 1] == 'x' || t[i + 1] == 'X')) {
            fmt(i, i + 3, COLOUR(C_MARKER));
            i += 3;
        }
    }

    /* table rows: the pipes; the delimiter row completely */
    if (i < len && t[i] == '|') {
        BOOL delim = TRUE;
        for (j = i; j < len; j++) {
            if (t[j] == '|') fmt(j, j + 1, COLOUR(C_MARKER));
            else if (t[j] != '-' && t[j] != ':' && !is_space(t[j])) delim = FALSE;
        }
        if (delim) {
            fmt(i, len, COLOUR(C_MARKER));
            return 0;
        }
    }

    /* link reference definition "[label]: url", footnote "[^label]: text" */
    if (i < len && t[i] == '[' && (j = find_char(t, i + 1, len, ']')) && j + 1 < len && t[j + 1] == ':') {
        fmt(i, j + 2, COLOUR(C_LINK));
        if (t[i + 1] != '^') {
            fmt(j + 2, len, COLOUR(C_URL));
            return 0;
        }
        i = j + 2;
    }

    inline_span(t, i, len);
    return 0;
}

static ULONG hook_func(struct Hook *hook, APTR object, struct HighlightMessage *msg)
{
    ULONG status;

    if (!msg->Text) return 0;
    obj = object;
    status = highlight_line((const char *)msg->Text, msg->StatusOfPrevBlock);
    obj = NULL;
    return status;
}

static struct Hook hook = { { NULL, NULL }, (APTR)HookEntry, (APTR)hook_func, NULL };

/* the hook, or NULL if texteditor.gadget is older than V47 */
struct Hook *highlight_hook(void)
{
    return TextFieldBase->lib_Version >= 47 ? &hook : NULL;
}
