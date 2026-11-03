/*
 * mdtree - converts a directory tree of Markdown files into a tree of
 * HTML pages (mdtohtml ALL)
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdtree.h"

#define PATHMAX  512
#define MAXDEPTH 16     /* the same bound as TinyDev's directory tree */

/* A local link, checked when all pages are written. The strings follow
 * the structure in the same allocation.                                  */
struct link {
    struct link *next;
    const char  *source;    /* the Markdown file, relative to the tree */
    const char  *url;       /* as written there */
    const char  *target;    /* path in the tree, NULL if the link is bad */
    const char  *anchor;    /* decoded, "" if none */
};

struct tree {
    const char *from, *to;
    struct MDConvOptions opt;
    char rel[PATHMAX];          /* the file at hand, relative to the tree */
    char page[PATHMAX];         /* the page being made, relative to the tree */
    char src_path[PATHMAX], dst_path[PATHMAX];  /* not kept over a recursion */
    char link_out[PATHMAX];     /* the changed link handed to mdconv */
    char title[108];            /* fallback title of the page */
    char root[3 * MAXDEPTH + 1];    /* $root$ of the page: "../" per drawer */
    struct link *links, *last;
    unsigned long pages, copied, warnings;
    int out_of_memory;
};

static struct tree tree;

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* non-zero if name ends in ext, which is lower case */
static int has_ext(const char *name, size_t len, const char *ext)
{
    size_t n = strlen(ext), i;

    if (len <= n) return 0;
    name += len - n;
    for (i = 0; i < n; i++)
        if (lower((unsigned char)name[i]) != ext[i]) return 0;
    return 1;
}

/* length of a Markdown name's extension, 0 if it has none of them */
static size_t markdown_ext(const char *name, size_t len)
{
    if (has_ext(name, len, ".md")) return 3;
    if (has_ext(name, len, ".markdown")) return 9;
    return 0;
}

int mdtree_is_markdown(const char *name)
{
    return markdown_ext(name, strlen(name)) != 0;
}

int mdtree_is_skipped(const char *name, int is_dir)
{
    if (is_dir) return name[0] == '.';
    return has_ext(name, strlen(name), ".info");
}

int mdtree_html_name(const char *name, char *out, size_t size)
{
    size_t len = strlen(name), ext = markdown_ext(name, len);

    if (len - ext + 6 > size) return 0;
    memcpy(out, name, len - ext);
    strcpy(out + len - ext, ".html");
    return 1;
}

/* non-zero if the link starts with a scheme other than file: */
static int is_external(const char *url)
{
    const char *p = url;

    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return 0;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
           *p == '+' || *p == '-' || *p == '.')
        p++;
    if (*p != ':') return 0;
    return !(p - url == 4 && !strncmp(url, "file", 4));
}

/* length of the path in a link, up to #anchor or ?query */
static size_t path_length(const char *url)
{
    return strcspn(url, "#?");
}

int mdtree_rewrite_link(const char *url, char *out, size_t size)
{
    size_t path, ext, rest;

    if (is_external(url)) return 0;
    path = path_length(url);
    if (!(ext = markdown_ext(url, path))) return 0;
    rest = strlen(url + path);
    if (path - ext + 5 + rest + 1 > size) return 0;
    memcpy(out, url, path - ext);
    memcpy(out + path - ext, ".html", 5);
    strcpy(out + path - ext + 5, url + path);
    return 1;
}

