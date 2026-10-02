/*
 * MDEdit - window, menus and speedbar
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/scroller.h>
#include <gadgets/speedbar.h>
#include <gadgets/texteditor.h>
#include <gadgets/html.h>
#include <images/bitmap.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/scroller.h>
#include <proto/speedbar.h>
#include <proto/bitmap.h>
#include <proto/texteditor.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "mdedit.h"

struct GUI gui;

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project",         0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"New",             (STRPTR)"N", 0, 0, (APTR)CMD_NEW },
    { NM_ITEM,  (STRPTR)"Open...",         (STRPTR)"O", 0, 0, (APTR)CMD_OPEN },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Save",            (STRPTR)"S", 0, 0, (APTR)CMD_SAVE },
    { NM_ITEM,  (STRPTR)"Save as...",      (STRPTR)"A", 0, 0, (APTR)CMD_SAVEAS },
    { NM_ITEM,  (STRPTR)"Export HTML...",  (STRPTR)"E", 0, 0, (APTR)CMD_EXPORT },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"About...",        (STRPTR)"?", 0, 0, (APTR)CMD_ABOUT },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Quit",            (STRPTR)"Q", 0, 0, (APTR)CMD_QUIT },
    { NM_TITLE, (STRPTR)"Edit",            0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Cut",             (STRPTR)"X", 0, 0, (APTR)CMD_CUT },
    { NM_ITEM,  (STRPTR)"Copy",            (STRPTR)"C", 0, 0, (APTR)CMD_COPY },
    { NM_ITEM,  (STRPTR)"Paste",           (STRPTR)"V", 0, 0, (APTR)CMD_PASTE },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Undo",            (STRPTR)"Z", 0, 0, (APTR)CMD_UNDO },
    { NM_ITEM,  (STRPTR)"Redo",            (STRPTR)"Y", 0, 0, (APTR)CMD_REDO },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Select all",      0, 0, 0, (APTR)CMD_SELECTALL },
    { NM_TITLE, (STRPTR)"Preview",         0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Refresh",         (STRPTR)"R", 0, 0, (APTR)CMD_REFRESH },
    { NM_ITEM,  (STRPTR)"Auto refresh",    0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)CMD_AUTOREFRESH },
    { NM_ITEM,  (STRPTR)"Synchronize scrolling", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)CMD_SYNCSCROLL },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Copy selection",  0, 0, 0, (APTR)CMD_COPYPREVIEW },
    { NM_END,   0, 0, 0, 0, 0 }
};

/* Speedbar buttons. The images come from AISS (TBIMAGES:<name>, the
 * selected state from <name>_s); a missing image falls back to the label. */
static const struct {
    UWORD cmd;
    WORD spacing;
    const char *image, *label, *help;
} tools[] = {
    { CMD_NEW,     0, "new",      "New",     "Create a new document" },
    { CMD_OPEN,    0, "open",     "Open",    "Open a Markdown file" },
    { CMD_SAVE,    0, "save",     "Save",    "Save the Markdown file" },
    { CMD_SAVEAS,  0, "saveas",   "Save as", "Save the Markdown file under a new name" },
    { CMD_CUT,     8, "cut",      "Cut",     "Cut the selection" },
    { CMD_COPY,    0, "copy",     "Copy",    "Copy the selection" },
    { CMD_PASTE,   0, "paste",    "Paste",   "Paste from the clipboard" },
    { CMD_UNDO,    8, "undo",     "Undo",    "Undo the last change" },
    { CMD_REDO,    0, "redo",     "Redo",    "Redo the last undone change" },
    { CMD_REFRESH, 8, "refresh",  "Refresh", "Refresh the HTML preview" },
    { CMD_EXPORT,  0, "copyfile", "Export",  "Export the document as HTML file" },
    { CMD_ABOUT,   8, "info",     "About",   "About MDEdit" },
};
#define NUMTOOLS (sizeof(tools) / sizeof(tools[0]))

