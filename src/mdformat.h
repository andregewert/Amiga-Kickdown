/*
 * mdformat - Markdown formatting commands of MDEdit (toolbar, Format menu)
 *
 * Plain ANSI C without the editor, so it builds for the host (make check,
 * test/hostfmt.c). The glue to texteditor.gadget is in format.c.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef MDFORMAT_H
#define MDFORMAT_H

#include <stddef.h>

enum {
    /* inline: around the selection (or at the cursor) */
    FMT_BOLD, FMT_ITALIC, FMT_UNDERLINE, FMT_CODE, FMT_LINK, FMT_IMAGE,
    /* lines: on every line touched by the selection (or the cursor line) */
    FMT_HEADING, FMT_BULLET, FMT_NUMBERED, FMT_TASK, FMT_QUOTE,
    FMT_COUNT
};

#define FMT_IS_INLINE(k) ((k) < FMT_HEADING)

/* Inline format of the selected text 'sel' ('len' bytes, may be 0): the
 * markers are put around it, or removed if it already has them. Returns
 * the replacement (release with free()), NULL if out of memory.
 * *mark0 and *mark1 are the part of the replacement to be marked
 * afterwards; equal values mean the cursor goes there.                 */
char *mdfmt_inline(int kind, const char *sel, size_t len, size_t *mark0, size_t *mark1);

/* Line format of complete lines ('len' bytes, lines separated by '\n',
 * no '\n' at the end). Returns the new lines (release with free()), NULL
 * if out of memory. A list format is removed if all non-empty lines
 * already have it, otherwise it replaces any other list marker.       */
char *mdfmt_lines(int kind, const char *text, size_t len);

#endif /* MDFORMAT_H */