int mdtree_link_target(const char *page, const char *url, char *path, size_t path_size,
                       char *anchor, size_t anchor_size)
{
    char decoded[PATHMAX];
    const char *s, *slash;
    size_t len, n;

    if (is_external(url)) return MDTREE_LINK_EXTERNAL;
    if (!strncmp(url, "file:", 5)) url += 5;

    len = path_length(url);
    anchor[0] = 0;
    if (url[len] == '#' &&
        !mdconv_url_decode(url + len + 1, strlen(url + len + 1), anchor, anchor_size))
        return MDTREE_LINK_BAD;
    if (!mdconv_url_decode(url, len, decoded, sizeof(decoded))) return MDTREE_LINK_BAD;
    if (decoded[0] == '/') return MDTREE_LINK_BAD;
    if (!decoded[0]) {
        if (strlen(page) >= path_size) return MDTREE_LINK_BAD;
        strcpy(path, page);
        return MDTREE_LINK_LOCAL;
    }

    /* start in the page's drawer, then part by part */
    slash = strrchr(page, '/');
    n = slash ? (size_t)(slash - page) : 0;
    if (n >= path_size) return MDTREE_LINK_BAD;
    memcpy(path, page, n);
    path[n] = 0;

    for (s = decoded; *s; ) {
        size_t part = strcspn(s, "/");
        if (part == 2 && s[0] == '.' && s[1] == '.') {
            char *up;
            if (!n) return MDTREE_LINK_BAD;     /* leaves the tree */
            up = strrchr(path, '/');
            n = up ? (size_t)(up - path) : 0;
            path[n] = 0;
        } else if (part && !(part == 1 && s[0] == '.')) {
            if (n + 1 + part + 1 > path_size) return MDTREE_LINK_BAD;
            if (n) path[n++] = '/';
            memcpy(path + n, s, part);
            n += part;
            path[n] = 0;
        }
        s += part;
        if (*s == '/') s++;
    }
    return MDTREE_LINK_LOCAL;
}

/* non-zero if the attribute name ending just before value_eq (at '=') is
 * "id" or "name", as a word of its own                                    */
static int is_anchor_attribute(const char *html, const char *value_eq)
{
    static const char *const names[] = { "id", "name" };
    int i;

    for (i = 0; i < 2; i++) {
        size_t n = strlen(names[i]), k;
        if ((size_t)(value_eq - html) <= n) continue;
        for (k = 0; k < n; k++)
            if (lower((unsigned char)value_eq[k - n]) != names[i][k]) break;
        if (k == n && (value_eq[-(long)n - 1] == ' ' || value_eq[-(long)n - 1] == '\n' ||
                       value_eq[-(long)n - 1] == '\t'))
            return 1;
    }
    return 0;
}

int mdtree_has_anchor(const char *html, const char *anchor)
{
    size_t len = strlen(anchor);
    const char *p = html;

    if (!len) return 1;
    while ((p = strstr(p, anchor))) {
        const char *eq = NULL;
        int after = (unsigned char)p[len];

        if (p > html && (p[-1] == '"' || p[-1] == '\'')) {
            if (after == p[-1] && p - html >= 2 && p[-2] == '=') eq = p - 2;
        } else if (p > html && p[-1] == '=') {
            if (after == ' ' || after == '>' || after == '\n' || after == '\t') eq = p - 1;
        }
        if (eq && is_anchor_attribute(html, eq)) return 1;
        p++;
    }
    return 0;
}

/* a + '/' + b into out, without a '/' where it would change the meaning;
 * zero if it does not fit                                                */
static int join(char *out, size_t size, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    int sep = la && lb && a[la - 1] != '/' && a[la - 1] != ':';

    if (la + sep + lb + 1 > size) return 0;
    memmove(out, a, la);    /* out may be a: the path grows in place */
    if (sep) out[la] = '/';
    strcpy(out + la + sep, b);
    return 1;
}

static void print3(const char *a, const char *b, const char *c)
{
    mdfs_print(a);
    mdfs_print(b);
    mdfs_print(c);
}

static void warn_dead(const struct link *l, const char *what)
{
    char path[PATHMAX];

    tree.warnings++;
    if (join(path, sizeof(path), tree.from, l->source)) mdfs_print(path);
    else mdfs_print(l->source);
    print3(what, l->url, "\"\n");
}

/* Called by mdconv for each link of a page: notes it for the check and
 * changes links to Markdown files.                                       */
