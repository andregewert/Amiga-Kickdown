/*
 * Kickdown - Markdown editor with HTML preview (ReAction, AmigaOS 3.2)
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef KICKDOWN_H
#define KICKDOWN_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classes.h>
#include <utility/hooks.h>

#define APPNAME "Kickdown"

#include "strings.h"

/* locale.c: S(MSG_...) is the string in the user's language */
void locale_open(void);
void locale_close(void);
const char *S(LONG id);

/* commands: menu user data and speedbar button ids */
enum {
    CMD_NEW = 1, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_EXPORT, CMD_ABOUT, CMD_QUIT,
    CMD_CUT, CMD_COPY, CMD_PASTE, CMD_UNDO, CMD_REDO, CMD_SELECTALL, CMD_HIGHLIGHT,
    CMD_LINENUMBERS, CMD_SETTINGS, CMD_ICONIFY, CMD_FIND, CMD_FINDNEXT,
    CMD_REFRESH, CMD_AUTOREFRESH, CMD_SYNCSCROLL, CMD_COPYPREVIEW,
    /* formatting, in the order of FMT_... (mdformat.h) */
    CMD_BOLD, CMD_ITALIC, CMD_UNDERLINE, CMD_CODE, CMD_LINK, CMD_IMAGE,
    CMD_HEADING, CMD_BULLET, CMD_NUMBERED, CMD_TASK, CMD_QUOTE,
    CMD_FORMATBAR
};

enum {
    GID_TOOLBAR = 1, GID_EDITOR, GID_ESCROLL, GID_HTML, GID_VSCROLL, GID_HSCROLL,
    GID_STATUS, GID_POS, GID_TOOLBAR2, GID_OVERFLOW
};

#define MAXTOOLS 32

struct GUI {
    struct Screen *screen;
    Object *winobj, *layout, *tbouter, *tbgroup, *toolbar, *toolbar2, *editor, *escroll;
    Object *overflow;               /* drop-down: the buttons that do not fit */
    struct List oflist;             /* its chooser nodes */
    ULONG ofkey[3];                 /* what the list was made for */
    Object *html, *vscroll, *hscroll, *status, *pos;
    struct Window *win;
    struct List buttons, buttons2;  /* speedbar nodes; buttons2: the formatting
                                       buttons (second row, or kept here while
                                       hidden with one row) */
    BOOL rows;                      /* formatting buttons in a second row */
    BOOL tbframes;
    struct Node *nodes[MAXTOOLS];   /* the node of each button */
    Object *images[MAXTOOLS];       /* bitmap.image or label.image of the buttons */
    Object *ghosts[MAXTOOLS];       /* their ghosted variants (AISS <name>_g) */
    Object *selimgs[MAXTOOLS];      /* pressed image and text (SBNA_SelImage) */
    char labels[MAXTOOLS][40];      /* texts of the label.images */
    struct DrawInfo *dri;
    LONG ghostpen;                  /* pen of ghosted texts, -1: not obtained */
    struct TextAttr *smallattr;     /* small font of image and text buttons ... */
    struct TextFont *smallfont;     /* ... kept open while they exist */
    ULONG disabled;                 /* ghosted buttons, bit 1 << cmd */
};

extern struct GUI gui;

/* gui.c */
struct Settings;
BOOL gui_open(Class *htmlclass, struct MsgPort *appport, struct Hook *apphook,
              const struct Settings *set);
void gui_close(void);
void gui_status(CONST_STRPTR text);
void gui_position(ULONG line, ULONG col);
void gui_title(CONST_STRPTR title);
void gui_sync_hscroll(void);
void gui_activate_editor(void);
BOOL gui_checked(ULONG cmd);
void gui_set_checked(ULONG cmd, BOOL on);
void gui_busy(BOOL on);
void gui_tools_disabled(ULONG mask);
BOOL gui_show_format(BOOL on);
void gui_update_overflow(void);
ULONG gui_overflow_cmd(ULONG index);
#define TOOLBIT(cmd) ((cmd) < 32 ? 1UL << (cmd) : 0)  /* only commands below 32 */
struct DiskObject;
void gui_set_icon(struct DiskObject *icon);
void gui_icon_title(CONST_STRPTR title);
void gui_iconify(void);
BOOL gui_uniconify(void);

/* kickdown.c: the text was changed from outside the editor (find.c) */
void editor_changed(void);

void splash_open(CONST_STRPTR iconname, CONST_STRPTR name, CONST_STRPTR version, ULONG steps);
void splash_status(CONST_STRPTR text);
void splash_step(void);
void splash_close(void);
BOOL about_window(CONST_STRPTR iconname, CONST_STRPTR name, CONST_STRPTR version, const char *details);
ULONG gui_tool_count(void);

void format_apply(int kind);

/* find.c */
void find_open(void);
void find_next(void);
ULONG find_sigmask(void);
void find_handle(void);
void find_cleanup(BOOL dispose);

/* dialog.c */
LONG dialog(CONST_STRPTR title, CONST_STRPTR text, CONST_STRPTR buttons, BOOL centred);

/* prefswin.c */
enum { PREFS_CANCEL, PREFS_USE, PREFS_SAVE };
int prefs_dialog(struct Settings *s);
BOOL prefs_open_classes(void);
void prefs_cleanup(void);

/* highlight.c */
void highlight_colours(struct Screen *scr, const ULONG *rgb);
void highlight_release(void);
struct Hook *highlight_hook(void);

/* sync.c */
void sync_rebuild(CONST_STRPTR mdtext, const char *html);
void sync_reset(void);
void sync_poll(void);
void sync_free(void);

#endif /* KICKDOWN_H */
