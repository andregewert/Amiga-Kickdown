/*
 * MDEdit - find and replace
 *
 * A window that stays open next to the editor (like the one of TextEdit):
 * search text, replacement, case sensitive, whole words, backwards, wrap
 * around; Find, Replace, Replace all, Close. Edit/Find next repeats the
 * last search without the window.
 *
 * The editor does the work: GM_TEXTEDITOR_Search leaves the hit marked,
 * GM_TEXTEDITOR_Replace replaces the marked text. Replace only replaces
 * the last hit (its block is remembered), not any other selection.
 * Replace all is one undo step (BEGINMULTICHANGE/ENDMULTICHANGE).
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/string.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "mdedit.h"

enum {
    FG_FIND = 300, FG_REPLACE, FG_CASE, FG_WORDS, FG_BACKWARDS, FG_WRAP,
    FG_DOFIND, FG_DOREPLACE, FG_REPLACEALL, FG_CLOSE, FG_STATUS
};

#define MAXSEARCH 120                   /* limit of GM_TEXTEDITOR_Search */

static struct {
    Object *winobj, *root, *find, *replace, *casesens, *words, *backwards, *wrap, *status;
    struct Window *win;
    BOOL focus;                         /* activate the search field when active */
    char text[MAXSEARCH + 1];           /* last search, also for Find next */
    BOOL casesens_on, words_on, backwards_on, wrap_on;
    BOOL hit;                           /* the last search found something ... */
    ULONG hx0, hy0, hx1, hy1;           /* ... marked here */
} fw = { .wrap_on = TRUE };

static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_DONE);
}

static Object *checkbox(ULONG id, const char *text, BOOL on)
{
    return NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, CHECKBOX_Checked, on, TAG_DONE);
}

static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, TAG_DONE);
}

static Object *string(ULONG id)
{
    return NewObject(STRING_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_MaxChars, MAXSEARCH, STRINGA_TextVal, (ULONG)"", TAG_DONE);
}

static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

static void status(CONST_STRPTR text)
{
    static char buf[80];
    strncpy(buf, (const char *)text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    if (fw.win) SetGadgetAttrs((struct Gadget *)fw.status, fw.win, NULL, GA_Text, (ULONG)buf, TAG_DONE);
    else gui_status((CONST_STRPTR)buf);
}

static BOOL build(void)
{
    fw.find = string(FG_FIND);
    fw.replace = string(FG_REPLACE);
    fw.casesens = checkbox(FG_CASE, S(MSG_FIND_CASE), fw.casesens_on);
    fw.words = checkbox(FG_WORDS, S(MSG_FIND_WORDS), fw.words_on);
    fw.backwards = checkbox(FG_BACKWARDS, S(MSG_FIND_BACKWARDS), fw.backwards_on);
    fw.wrap = checkbox(FG_WRAP, S(MSG_FIND_WRAP), fw.wrap_on);
    fw.status = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, FG_STATUS, GA_ReadOnly, TRUE, GA_Text, (ULONG)"",
        BUTTON_Justification, BCJ_LEFT, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,           /* the window's backfill shows through */
        TAG_DONE);

    fw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,          (ULONG)S(MSG_FIND_TITLE),
        WA_PubScreen,      (ULONG)gui.screen,
        WA_Activate,       TRUE,
        WA_DepthGadget,    TRUE,
        WA_DragBar,        TRUE,
        WA_CloseGadget,    TRUE,
        WA_SizeGadget,     TRUE,
        WINDOW_RefWindow,  (ULONG)gui.win,
        WINDOW_Position,   WPOS_CENTERWINDOW,
        WA_IDCMP,          IDCMP_ACTIVEWINDOW,
        WINDOW_ParentGroup, (ULONG)(fw.root = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,    TRUE,
            LAYOUT_LeftSpacing,   6,
            LAYOUT_RightSpacing,  6,
            LAYOUT_TopSpacing,    6,
            LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing,  4,
            LAYOUT_DeferLayout,   TRUE,
            LAYOUT_AddChild, (ULONG)fw.find,
            CHILD_Label,     (ULONG)label(S(MSG_FIND_FIND)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)fw.replace,
            CHILD_Label,     (ULONG)label(S(MSG_FIND_WITH)),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,    (ULONG)fw.casesens,
                LAYOUT_AddChild,    (ULONG)fw.words,
                LAYOUT_AddChild,    (ULONG)fw.backwards,
                LAYOUT_AddChild,    (ULONG)fw.wrap,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)fw.status,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_EvenSize,    TRUE,
                LAYOUT_AddChild,    (ULONG)button(FG_DOFIND, S(MSG_FIND_DOFIND)),
                LAYOUT_AddChild,    (ULONG)button(FG_DOREPLACE, S(MSG_FIND_REPLACE)),
                LAYOUT_AddChild,    (ULONG)button(FG_REPLACEALL, S(MSG_FIND_REPLACEALL)),
                LAYOUT_AddChild,    (ULONG)button(FG_CLOSE, S(MSG_FIND_CLOSE)),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE)),
        TAG_DONE);
    return fw.winobj != NULL;
}