static const char *page_link(const char *url, int image, void *user)
{
    char path[PATHMAX], anchor[PATHMAX];
    const char *result = NULL;
    const char *used = url;
    size_t lrel, lurl, lpath = 0, lanchor = 0;
    struct link *l;
    char *d;
    int kind;

    (void)user;
    if (!image && mdtree_rewrite_link(url, tree.link_out, sizeof(tree.link_out)))
        used = result = tree.link_out;

    kind = mdtree_link_target(tree.page, used, path, sizeof(path), anchor, sizeof(anchor));
    if (kind == MDTREE_LINK_EXTERNAL) return result;
    if (kind == MDTREE_LINK_LOCAL) {
        lpath = strlen(path) + 1;
        lanchor = strlen(anchor) + 1;
    }

    lrel = strlen(tree.rel) + 1;
    lurl = strlen(url) + 1;
    if (!(l = malloc(sizeof(*l) + lrel + lurl + lpath + lanchor))) {
        tree.out_of_memory = 1;
        return result;
    }
    d = (char *)(l + 1);
    l->next = NULL;
    l->source = memcpy(d, tree.rel, lrel);
    d += lrel;
    l->url = memcpy(d, url, lurl);
    d += lurl;
    l->target = NULL;
    l->anchor = "";
    if (kind == MDTREE_LINK_LOCAL) {
        l->target = memcpy(d, path, lpath);
        d += lpath;
        l->anchor = memcpy(d, anchor, lanchor);
    }
    if (tree.last) tree.last->next = l;
    else tree.links = l;
    tree.last = l;
    return result;
}

/* file name without extension as fallback title */
static void set_title(const char *name)
{
    char *dot;

    strncpy(tree.title, name, sizeof(tree.title) - 1);
    tree.title[sizeof(tree.title) - 1] = 0;
    if ((dot = strrchr(tree.title, '.')) && dot != tree.title) *dot = 0;
}

/* $root$ of the page tree.page: the way up to the root of the tree. The
 * walk's depth bound makes sure it fits.                                 */
static void set_root(void)
{
    const char *p;
    size_t n = 0;

    for (p = tree.page; *p; p++)
        if (*p == '/' && n + 4 <= sizeof(tree.root)) {
            memcpy(tree.root + n, "../", 3);
            n += 3;
        }
    tree.root[n] = 0;
}

/* Would the page of this Markdown file overwrite another file of the
 * tree? a.md with a.html, or a.markdown with a.md.                       */
static int page_conflicts(const char *name, const char *html_name)
{
    char other[MDFS_NAMEMAX + 8];

    /* src_path holds the Markdown file: replace its name */
    {
        size_t base = strlen(tree.src_path) - strlen(name);
        if (base + strlen(html_name) + 1 > sizeof(tree.src_path)) return 0;
        strcpy(tree.src_path + base, html_name);
        if (mdfs_exists(tree.src_path) != MDFS_NONE) {
            print3(tree.src_path, ": would be overwritten by the page of ", name);
            mdfs_print("\n");
            return 1;
        }
        if (has_ext(name, strlen(name), ".markdown")) {
            strcpy(other, name);
            strcpy(other + strlen(other) - 9, ".md");
            strcpy(tree.src_path + base, other);
            if (mdfs_exists(tree.src_path) != MDFS_NONE) {
                print3(tree.src_path, ": gives the same page as ", name);
                mdfs_print("\n");
                return 1;
            }
        }
        strcpy(tree.src_path + base, name);
    }
    return 0;
}

/* Converts the file tree.rel (in the drawer of tree.src_path). Returns
 * zero after an error.                                                    */
