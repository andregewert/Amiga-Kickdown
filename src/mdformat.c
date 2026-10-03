/*
 * mdformat - Markdown formatting commands of Kickdown, see mdformat.h
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mdformat.h"

/* growing output buffer */
struct out {
    char *buf;
    size_t len, size;
    int failed;
};

static void put(struct out *o, const char *s, size_t n)
{
    if (o->failed) return;
    if (o->len + n + 1 > o->size) {
        size_t size = (o->len + n + 1) * 2 + 64;
        char *b = realloc(o->buf, size);
        if (!b) {
            o->failed = 1;
            return;
        }
        o->buf = b;
        o->size = size;
    }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
    o->buf[o->len] = 0;
}

static void puts_(struct out *o, const char *s)
{
    put(o, s, strlen(s));
}

static char *finish(struct out *o)
{
    if (o->failed || !o->buf) {
        free(o->buf);
        if (o->failed) return NULL;
        return calloc(1, 1);
    }
    return o->buf;
}

/*****************************************************************************/
/* inline                                                                    */

static size_t wrapped_md(int kind, const char *s, size_t len);

static int has(const char *s, size_t len, const char *m)
{
    size_t n = strlen(m);
    return len >= 2 * n && !memcmp(s, m, n) && !memcmp(s + len - n, m, n);
}

/* opening and closing marker of an inline format; Markdown has no
 * underline, HTML is passed through (md4c, GitHub dialect)        */
static const char *open_marker(int kind)
{
    return kind == FMT_BOLD ? "**" : kind == FMT_ITALIC ? "*" : kind == FMT_UNDERLINE ? "<u>" : "`";
}

static const char *close_marker(int kind)
{
    return kind == FMT_UNDERLINE ? "</u>" : open_marker(kind);
}

static int has_tag(const char *s, size_t len, const char *open, const char *close)
{
    size_t a = strlen(open), b = strlen(close), i;
    if (len < a + b) return 0;
    for (i = 0; i < a; i++)
        if (tolower((unsigned char)s[i]) != open[i]) return 0;
    for (i = 0; i < b; i++)
        if (tolower((unsigned char)s[len - b + i]) != close[i]) return 0;
    return 1;
}

/* Is the text formatted already? Returns the length of the opening
 * marker and sets *close to the one of the closing marker, 0 if not.
 * Bold is "**" or "__", italic "*" or "_" but not bold: "**x**" is not
 * italic, "***x***" is both.                                          */
static size_t wrapped(int kind, const char *s, size_t len, size_t *close)
{
    *close = 0;
    if (kind == FMT_UNDERLINE) {
        if (!has_tag(s, len, "<u>", "</u>")) return 0;
        *close = 4;
        return 3;
    }
    *close = wrapped_md(kind, s, len);
    return *close;
}

static size_t wrapped_md(int kind, const char *s, size_t len)
{
    if (kind == FMT_BOLD)
        return has(s, len, "**") || has(s, len, "__") ? 2 : 0;
    if (kind == FMT_ITALIC) {
        if (has(s, len, "***") || has(s, len, "___")) return 1;
        if (has(s, len, "**") || has(s, len, "__")) return 0;
        return has(s, len, "*") || has(s, len, "_") ? 1 : 0;
    }
    return has(s, len, "`") ? 1 : 0;
}

static int is_url(const char *s, size_t len)
{
    static const char *const prefix[] = { "http://", "https://", "ftp://", "mailto:", "www.", NULL };
    int i;
    for (i = 0; prefix[i]; i++) {
        size_t n = strlen(prefix[i]);
        if (len >= n && !memcmp(s, prefix[i], n)) return 1;
    }
    return 0;
}

static char *inline_lines(int kind, const char *sel, size_t len, size_t *mark1);

