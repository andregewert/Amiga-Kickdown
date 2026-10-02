/*
 * MDEdit - Markdown editor with HTML preview (ReAction, AmigaOS 3.2)
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#ifndef MDEDIT_H
#define MDEDIT_H

#include <exec/types.h>
#include <exec/lists.h>
#include <intuition/classes.h>
#include <utility/hooks.h>

#define APPNAME "MDEdit"

/* commands: menu user data and speedbar button ids */
enum {
    CMD_NEW = 1, CMD_OPEN, CMD_SAVE, CMD_SAVEAS, CMD_EXPORT, CMD_ABOUT, CMD_QUIT,
    CMD_CUT, CMD_COPY, CMD_PASTE, CMD_UNDO, CMD_REDO, CMD_SELECTALL, CMD_HIGHLIGHT,
    CMD_LINENUMBERS, CMD_SETTINGS,
    CMD_REFRESH, CMD_AUTOREFRESH, CMD_SYNCSCROLL, CMD_COPYPREVIEW
};

enum {
    GID_TOOLBAR = 1, GID_EDITOR, GID_ESCROLL, GID_HTML, GID_VSCROLL, GID_HSCROLL,
    GID_STATUS, GID_POS
};

#define MAXTOOLS 16

struct GUI {
    struct Screen *screen;
    Object *winobj, *layout, *toolbar, *editor, *escroll;
    Object *html, *vscroll, *hscroll, *status, *pos;
    struct Window *win;
    struct List buttons;            /* speedbar nodes */
    Object *images[MAXTOOLS];       /* bitmap.image objects of the buttons */
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

/* dialog.c */
LONG dialog(CONST_STRPTR title, CONST_STRPTR text, CONST_STRPTR buttons, BOOL centred);

/* prefswin.c */
enum { PREFS_CANCEL, PREFS_USE, PREFS_SAVE };
int prefs_dialog(struct Settings *s);
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

#endif /* MDEDIT_H */
