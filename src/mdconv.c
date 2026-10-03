/*
 * mdconv - Markdown to HTML conversion shared by Kickdown and mdtohtml
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md4c.h"
#include "md4c-html.h"
#include "mdconv.h"

#ifndef MD4C_VERSION_STR
#define MD4C_VERSION_STR "?"
#endif

/* html.gadget renders HTML 4: keep <br>, <hr> and leave entities to the
 * gadget (or the browser), md4c would turn them into UTF-8 otherwise.  */
#define RENDER_FLAGS  (MD_HTML_FLAG_VERBATIM_ENTITIES | MD_HTML_FLAG_SKIP_UTF8_BOM)

static const char default_template[] =
    "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n"
    "<html>\n"
    "<head>\n"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=$encoding$\">\n"
    "<title>$title$</title>\n"
    "</head>\n"
    "<body>\n"
    "$body$"
    "</body>\n"
    "</html>\n";

/* growing output buffer */
struct buf {
    char  *data;
    size_t len, size;
    int    failed;
};

static void buf_add(struct buf *b, const char *s, size_t n)
{
    if (b->failed) return;
    if (b->len + n + 1 > b->size) {
        size_t nsize = b->size ? b->size : 4096;
        char *d;
        while (b->len + n + 1 > nsize) nsize *= 2;
        if (!(d = realloc(b->data, nsize))) {
            b->failed = 1;
            return;
        }
        b->data = d;
        b->size = nsize;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void md_output(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    buf_add((struct buf *)userdata, text, size);
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int strieq(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return *a == *b;
}

/* case-insensitive prefix test */
static int starts_with(const char *s, const char *prefix)
{
    while (*prefix && lower((unsigned char)*s) == *prefix) s++, prefix++;
    return !*prefix;
}

int mdconv_dialect(const char *name, unsigned *flags)
{
    if (strieq(name, "GitHub") || strieq(name, "GFM")) {
        *flags = MD_DIALECT_GITHUB;
        return 1;
    }
    if (strieq(name, "CommonMark")) {
        *flags = MD_DIALECT_COMMONMARK;
        return 1;
    }
    return 0;
}

unsigned mdconv_default_flags(void)
{
    return MD_DIALECT_GITHUB;
}

const char *mdconv_version(void)
{
    return "md4c " MD4C_VERSION_STR;
}

const char *mdconv_guess_charset(const char *text, size_t size)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + size;
    int utf8 = 0;

    while (p < end) {
        unsigned c = *p++;
        int n;
        if (c < 0x80) continue;
        if (c >= 0xC2 && c <= 0xDF) n = 1;
        else if (c >= 0xE0 && c <= 0xEF) n = 2;
        else if (c >= 0xF0 && c <= 0xF4) n = 3;
        else return "ISO-8859-1";
        if (end - p < n) return "ISO-8859-1";
        while (n--)
            if ((*p++ & 0xC0) != 0x80) return "ISO-8859-1";
        utf8 = 1;
    }
    return utf8 ? "UTF-8" : "ISO-8859-1";
}

/* GitHub style anchor of a heading: the text without tags and entities,
 * ASCII lower case, spaces become '-', other punctuation is dropped.
 * Bytes >= 0x80 (Latin-1 or UTF-8 letters) are kept.                   */
static void make_slug(struct buf *slug, const char *s, const char *end)
{
    int intag = 0;

    slug->len = 0;
    buf_add(slug, "", 0);
    for (; s < end; s++) {
        unsigned char c = *s;
        if (intag) {
            if (c == '>') intag = 0;
        } else if (c == '<') {
            intag = 1;
        } else if (c == '&') {
            const char *semi = s + 1;
            while (semi < end && *semi != ';' && semi - s < 12) semi++;
            if (semi < end && *semi == ';') s = semi;
        } else if (c == ' ' || c == '\n' || c == '-') {
            buf_add(slug, "-", 1);
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80) {
            buf_add(slug, (const char *)&c, 1);
        } else if (c >= 'A' && c <= 'Z') {
            c += 32;
            buf_add(slug, (const char *)&c, 1);
        }
    }
}

/* Copies md4c's body and gives every "<hN>" an id for #anchor links.
 * Repeated anchors get "-1", "-2"... like on GitHub. 'used' collects the
 * anchors, separated by NUL bytes.                                       */
static void add_heading_ids(struct buf *out, const char *html)
{
    struct buf slug = { NULL, 0, 0, 0 }, used = { NULL, 0, 0, 0 };
    const char *p = html, *h, *end;

    while ((h = strstr(p, "<h"))) {
        char level = h[2], tag[6], num[12];
        size_t base;
        int dup = 0;

        if (level < '1' || level > '6' || h[3] != '>') {
            buf_add(out, p, h + 2 - p);
            p = h + 2;
            continue;
        }
        strcpy(tag, "</h_>");
        tag[3] = level;
        if (!(end = strstr(h + 4, tag))) break;

        make_slug(&slug, h + 4, end);
        if (slug.failed) out->failed = 1;
        if (slug.failed || !slug.len) {     /* nothing usable: leave as is */
            buf_add(out, p, h + 4 - p);
            p = h + 4;
            continue;
        }
        /* first free name: slug, slug-1, slug-2 ... */
        base = slug.len;
        for (;;) {
            const char *u;
            for (u = used.data; u && u < used.data + used.len; u += strlen(u) + 1)
                if (!strcmp(u, slug.data)) break;
            if (!u || u >= used.data + used.len) break;
            slug.len = base;
            sprintf(num, "-%d", ++dup);
            buf_add(&slug, num, strlen(num));
        }
        buf_add(&used, slug.data, slug.len + 1);

        buf_add(out, p, h + 3 - p);         /* up to "<hN" */
        buf_add(out, " id=\"", 5);
        buf_add(out, slug.data, slug.len);
        buf_add(out, "\"", 1);
        p = h + 3;
    }
    buf_add(out, p, strlen(p));
    if (used.failed) out->failed = 1;
    free(slug.data);
    free(used.data);
}

/* Task list items without a list marker, the checkbox takes its place:
 * type="none" is what the HTML standard maps to list-style-type: none,
 * browsers and html.gadget (1.1 and up) follow it.                   */
static void plain_task_items(struct buf *out, const char *html)
{
    static const char item[] = "<li class=\"task-list-item\">";
    static const char plain[] = "<li class=\"task-list-item\" type=\"none\">";
    const char *p = html, *q;

    while ((q = strstr(p, item))) {
        buf_add(out, p, q - p);
        buf_add(out, plain, sizeof(plain) - 1);
        p = q + sizeof(item) - 1;
    }
    buf_add(out, p, strlen(p));
}

/* Text of the first <h1> without tags, or NULL. Entities stay as they
 * are, they are valid in <title>.                                       */
static char *first_heading(const char *html)
{
    const char *s = strstr(html, "<h1"), *e;
    char *title, *d;
    int intag = 0;

    if (!s || !(s = strchr(s, '>'))) return NULL;
    s++;
    if (!(e = strstr(s, "</h1>"))) return NULL;
    if (!(title = malloc(e - s + 1))) return NULL;
    for (d = title; s < e; s++) {
        if (*s == '<') intag = 1;
        else if (*s == '>') intag = 0;
        else if (!intag) *d++ = *s == '\n' ? ' ' : *s;
    }
    *d = 0;
    if (!*title) {
        free(title);
        return NULL;
    }
    return title;
}

/* appends s with &, <, > and " escaped */
static void add_escaped(struct buf *b, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&': buf_add(b, "&amp;", 5); break;
        case '<': buf_add(b, "&lt;", 4); break;
        case '>': buf_add(b, "&gt;", 4); break;
        case '"': buf_add(b, "&quot;", 6); break;
        default:  buf_add(b, s, 1); break;
        }
    }
}