char *mdfmt_inline(int kind, const char *sel, size_t len, size_t *mark0, size_t *mark1)
{
    struct out o = { 0 };
    size_t n, c;

    *mark0 = *mark1 = 0;
    if ((kind == FMT_BOLD || kind == FMT_ITALIC || kind == FMT_UNDERLINE) && memchr(sel, '\n', len))
        return inline_lines(kind, sel, len, mark1);
    if (kind == FMT_LINK || kind == FMT_IMAGE) {
        if (kind == FMT_IMAGE) puts_(&o, "!");
        puts_(&o, "[");
        if (len && is_url(sel, len)) {
            /* the address is selected: the cursor into the text */
            *mark0 = *mark1 = o.len;
            puts_(&o, "](");
            put(&o, sel, len);
            puts_(&o, ")");
        } else {
            put(&o, sel, len);
            puts_(&o, "](");
            *mark0 = *mark1 = len ? o.len : o.len - 2;
            puts_(&o, ")");
        }
        return finish(&o);
    }
    if (kind == FMT_CODE && memchr(sel, '\n', len)) {
        /* several lines: a fenced code block */
        if (len >= 8 && !memcmp(sel, "```\n", 4) && !memcmp(sel + len - 4, "\n```", 4)) {
            put(&o, sel + 4, len - 8);
            *mark1 = o.len;
            return finish(&o);
        }
        puts_(&o, "```\n");
        *mark0 = o.len;
        put(&o, sel, len);
        *mark1 = o.len;
        puts_(&o, "\n```");
        return finish(&o);
    }
    if ((n = wrapped(kind, sel, len, &c))) {
        /* already formatted: remove the markers, the text stays marked */
        put(&o, sel + n, len - n - c);
        *mark1 = o.len;
        return finish(&o);
    }
    puts_(&o, open_marker(kind));
    *mark0 = o.len;
    put(&o, sel, len);
    *mark1 = o.len;
    puts_(&o, close_marker(kind));
    return finish(&o);
}

/*****************************************************************************/
/* lines                                                                     */

struct line {
    const char *s;
    size_t len;
    size_t indent;              /* leading blanks */
    size_t heading;             /* number of '#' of a heading, 0: none */
    size_t marker;              /* length of the list marker after the indent */
    int list;                   /* FMT_BULLET, FMT_NUMBERED, FMT_TASK or -1 */
};

static int blank(char c)
{
    return c == ' ' || c == '\t';
}

static void parse(struct line *l)
{
    const char *s = l->s + l->indent;
    size_t rest = l->len - l->indent, i = 0;

    l->heading = l->marker = 0;
    l->list = -1;
    while (i < rest && i < 6 && s[i] == '#') i++;
    if (i && (i == rest || s[i] == ' ')) l->heading = i;

    if (rest >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
        l->list = FMT_BULLET;
        l->marker = 2;
        if (rest >= 6 && s[2] == '[' && (s[3] == ' ' || s[3] == 'x' || s[3] == 'X') &&
            s[4] == ']' && s[5] == ' ') {
            l->list = FMT_TASK;
            l->marker = 6;
        }
        return;
    }
    for (i = 0; i < rest && i < 9 && s[i] >= '0' && s[i] <= '9'; i++) ;
    if (i && i + 1 < rest && (s[i] == '.' || s[i] == ')') && s[i + 1] == ' ') {
        l->list = FMT_NUMBERED;
        l->marker = i + 2;
    }
}

static size_t count_lines(const char *text, size_t len)
{
    size_t n = 1, i;
    for (i = 0; i < len; i++)
        if (text[i] == '\n') n++;
    return n;
}

static int empty(const struct line *l)
{
    return l->indent == l->len;
}

static void number(struct out *o, unsigned long n)
{
    char buf[16];
    int i = sizeof(buf);
    buf[--i] = 0;
    do buf[--i] = (char)('0' + n % 10); while ((n /= 10) && i > 0);
    puts_(o, buf + i);
}