static struct TagItem escroll_map[] = { { SCROLLER_Top, GA_TEXTEDITOR_Prop_First }, { TAG_DONE, 0 } };
static struct TagItem editor_map[] = {
    { GA_TEXTEDITOR_Prop_First,   SCROLLER_Top },
    { GA_TEXTEDITOR_Prop_Entries, SCROLLER_Total },
    { GA_TEXTEDITOR_Prop_Visible, SCROLLER_Visible },
    { TAG_DONE, 0 }
};
static struct TagItem vscroll_map[] = { { SCROLLER_Top, HTML_Top }, { TAG_DONE, 0 } };
static struct TagItem hscroll_map[] = { { SCROLLER_Top, HTML_Left }, { TAG_DONE, 0 } };
static struct TagItem html_map[] = {
    { HTML_Top,     SCROLLER_Top },
    { HTML_Total,   SCROLLER_Total },
    { HTML_Visible, SCROLLER_Visible },
    { TAG_DONE, 0 }
};

static BOOL exists(CONST_STRPTR name)
{
    BPTR lock = Lock((STRPTR)name, ACCESS_READ);
    if (!lock) return FALSE;
    UnLock(lock);
    return TRUE;
}

static Object *load_image(const char *name)
{
    char file[64], sel[64];

    sprintf(file, "TBIMAGES:%s", name);
    sprintf(sel, "TBIMAGES:%s_s", name);
    if (!exists((CONST_STRPTR)file)) return NULL;
    return NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile,       (ULONG)file,
        exists((CONST_STRPTR)sel) ? BITMAP_SelectSourceFile : TAG_IGNORE, (ULONG)sel,
        BITMAP_Screen,           (ULONG)gui.screen,
        BITMAP_Masking,          TRUE,
        TAG_DONE);
}

static BOOL make_buttons(void)
{
    ULONG i;
    struct Node *node;

    NewList(&gui.buttons);
    for (i = 0; i < NUMTOOLS; i++) {
        Object *img = gui.images[i] = load_image(tools[i].image);
        node = AllocSpeedButtonNode(tools[i].cmd,
            img ? SBNA_Image : SBNA_Text, img ? (ULONG)img : (ULONG)tools[i].label,
            SBNA_Help,      (ULONG)tools[i].help,
            SBNA_Enabled,   TRUE,
            SBNA_Spacing,   tools[i].spacing,
            SBNA_Highlight, SBH_RECESS,
            TAG_DONE);
        if (!node) return FALSE;
        AddTail(&gui.buttons, node);
    }
    return TRUE;
}

