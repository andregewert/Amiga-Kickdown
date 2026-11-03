/*
 * mdtree - converts a directory tree of Markdown files into a tree of
 * HTML pages (mdtohtml ALL)
 *
 * Plain ANSI C: the tree logic builds for the Amiga and for the host
 * (make check). The file system is the mdfs_* functions below, which
 * src/mdtree_dos.c implements with dos.library and test/hosttree.c with
 * POSIX calls.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef MDTREE_H
#define MDTREE_H

#include <stddef.h>

#include "mdconv.h"

/* results of mdtree_convert(), the DOS return codes without their headers */
#define MDTREE_OK    0
#define MDTREE_WARN  5      /* written, but with dead links or leftovers */
#define MDTREE_ERROR 10     /* stopped: nothing more was written */

/* What mdtree_link_target() found */
enum {
    MDTREE_LINK_LOCAL,      /* a file of the tree, path and anchor are set */
    MDTREE_LINK_EXTERNAL,   /* has a scheme (http:, mailto:, Work:...) */
    MDTREE_LINK_BAD         /* leaves the tree or does not fit the buffers */
};

/* Non-zero for a file name ending in .md or .markdown (ignoring case). */
int mdtree_is_markdown(const char *name);

/* Non-zero for what is not converted, copied or looked at: icons and,
 * if is_dir, drawers whose name starts with a dot (.tinydev, .git).     */
int mdtree_is_skipped(const char *name, int is_dir);

/* Writes name with .html in place of its Markdown extension. Returns
 * zero if it does not fit.                                               */
int mdtree_html_name(const char *name, char *out, size_t size);

/* Writes a local link to a Markdown file with its path changed to .html,
 * keeping #anchor (a.md#x -> a.html#x). Returns zero, leaving out alone,
 * if the link is another kind or does not fit.                           */
int mdtree_rewrite_link(const char *url, char *out, size_t size);

/* Finds what a link of a page points to. page is the page's path in the
 * tree (dir/page.html), url the link as written in the HTML: %XX and
 * &amp; are decoded, "." and ".." resolved. A link with only an anchor
 * gives the page itself. A path of the tree is relative to its root with
 * '/' between the parts.                                                 */
int mdtree_link_target(const char *page, const char *url, char *path, size_t path_size,
                       char *anchor, size_t anchor_size);

/* Non-zero if the HTML text has an id or name attribute with this value. */
int mdtree_has_anchor(const char *html, const char *anchor);

/* Mirrors the drawer 'from' in 'to' (made if missing, its parent must
 * exist): Markdown files become HTML pages, with links between them
 * changed to match, other files are copied. $root$ in the template
 * is the way from a page up to the root of the tree ("", "../", ...).
 * Afterwards it reports dead
 * links, dead anchors and files of 'to' without a source; nothing is ever
 * deleted. opt applies to all pages, opt->title must be NULL. Returns
 * MDTREE_OK, MDTREE_WARN or MDTREE_ERROR; messages go to mdfs_print().   */
int mdtree_convert(const char *from, const char *to, const struct MDConvOptions *opt);

/* The file system. Paths are as AmigaDOS (and POSIX) take them, with '/'
 * between the parts.                                                     */

#define MDFS_NAMEMAX 256    /* size of a file name's buffer, with its NUL */

/* mdfs_exists() */
#define MDFS_NONE 0
#define MDFS_FILE 1
#define MDFS_DIR  2

void *mdfs_open_dir(const char *path);      /* NULL on error, see mdfs_fault() */
/* Next entry without links; 1 if there is one, 0 at the end, -1 on error. */
int   mdfs_next(void *dir, char *name, int *is_dir);
void  mdfs_close_dir(void *dir);

int   mdfs_exists(const char *path);
int   mdfs_make_dir(const char *path);      /* non-zero if it now exists */
/* The file's contents with a NUL behind them, or NULL; mdfs_free() frees. */
char *mdfs_read(const char *path, size_t *len);
void  mdfs_free(char *data);
int   mdfs_write(const char *path, const char *data, size_t len);   /* non-zero if done */
int   mdfs_copy(const char *from, const char *to);                  /* non-zero if done */
/* Non-zero if the drawer inner is the drawer outer or lies below it. */
int   mdfs_is_inside(const char *inner, const char *outer);

void  mdfs_print(const char *text);         /* to standard output */
void  mdfs_fault(const char *path);         /* the last error, with this path */
int   mdfs_break(void);                     /* non-zero if Ctrl-C was pressed */

#endif /* MDTREE_H */