char *mdfmt_lines(int kind, const char *text, size_t len)
{
    struct out o = { 0 };
    struct line *lines;
    size_t n = count_lines(text, len), i, pos = 0, level = 0;
    int all = 1, any = 0;
    unsigned long num = 1;

    if (!(lines = calloc(n, sizeof(*lines)))) return NULL;
    for (i = 0; i < n; i++) {
        const char *e = memchr(text + pos, '\n', len - pos);
        struct line *l = &lines[i];
        l->s = text + pos;
        l->len = e ? (size_t)(e - (text + pos)) : len - pos;
        while (l->indent < l->len && blank(l->s[l->indent])) l->indent++;
        parse(l);
        pos += l->len + 1;
        if (empty(l)) continue;
        any = 1;
        if (kind == FMT_QUOTE ? l->s[l->indent] != '>' : l->list != kind) all = 0;
    }
    if (!any) all = 0;

    /* heading: the first line decides, every click one level more, after
     * ### back to text                                                  */
    if (kind == FMT_HEADING) {
        for (i = 0; i < n && empty(&lines[i]); i++) ;
        level = i < n ? lines[i].heading + 1 : 1;
        if (level > 3) level = 0;
    }

    for (i = 0; i < n; i++) {
        struct line *l = &lines[i];
        const char *s = l->s + l->indent;
        size_t rest = l->len - l->indent;

        if (i) puts_(&o, "\n");
        if (kind == FMT_QUOTE) {
            if (all) {
                /* one level less */
                put(&o, l->s, l->indent);
                if (rest && *s == '>') {
                    s++, rest--;
                    if (rest && *s == ' ') s++, rest--;
                }
                put(&o, s, rest);
            } else {
                puts_(&o, rest || n == 1 ? "> " : ">");
                put(&o, l->s, l->len);
            }
            continue;
        }
        if (empty(l)) {
            /* an empty line alone gets the marker, else it stays */
            if (n == 1) {
                put(&o, l->s, l->len);
                if (kind == FMT_HEADING) {
                    size_t k;
                    for (k = 0; k < (level ? level : 1); k++) puts_(&o, "#");
                    puts_(&o, " ");
                } else if (kind == FMT_BULLET) puts_(&o, "- ");
                else if (kind == FMT_NUMBERED) puts_(&o, "1. ");
                else if (kind == FMT_TASK) puts_(&o, "- [ ] ");
            } else put(&o, l->s, l->len);
            continue;
        }
        if (kind == FMT_HEADING) {
            size_t k;
            /* the heading marker replaces '#'s and a list marker */
            if (l->heading) {
                s += l->heading, rest -= l->heading;
            } else if (l->marker) {
                s += l->marker, rest -= l->marker;
            }
            while (rest && *s == ' ') s++, rest--;
            for (k = 0; k < level; k++) puts_(&o, "#");
            if (level) puts_(&o, " ");
            put(&o, s, rest);
            continue;
        }
        /* lists: keep the indent, replace or remove the marker */
        put(&o, l->s, l->indent);
        if (l->list >= 0) s += l->marker, rest -= l->marker;
        if (!all) {
            if (kind == FMT_BULLET) puts_(&o, "- ");
            else if (kind == FMT_TASK) puts_(&o, "- [ ] ");
            else {
                number(&o, num++);
                puts_(&o, ". ");
            }
        }
        put(&o, s, rest);
    }
    free(lines);
    return finish(&o);
}

/*****************************************************************************/
/* inline format over several lines                                          */

/* length of the line's markers: indent, quote '>'s, heading '#'s or list
 * marker and the blanks after them                                    */
static size_t prefix(const char *s, size_t len)
{
    struct line l;
    size_t i = 0;

    while (i < len && (blank(s[i]) || s[i] == '>')) i++;
    l.s = s + i;
    l.len = len - i;
    l.indent = 0;
    parse(&l);
    i += l.heading ? l.heading : l.marker;
    while (i < len && blank(s[i])) i++;
    return i;
}

/* the content of line s: start *a and end *b (without trailing blanks) */
static void content(const char *s, size_t len, size_t *a, size_t *b)
{
    *a = prefix(s, len);
    *b = len;
    while (*b > *a && blank(s[*b - 1])) (*b)--;
}

/* Bold, italic or underline on each line of the selection: the markers go around
 * the content after list, quote and heading markers. If all lines are
 * formatted already, the markers are removed. Empty lines stay.       */
static char *inline_lines(int kind, const char *sel, size_t len, size_t *mark1)
{
    struct out o = { 0 };
    size_t pos, a, b, c;
    int all = 1, any = 0;

    for (pos = 0; pos <= len; ) {
        const char *e = memchr(sel + pos, '\n', len - pos);
        size_t l = e ? (size_t)(e - (sel + pos)) : len - pos;
        content(sel + pos, l, &a, &b);
        if (b > a) {
            any = 1;
            if (!wrapped(kind, sel + pos + a, b - a, &c)) all = 0;
        }
        pos += l + 1;
    }
    if (!any) all = 0;

    for (pos = 0; pos <= len; ) {
        const char *e = memchr(sel + pos, '\n', len - pos);
        size_t l = e ? (size_t)(e - (sel + pos)) : len - pos, w;
        const char *s = sel + pos;
        if (pos) puts_(&o, "\n");
        content(s, l, &a, &b);
        put(&o, s, a);
        w = b > a ? wrapped(kind, s + a, b - a, &c) : 0;
        if (b == a) ;
        else if (all) put(&o, s + a + w, b - a - w - c);
        else if (w) put(&o, s + a, b - a);
        else {
            puts_(&o, open_marker(kind));
            put(&o, s + a, b - a);
            puts_(&o, close_marker(kind));
        }
        put(&o, s + b, l - b);
        pos += l + 1;
    }
    *mark1 = o.len;
    return finish(&o);
}