static int make_page(const char *name)
{
    char html_name[MDFS_NAMEMAX + 8];
    char *md, *html;
    size_t mdlen, htmllen, base;

    if (!mdtree_html_name(name, html_name, sizeof(html_name))) return 0;
    if (page_conflicts(name, html_name)) return 0;

    /* the page's path: tree.rel with the new name */
    base = strlen(tree.rel) - strlen(name);
    if (base + strlen(html_name) + 1 > sizeof(tree.page)) {
        print3("mdtohtml: path too long: ", tree.rel, "\n");
        return 0;
    }
    memcpy(tree.page, tree.rel, base);
    strcpy(tree.page + base, html_name);
    if (!join(tree.dst_path, sizeof(tree.dst_path), tree.to, tree.page)) {
        print3("mdtohtml: path too long: ", tree.page, "\n");
        return 0;
    }

    if (!(md = mdfs_read(tree.src_path, &mdlen))) {
        mdfs_fault(tree.src_path);
        return 0;
    }
    set_title(name);
    tree.opt.fallback_title = tree.title;
    set_root();
    tree.opt.root = tree.root;
    html = mdconv_html(md, mdlen, &tree.opt, &htmllen);
    mdfs_free(md);
    if (!html || tree.out_of_memory) {
        if (html) mdconv_free(html);
        print3("mdtohtml: out of memory at ", tree.src_path, "\n");
        return 0;
    }
    if (!mdfs_write(tree.dst_path, html, htmllen)) {
        mdfs_fault(tree.dst_path);
        mdconv_free(html);
        return 0;
    }
    mdconv_free(html);
    tree.pages++;
    return 1;
}

/* Goes through the drawer tree.rel. Returns zero after an error, which is
 * already reported.                                                       */
static int walk_source(int depth)
{
    char name[MDFS_NAMEMAX];
    size_t rel_len = strlen(tree.rel);
    void *dir;
    int is_dir, more, ok = 1;

    if (!join(tree.src_path, sizeof(tree.src_path), tree.from, tree.rel) ||
        !(dir = mdfs_open_dir(tree.src_path))) {
        mdfs_fault(tree.src_path);
        return 0;
    }
    while (ok && (more = mdfs_next(dir, name, &is_dir)) > 0) {
        if (mdfs_break()) {
            mdfs_print("mdtohtml: stopped\n");
            ok = 0;
            break;
        }
        if (mdtree_is_skipped(name, is_dir)) continue;

        if (!join(tree.rel, sizeof(tree.rel), tree.rel, name) ||
            !join(tree.src_path, sizeof(tree.src_path), tree.from, tree.rel) ||
            !join(tree.dst_path, sizeof(tree.dst_path), tree.to, tree.rel)) {
            print3("mdtohtml: path too long: ", name, "\n");
            ok = 0;
        } else if (is_dir) {
            if (depth + 1 >= MAXDEPTH) {
                print3(tree.src_path, ": too deep, left out", "\n");
                tree.warnings++;
            } else if (!mdfs_make_dir(tree.dst_path)) {
                mdfs_fault(tree.dst_path);
                ok = 0;
            } else {
                ok = walk_source(depth + 1);
            }
        } else if (mdtree_is_markdown(name)) {
            ok = make_page(name);
        } else if (mdfs_copy(tree.src_path, tree.dst_path)) {
            tree.copied++;
        } else {
            mdfs_fault(tree.dst_path);
            ok = 0;
        }
        tree.rel[rel_len] = 0;
    }
    if (ok && more < 0) {
        join(tree.src_path, sizeof(tree.src_path), tree.from, tree.rel);
        mdfs_fault(tree.src_path);
        ok = 0;
    }
    mdfs_close_dir(dir);
    return ok;
}

/* The pages are written: reports links to nothing. Reading the page of an
 * anchor is the slow part, so the last one stays in memory.               */
static int check_links(void)
{
    const struct link *l;
    char last[PATHMAX] = "";
    char *data = NULL;
    int ok = 1;

    for (l = tree.links; l; l = l->next) {
        size_t len;

        if (mdfs_break()) {
            mdfs_print("mdtohtml: stopped\n");
            ok = 0;
            break;
        }
        if (!l->target || !join(tree.dst_path, sizeof(tree.dst_path), tree.to, l->target) ||
            mdfs_exists(tree.dst_path) != MDFS_FILE) {
            warn_dead(l, ": dead link \"");
            continue;
        }
        len = strlen(l->target);
        if (!l->anchor[0] || !(has_ext(l->target, len, ".html") || has_ext(l->target, len, ".htm")))
            continue;
        if (strcmp(last, l->target)) {
            mdfs_free(data);
            if (!(data = mdfs_read(tree.dst_path, NULL))) {
                mdfs_fault(tree.dst_path);
                last[0] = 0;
                ok = 0;
                break;
            }
            strcpy(last, l->target);
        }
        if (!mdtree_has_anchor(data, l->anchor)) warn_dead(l, ": dead anchor \"");
    }
    mdfs_free(data);
    return ok;
}