char *mdconv_html(const char *md, size_t size, const struct MDConvOptions *opt, size_t *len)
{
    struct buf raw = { NULL, 0, 0, 0 }, tasks = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 },
               page = { NULL, 0, 0, 0 };
    const char *tmpl = opt->tmpl ? opt->tmpl : default_template;
    const char *charset = opt->charset ? opt->charset : mdconv_guess_charset(md, size);
    char *heading = NULL;
    const char *p;

    buf_add(&raw, "", 0);
    if (md_html(md, (MD_SIZE)size, md_output, &raw, opt->flags, RENDER_FLAGS) == 0 && !raw.failed)
        plain_task_items(&tasks, raw.data);
    if (tasks.data && !tasks.failed)
        add_heading_ids(&body, tasks.data);
    free(raw.data);
    free(tasks.data);
    if (!body.data || raw.failed || tasks.failed || body.failed) {
        free(body.data);
        return NULL;
    }
    if (!opt->title) heading = first_heading(body.data);

    for (p = tmpl; *p; ) {
        const char *dollar = strchr(p, '$');
        if (!dollar) {
            buf_add(&page, p, strlen(p));
            break;
        }
        buf_add(&page, p, dollar - p);
        p = dollar;
        if (starts_with(p, "$title$")) {
            if (opt->title) add_escaped(&page, opt->title);
            else if (heading) buf_add(&page, heading, strlen(heading));
            else add_escaped(&page, opt->fallback_title ? opt->fallback_title : "Untitled");
            p += 7;
        } else if (starts_with(p, "$encoding$")) {
            add_escaped(&page, charset);
            p += 10;
        } else if (starts_with(p, "$charset$")) {
            add_escaped(&page, charset);
            p += 9;
        } else if (starts_with(p, "$body$")) {
            buf_add(&page, body.data, body.len);
            p += 6;
        } else {
            buf_add(&page, "$", 1);
            p++;
        }
    }
    buf_add(&page, "", 0);

    free(heading);
    free(body.data);
    if (page.failed) {
        free(page.data);
        return NULL;
    }
    if (len) *len = page.len;
    return page.data;
}

void mdconv_free(char *html)
{
    free(html);
}