BOOL gui_open(Class *htmlclass, struct MsgPort *appport, struct Hook *apphook,
              BOOL autorefresh, BOOL syncscroll)
{
    struct NewMenu *nm;

    /* the tool images are remapped for this screen */
    if (!(gui.screen = LockPubScreen(NULL))) return FALSE;
    if (!make_buttons()) return FALSE;

    for (nm = menus; nm->nm_Type != NM_END; nm++)
        if ((nm->nm_UserData == (APTR)CMD_AUTOREFRESH && !autorefresh) ||
            (nm->nm_UserData == (APTR)CMD_SYNCSCROLL && !syncscroll))
            nm->nm_Flags &= ~CHECKED;

    gui.toolbar = NewObject(SPEEDBAR_GetClass(), NULL,
        GA_ID,                GID_TOOLBAR,
        GA_RelVerify,         TRUE,
        SPEEDBAR_Orientation, SBORIENT_HORIZ,
        SPEEDBAR_Buttons,     (ULONG)&gui.buttons,
        TAG_DONE);
    gui.editor = NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID,                    GID_EDITOR,
        GA_RelVerify,             TRUE,
        GA_TEXTEDITOR_FixedFont,  TRUE,
        GA_TEXTEDITOR_Contents,   (ULONG)"",
        TAG_DONE);
    gui.escroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID,                GID_ESCROLL,
        GA_RelVerify,         TRUE,
        SCROLLER_Orientation, SORIENT_VERT,
        SCROLLER_Arrows,      TRUE,
        SCROLLER_ArrowDelta,  1,
        ICA_TARGET,           (ULONG)gui.editor,
        ICA_MAP,              (ULONG)escroll_map,
        TAG_DONE);
    gui.html = NewObject(htmlclass, NULL,
        GA_ID,                GID_HTML,
        GA_RelVerify,         TRUE,
        HTML_Text,            (ULONG)"",
        TAG_DONE);
    gui.vscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID,                GID_VSCROLL,
        GA_RelVerify,         TRUE,
        SCROLLER_Orientation, SORIENT_VERT,
        SCROLLER_Arrows,      TRUE,
        SCROLLER_ArrowDelta,  16,
        ICA_TARGET,           (ULONG)gui.html,
        ICA_MAP,              (ULONG)vscroll_map,
        TAG_DONE);
    gui.hscroll = NewObject(SCROLLER_GetClass(), NULL,
        GA_ID,                GID_HSCROLL,
        GA_RelVerify,         TRUE,
        SCROLLER_Orientation, SORIENT_HORIZ,
        SCROLLER_Arrows,      TRUE,
        SCROLLER_ArrowDelta,  16,
        ICA_TARGET,           (ULONG)gui.html,
        ICA_MAP,              (ULONG)hscroll_map,
        TAG_DONE);
    gui.status = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,                GID_STATUS,
        GA_ReadOnly,          TRUE,
        GA_Text,              (ULONG)"",
        BUTTON_Justification, BCJ_LEFT,
        TAG_DONE);
    gui.pos = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,                GID_POS,
        GA_ReadOnly,          TRUE,
        GA_Text,              (ULONG)"",
        BUTTON_Justification, BCJ_RIGHT,
        TAG_DONE);

    if (!gui.toolbar || !gui.editor || !gui.escroll || !gui.html || !gui.vscroll ||
        !gui.hscroll || !gui.status || !gui.pos)
        return FALSE;
    SetAttrs(gui.editor, ICA_TARGET, (ULONG)gui.escroll, ICA_MAP, (ULONG)editor_map, TAG_DONE);
    SetAttrs(gui.html, ICA_TARGET, (ULONG)gui.vscroll, ICA_MAP, (ULONG)html_map, TAG_DONE);

    gui.layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_DeferLayout, TRUE,

        LAYOUT_AddChild,    (ULONG)gui.toolbar,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,

            /* editor with its scroller */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
                LAYOUT_InnerSpacing, 0,
                LAYOUT_AddChild,     (ULONG)gui.editor,
                LAYOUT_AddChild,     (ULONG)gui.escroll,
                CHILD_WeightedWidth, 0,
                TAG_DONE),
            LAYOUT_WeightBar, TRUE,

            /* preview with both scrollers */
            LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
                LAYOUT_InnerSpacing, 0,
                LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                    LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
                    LAYOUT_InnerSpacing, 0,
                    LAYOUT_AddChild,     (ULONG)gui.html,
                    LAYOUT_AddChild,     (ULONG)gui.vscroll,
                    CHILD_WeightedWidth, 0,
                    TAG_DONE),
                LAYOUT_AddChild,      (ULONG)gui.hscroll,
                CHILD_WeightedHeight, 0,
                TAG_DONE),
            TAG_DONE),

        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild,     (ULONG)gui.status,
            LAYOUT_AddChild,     (ULONG)gui.pos,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      gui.screen->RastPort.TxWidth * 18,
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);
    if (!gui.layout) return FALSE;

    gui.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)APPNAME,
        WA_PubScreen,       (ULONG)gui.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_SizeGadget,      TRUE,
        WA_Width,           gui.screen->Width * 9 / 10,
        WA_Height,          gui.screen->Height * 4 / 5,
        WA_IDCMP,           IDCMP_INTUITICKS | IDCMP_RAWKEY,
        WA_NewLookMenus,    TRUE,
        WINDOW_NewMenu,     (ULONG)menus,
        WINDOW_Position,    WPOS_CENTERSCREEN,
        appport ? WINDOW_AppPort : TAG_IGNORE,   (ULONG)appport,
        appport ? WINDOW_AppWindow : TAG_IGNORE, TRUE,
        apphook ? WINDOW_AppMsgHook : TAG_IGNORE, (ULONG)apphook,
        WINDOW_ParentGroup, (ULONG)gui.layout,
        TAG_DONE);
    if (!gui.winobj) return FALSE;              /* gui_close() disposes the layout */
    if (!(gui.win = (struct Window *)DoMethod(gui.winobj, WM_OPEN))) return FALSE;

    SetGadgetAttrs((struct Gadget *)gui.toolbar, gui.win, NULL,
                   SPEEDBAR_Window, (ULONG)gui.win, TAG_DONE);
    gui_activate_editor();
    return TRUE;
}