/* takes the texts and options from the gadgets */
static void read_gadgets(void)
{
    STRPTR s;
    if (!fw.winobj) return;
    fw.text[0] = 0;
    if ((s = (STRPTR)get(fw.find, STRINGA_TextVal))) strncat(fw.text, (const char *)s, MAXSEARCH);
    fw.casesens_on = get(fw.casesens, CHECKBOX_Checked) != 0;
    fw.words_on = get(fw.words, CHECKBOX_Checked) != 0;
    fw.backwards_on = get(fw.backwards, CHECKBOX_Checked) != 0;
    fw.wrap_on = get(fw.wrap, CHECKBOX_Checked) != 0;
}

static ULONG search_flags(BOOL fromtop)
{
    ULONG f = fromtop ? GF_TEXTEDITOR_Search_FromTop :
              fw.backwards_on ? GF_TEXTEDITOR_Search_Backwards : GF_TEXTEDITOR_Search_Next;
    if (fw.casesens_on) f |= GF_TEXTEDITOR_Search_CaseSensitive;
    if (fw.words_on) f |= GF_TEXTEDITOR_Search_WholeWord;
    if (fw.wrap_on && !fromtop) f |= GF_TEXTEDITOR_Search_Cyclic;
    return f;
}

static void block(ULONG *x0, ULONG *y0, ULONG *x1, ULONG *y1)
{
    *x0 = *y0 = *x1 = *y1 = ~0UL;
    DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_BlockInfo, 0,
                   (ULONG)x0, (ULONG)y0, (ULONG)x1, (ULONG)y1);
}

/* searches and remembers where the hit is */
static BOOL search(BOOL fromtop)
{
    ULONG found;

    fw.hit = FALSE;
    if (!fw.text[0] || !gui.win) return FALSE;
    found = DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_Search, 0,
                           (ULONG)fw.text, search_flags(fromtop));
    if (found) {
        fw.hit = TRUE;
        block(&fw.hx0, &fw.hy0, &fw.hx1, &fw.hy1);
    }
    return found != 0;
}

/* the last hit is still marked as it was found */
static BOOL hit_marked(void)
{
    ULONG x0, y0, x1, y1;
    if (!fw.hit || !get(gui.editor, GA_TEXTEDITOR_AreaMarked)) return FALSE;
    block(&x0, &y0, &x1, &y1);
    return x0 == fw.hx0 && y0 == fw.hy0 && x1 == fw.hx1 && y1 == fw.hy1;
}

static STRPTR replacement(void)
{
    STRPTR s = (STRPTR)get(fw.replace, STRINGA_TextVal);
    return s ? s : (STRPTR)"";
}

static void do_find(void)
{
    char msg[MAXSEARCH + 20];

    read_gadgets();
    if (!fw.text[0]) return;
    if (search(FALSE)) status((CONST_STRPTR)"");
    else {
        snprintf(msg, sizeof(msg), S(MSG_FIND_NOTFOUND), fw.text);
        status((CONST_STRPTR)msg);
    }
}