/* Is there a source for this file of the target? tree.rel is its path, its
 * name is at its end.                                                     */
static int has_source(const char *name)
{
    size_t len = strlen(name), base;

    if (!join(tree.src_path, sizeof(tree.src_path), tree.from, tree.rel)) return 1;
    if (mdfs_exists(tree.src_path) != MDFS_NONE) return 1;
    if (!has_ext(name, len, ".html")) return 0;

    base = strlen(tree.src_path) - 5;
    if (base + 10 > sizeof(tree.src_path)) return 1;
    strcpy(tree.src_path + base, ".md");
    if (mdfs_exists(tree.src_path) != MDFS_NONE) return 1;
    strcpy(tree.src_path + base, ".markdown");
    return mdfs_exists(tree.src_path) != MDFS_NONE;
}

/* Reports what lies in the target drawer tree.rel without a source. */
static int walk_target(int depth)
{
    char name[MDFS_NAMEMAX];
    size_t rel_len = strlen(tree.rel);
    void *dir;
    int is_dir, more, ok = 1;

    if (!join(tree.dst_path, sizeof(tree.dst_path), tree.to, tree.rel) ||
        !(dir = mdfs_open_dir(tree.dst_path))) {
        mdfs_fault(tree.dst_path);
        return 0;
    }
    while (ok && (more = mdfs_next(dir, name, &is_dir)) > 0) {
        if (mdtree_is_skipped(name, is_dir)) continue;
        if (!join(tree.rel, sizeof(tree.rel), tree.rel, name)) continue;

        if (!has_source(name)) {
            if (join(tree.dst_path, sizeof(tree.dst_path), tree.to, tree.rel))
                print3(tree.dst_path, ": no source (left in place)", "\n");
            tree.warnings++;
        } else if (is_dir && depth + 1 < MAXDEPTH) {
            ok = walk_target(depth + 1);
        }
        tree.rel[rel_len] = 0;
    }
    if (ok && more < 0) {
        join(tree.dst_path, sizeof(tree.dst_path), tree.to, tree.rel);
        mdfs_fault(tree.dst_path);
        ok = 0;
    }
    mdfs_close_dir(dir);
    return ok;
}

int mdtree_convert(const char *from, const char *to, const struct MDConvOptions *opt)
{
    char summary[80];
    struct link *l, *next;
    int ok;

    memset(&tree, 0, sizeof(tree));
    tree.from = from;
    tree.to = to;
    tree.opt = *opt;
    tree.opt.link_func = page_link;

    if (mdfs_exists(from) != MDFS_DIR) {
        print3("mdtohtml: not a drawer: ", from, "\n");
        return MDTREE_ERROR;
    }
    if (!mdfs_make_dir(to)) {
        mdfs_fault(to);
        return MDTREE_ERROR;
    }
    if (mdfs_is_inside(to, from)) {
        print3("mdtohtml: the target lies in the source: ", to, "\n");
        return MDTREE_ERROR;
    }

    ok = walk_source(0);
    if (ok) ok = check_links();
    if (ok) {
        tree.rel[0] = 0;
        ok = walk_target(0);
    }

    for (l = tree.links; l; l = next) {
        next = l->next;
        free(l);
    }
    tree.links = tree.last = NULL;

    if (!ok) return MDTREE_ERROR;
    sprintf(summary, "%lu pages, %lu files copied, %lu warnings\n",
            tree.pages, tree.copied, tree.warnings);
    mdfs_print(summary);
    return tree.warnings ? MDTREE_WARN : MDTREE_OK;
}