void gui_close(void)
{
    struct Node *node, *next;
    ULONG i;

    if (gui.winobj) DisposeObject(gui.winobj);      /* disposes all gadgets */
    else if (gui.layout) DisposeObject(gui.layout);
    else {
        Object *objs[] = { gui.toolbar, gui.editor, gui.escroll, gui.html,
                           gui.vscroll, gui.hscroll, gui.status, gui.pos };
        for (i = 0; i < sizeof(objs) / sizeof(objs[0]); i++)
            if (objs[i]) DisposeObject(objs[i]);
    }
    /* the speedbar neither frees its nodes nor their images */
    if (gui.buttons.lh_Head) {
        for (node = gui.buttons.lh_Head; (next = node->ln_Succ); node = next)
            FreeSpeedButtonNode(node);
    }
    for (i = 0; i < MAXTOOLS; i++)
        if (gui.images[i]) DisposeObject(gui.images[i]);
    if (gui.screen) UnlockPubScreen(NULL, gui.screen);
    memset(&gui, 0, sizeof(gui));
}

void gui_status(CONST_STRPTR text)
{
    static char buf[256];
    /* the button keeps the pointer, so the text is copied */
    strncpy(buf, (const char *)text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    SetGadgetAttrs((struct Gadget *)gui.status, gui.win, NULL, GA_Text, (ULONG)buf, TAG_DONE);
}

void gui_position(ULONG line, ULONG col)
{
    static char buf[32];
    sprintf(buf, "Line %lu, Col %lu", (unsigned long)line, (unsigned long)col);
    SetGadgetAttrs((struct Gadget *)gui.pos, gui.win, NULL, GA_Text, (ULONG)buf, TAG_DONE);
}

void gui_title(CONST_STRPTR title)
{
    static char buf[160];
    strncpy(buf, (const char *)title, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    SetAttrs(gui.winobj, WA_Title, (ULONG)buf, TAG_DONE);
}

void gui_sync_hscroll(void)
{
    ULONG left = 0, total = 0, vis = 0;
    GetAttr(HTML_Left, gui.html, &left);
    GetAttr(HTML_TotalWidth, gui.html, &total);
    GetAttr(HTML_VisibleWidth, gui.html, &vis);
    SetGadgetAttrs((struct Gadget *)gui.hscroll, gui.win, NULL,
                   SCROLLER_Total, total, SCROLLER_Visible, vis, SCROLLER_Top, left, TAG_DONE);
}

void gui_activate_editor(void)
{
    ActivateLayoutGadget((struct Gadget *)gui.layout, gui.win, NULL, (ULONG)gui.editor);
}

/* state of a checkmark menu item */
BOOL gui_checked(ULONG cmd)
{
    struct Menu *menu;
    struct MenuItem *item;

    if (!gui.win) return TRUE;
    for (menu = gui.win->MenuStrip; menu; menu = menu->NextMenu)
        for (item = menu->FirstItem; item; item = item->NextItem)
            if (GTMENUITEM_USERDATA(item) == (APTR)cmd)
                return (item->Flags & CHECKED) != 0;
    return TRUE;
}