static void do_replace(void)
{
    read_gadgets();
    if (!fw.text[0]) return;
    if (hit_marked()) {
        DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_Replace, 0,
                       (ULONG)replacement(), 0);
        editor_changed();
    }
    do_find();                          /* on to the next one */
}

static void do_replace_all(void)
{
    char msg[40];
    ULONG n = 0;
    STRPTR with;

    read_gadgets();
    if (!fw.text[0]) return;
    with = replacement();
    DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                   (ULONG)"BEGINMULTICHANGE");
    /* from the top, never around: the replacement may contain the text */
    if (DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_Search, 0,
                       (ULONG)fw.text, search_flags(TRUE))) {
        ULONG flags = search_flags(FALSE) & ~(GF_TEXTEDITOR_Search_Cyclic | GF_TEXTEDITOR_SearchType_Mask);
        do {
            DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_Replace, 0,
                           (ULONG)with, 0);
            n++;
        } while (n < 100000 &&
                 DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_Search, 0,
                                (ULONG)fw.text, flags | GF_TEXTEDITOR_Search_Next));
    }
    DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_ARexxCmd, 0,
                   (ULONG)"ENDMULTICHANGE");
    fw.hit = FALSE;
    if (n) editor_changed();
    snprintf(msg, sizeof(msg), n == 1 ? S(MSG_FIND_ONE) : S(MSG_FIND_MANY), (unsigned long)n);
    status((CONST_STRPTR)msg);
}

/*****************************************************************************/

/* The cursor into the search field. Works only while the window is
 * active; ActivateWindow() is asynchronous, so it is tried again when
 * the window reports WMHI_ACTIVE.                                     */
static void focus_field(void)
{
    if (fw.win && ActivateLayoutGadget((struct Gadget *)fw.root, fw.win, NULL, (ULONG)fw.find))
        fw.focus = FALSE;
}

/* opens the window (or brings it to the front), the search field active */
void find_open(void)
{
    if (!gui.win) return;
    if (!prefs_open_classes()) return;
    if (!fw.winobj && !build()) {
        status((CONST_STRPTR)S(MSG_FIND_NOWINDOW));
        return;
    }
    if (!fw.win) {
        SetAttrs(fw.winobj, WINDOW_RefWindow, (ULONG)gui.win, TAG_DONE);
        if (!(fw.win = (struct Window *)DoMethod(fw.winobj, WM_OPEN))) return;
    } else {
        WindowToFront(fw.win);
        ActivateWindow(fw.win);
    }
    fw.focus = TRUE;
    focus_field();
}

/* Edit/Find next: the last search again, without the window */
void find_next(void)
{
    char msg[MAXSEARCH + 20];

    read_gadgets();
    if (!fw.text[0]) {
        find_open();
        return;
    }
    if (!search(FALSE)) {
        snprintf(msg, sizeof(msg), S(MSG_FIND_NOTFOUND), fw.text);
        status((CONST_STRPTR)msg);
    }
}

static void find_close(void)
{
    if (fw.win) {
        read_gadgets();                 /* for Find next */
        DoMethod(fw.winobj, WM_CLOSE);
        fw.win = NULL;
    }
}

ULONG find_sigmask(void)
{
    ULONG sig = 0;
    if (fw.win) GetAttr(WINDOW_SigMask, fw.winobj, &sig);
    return sig;
}

void find_handle(void)
{
    ULONG result;
    UWORD code;

    if (!fw.win) return;
    while ((result = DoMethod(fw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            find_close();
            return;
        case WMHI_ACTIVE:
            if (fw.focus) focus_field();
            break;
        case WMHI_GADGETUP:
            switch (result & WMHI_GADGETMASK) {
            case FG_FIND:                       /* Return in the search field */
            case FG_DOFIND:      do_find(); break;
            case FG_DOREPLACE:   do_replace(); break;
            case FG_REPLACEALL:  do_replace_all(); break;
            case FG_CLOSE:       find_close(); return;
            }
            break;
        }
    }
}

/* main window iconified or closed */
void find_cleanup(BOOL dispose)
{
    find_close();
    if (dispose && fw.winobj) {
        DisposeObject(fw.winobj);
        fw.winobj = NULL;
    }
}
