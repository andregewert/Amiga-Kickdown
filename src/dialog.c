/*
 * MDEdit - message and question requesters, centred over the main window
 *
 * EasyRequest() places its window where Intuition likes; this one is a
 * window.class window with WINDOW_RefWindow and WPOS_CENTERWINDOW. The
 * buttons are given like for EasyRequest() ("Save|Discard|Cancel") and
 * numbered the same way: 1, 2, ..., the last one 0. Return selects the
 * first button, Esc and the close gadget the last one.
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
#include <images/label.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <string.h>

#include "mdedit.h"

extern struct Library *LabelBase;   /* prefswin.c, closed by prefs_cleanup() */

#define MAXBUTTONS 6
#define GID_BUTTON 200

/* Returns the number of the button like EasyRequest(), -1 if the window
 * could not be opened (the caller can fall back to EasyRequest()).     */
LONG dialog(CONST_STRPTR title, CONST_STRPTR text, CONST_STRPTR buttons, BOOL centred)
{
    char labels[MAXBUTTONS][40];
    struct TagItem tags[3 + MAXBUTTONS];
    Object *winobj, *row, *b;
    struct Window *win;
    ULONG sig = 0, mainsig = 0, result, n = 0, i, t;
    const char *p;
    UWORD code;
    LONG rc = -1;

    if (!gui.win) return -1;
    if (!LabelBase && !(LabelBase = OpenLibrary((STRPTR)"images/label.image", 44))) return -1;

    /* "A|B|C" -> labels */
    for (p = (const char *)buttons; *p && n < MAXBUTTONS; n++) {
        const char *e = strchr(p, '|');
        ULONG len = e ? (ULONG)(e - p) : strlen(p);
        if (len >= sizeof(labels[0])) len = sizeof(labels[0]) - 1;
        memcpy(labels[n], p, len);
        labels[n][len] = 0;
        p += len;
        if (*p == '|') p++;
    }

    /* the button row, all buttons the same width; the tag list is built
     * here as LAYOUT_AddChild with OM_SET needs layout.gadget V47     */
    t = 0;
    tags[t].ti_Tag = LAYOUT_Orientation; tags[t++].ti_Data = LAYOUT_ORIENT_HORIZ;
    tags[t].ti_Tag = LAYOUT_EvenSize;    tags[t++].ti_Data = TRUE;
    for (i = 0; i < n; i++) {
        /* EasyRequest numbering: 1, 2, ..., last = 0 */
        ULONG id = GID_BUTTON + (i + 1 == n ? 0 : i + 1);
        if ((b = NewObject(BUTTON_GetClass(), NULL,
                GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)labels[i], TAG_DONE))) {
            tags[t].ti_Tag = LAYOUT_AddChild;
            tags[t++].ti_Data = (ULONG)b;
        }
    }
    tags[t].ti_Tag = TAG_DONE;
    if (!(row = NewObjectA(LAYOUT_GetClass(), NULL, tags))) return -1;

    winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)title,
        WA_PubScreen,       (ULONG)gui.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_IDCMP,           IDCMP_VANILLAKEY,
        WINDOW_RefWindow,   (ULONG)gui.win,
        WINDOW_Position,    WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,    TRUE,
            LAYOUT_LeftSpacing,   12,
            LAYOUT_RightSpacing,  12,
            LAYOUT_TopSpacing,    8,
            LAYOUT_BottomSpacing, 6,
            LAYOUT_InnerSpacing,  10,
            LAYOUT_AddImage, (ULONG)NewObject(LABEL_GetClass(), NULL,
                LABEL_Underscore,    0,     /* file names may contain '_' */
                LABEL_Justification, centred ? LJ_CENTRE : LJ_LEFT,
                LABEL_Text,          (ULONG)text,
                TAG_DONE),
            LAYOUT_AddChild, (ULONG)row,
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (!winobj) {
        DisposeObject(row);
        return -1;
    }
    if (!(win = (struct Window *)DoMethod(winobj, WM_OPEN))) {
        DisposeObject(winobj);
        return -1;
    }

    gui_busy(TRUE);
    GetAttr(WINDOW_SigMask, gui.winobj, &mainsig);
    GetAttr(WINDOW_SigMask, winobj, &sig);
    while (rc < 0) {
        ULONG got = Wait(sig | mainsig);

        /* the main window waits: its input is dropped */
        if (got & mainsig)
            while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;

        while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_GADGETUP:
                rc = (result & WMHI_GADGETMASK) - GID_BUTTON;
                break;
            case WMHI_CLOSEWINDOW:
                rc = 0;
                break;
            case WMHI_VANILLAKEY:
                if ((result & WMHI_KEYMASK) == 13) rc = n > 1 ? 1 : 0;     /* Return */
                else if ((result & WMHI_KEYMASK) == 27) rc = 0;           /* Esc */
                break;
            }
        }
    }
    gui_busy(FALSE);
    DisposeObject(winobj);
    return rc;
}
