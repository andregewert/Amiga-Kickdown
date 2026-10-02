/*
 * MDEdit - window, menus and speedbar
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
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
#include <gadgets/htmlttf.h>
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
#include "settings.h"

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
    { NM_ITEM,  (STRPTR)"Settings...",     (STRPTR)",", 0, 0, (APTR)CMD_SETTINGS },
    { NM_ITEM,  (STRPTR)"Iconify",         (STRPTR)"I", 0, 0, (APTR)CMD_ICONIFY },
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
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Find...",         (STRPTR)"F", 0, 0, (APTR)CMD_FIND },
    { NM_ITEM,  (STRPTR)"Find next",       (STRPTR)"G", 0, 0, (APTR)CMD_FINDNEXT },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Syntax highlighting", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)CMD_HIGHLIGHT },
    { NM_ITEM,  (STRPTR)"Line numbers",    0, CHECKIT | MENUTOGGLE, 0, (APTR)CMD_LINENUMBERS },
    { NM_TITLE, (STRPTR)"Preview",         0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Refresh",         (STRPTR)"R", 0, 0, (APTR)CMD_REFRESH },
    { NM_ITEM,  (STRPTR)"Auto refresh",    0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)CMD_AUTOREFRESH },
    { NM_ITEM,  (STRPTR)"Synchronize scrolling", 0, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)CMD_SYNCSCROLL },
    { NM_ITEM,  NM_BARLABEL,               0, 0, 0, 0 },
    { NM_ITEM,  (STRPTR)"Copy selection",  0, 0, 0, (APTR)CMD_COPYPREVIEW },
    { NM_END,   0, 0, 0, 0, 0 }
};

/* help bubbles of the speedbar buttons (window.class), see make_buttons() */
static struct HintInfo hints[MAXTOOLS + 1];

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
    { CMD_FIND,    8, "find",     "Find",    "Find and replace" },
    { CMD_REFRESH, 8, "refresh",  "Refresh", "Refresh the HTML preview" },
    { CMD_EXPORT,  0, "copyfile", "Export",  "Export the document as HTML file" },
    { CMD_SETTINGS, 8, "prefs",   "Settings", "Settings" },
    { CMD_ABOUT,   0, "info",     "About",   "About MDEdit" },
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
            SBNA_Enabled,   TRUE,
            SBNA_Spacing,   tools[i].spacing,
            SBNA_Highlight, SBH_RECESS,
            TAG_DONE);
        if (!node) return FALSE;
        AddTail(&gui.buttons, node);
        hints[i].hi_GadgetID = GID_TOOLBAR;
        hints[i].hi_Code = tools[i].cmd;
        hints[i].hi_Text = (STRPTR)tools[i].help;
        hints[i].hi_Flags = 0;
    }
    hints[i].hi_GadgetID = hints[i].hi_Code = -1;
    return TRUE;
}

/* window size from the settings: at most the screen, 0 = default */
static LONG start_size(LONG wanted, LONG screen, LONG def)
{
    if (wanted <= 0) return def;
    return wanted > screen ? screen : wanted;
}

BOOL gui_open(Class *htmlclass, struct MsgPort *appport, struct Hook *apphook,
              const struct Settings *set)
{
    BOOL highlight = set->highlight && highlight_hook();
    LONG width, height, left, top;
    struct NewMenu *nm;

    /* the tool images are remapped for this screen */
    if (!(gui.screen = LockPubScreen(NULL))) return FALSE;
    if (!make_buttons()) return FALSE;

    for (nm = menus; nm->nm_Type != NM_END; nm++) {
        if (nm->nm_UserData == (APTR)CMD_LINENUMBERS && set->linenumbers)
            nm->nm_Flags |= CHECKED;
        if ((nm->nm_UserData == (APTR)CMD_AUTOREFRESH && !set->autorefresh) ||
            (nm->nm_UserData == (APTR)CMD_SYNCSCROLL && !set->syncscroll) ||
            (nm->nm_UserData == (APTR)CMD_HIGHLIGHT && !highlight))
            nm->nm_Flags &= ~CHECKED;
    }

    gui.toolbar = NewObject(SPEEDBAR_GetClass(), NULL,
        GA_ID,                GID_TOOLBAR,
        GA_RelVerify,         TRUE,
        SPEEDBAR_Orientation, SBORIENT_HORIZ,
        SPEEDBAR_Buttons,     (ULONG)&gui.buttons,
        TAG_DONE);
    highlight_colours(gui.screen, set->colours);
    gui.editor = NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID,                    GID_EDITOR,
        GA_RelVerify,             TRUE,
        GA_TEXTEDITOR_FixedFont,  TRUE,
        GA_TEXTEDITOR_ShowLineNumbers, set->linenumbers,
        GA_TEXTEDITOR_Contents,   (ULONG)"",
        highlight ? GA_TEXTEDITOR_HighlighterHook : TAG_IGNORE,
                                  (ULONG)highlight_hook(),
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
        /* htmlttf.gadget only, html.gadget ignores them */
        set->ttf && set->fontset[0] ? HTMLTTF_FontSet : TAG_IGNORE, (ULONG)set->fontset,
        set->ttf && set->fontsize > 0 ? HTMLTTF_Size : TAG_IGNORE, set->fontsize,
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

    width = start_size(set->winwidth, gui.screen->Width, gui.screen->Width * 9 / 10);
    height = start_size(set->winheight, gui.screen->Height, gui.screen->Height * 4 / 5);
    /* a saved position, moved in if the window would stick out */
    left = set->winleft;
    top = set->wintop;
    if (left > gui.screen->Width - width) left = gui.screen->Width - width;
    if (top > gui.screen->Height - height) top = gui.screen->Height - height;
    gui.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)APPNAME,
        WA_PubScreen,       (ULONG)gui.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_SizeGadget,      TRUE,
        WA_Width,           width,
        WA_Height,          height,
        set->winleft >= 0 ? WA_Left : TAG_IGNORE, left,
        set->wintop >= 0 ? WA_Top : TAG_IGNORE,   top,
        WA_IDCMP,           IDCMP_INTUITICKS | IDCMP_RAWKEY,
        WA_NewLookMenus,    TRUE,
        WINDOW_NewMenu,     (ULONG)menus,
        set->winleft < 0 || set->wintop < 0 ? WINDOW_Position : TAG_IGNORE, WPOS_CENTERSCREEN,
        appport ? WINDOW_AppPort : TAG_IGNORE,   (ULONG)appport,
        appport ? WINDOW_AppWindow : TAG_IGNORE, TRUE,
        apphook ? WINDOW_AppMsgHook : TAG_IGNORE, (ULONG)apphook,
        /* iconifying needs the AppPort for the AppIcon */
        appport ? WINDOW_IconifyGadget : TAG_IGNORE, TRUE,
        WINDOW_IconTitle,   (ULONG)APPNAME,
        WINDOW_HintInfo,    (ULONG)hints,
        WINDOW_GadgetHelp,  TRUE,
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
    highlight_release();                            /* after the editor is gone */
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

/* icon on the Workbench while iconified; the window disposes it */
void gui_set_icon(struct DiskObject *icon)
{
    SetAttrs(gui.winobj, WINDOW_Icon, (ULONG)icon, TAG_DONE);
}

void gui_icon_title(CONST_STRPTR title)
{
    static char buf[108];
    /* the window keeps the pointer */
    strncpy(buf, (const char *)title, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    SetAttrs(gui.winobj, WINDOW_IconTitle, (ULONG)buf, TAG_DONE);
}

void gui_iconify(void)
{
    if (!gui.win) return;
    SetAttrs(gui.toolbar, SPEEDBAR_Window, 0, TAG_DONE);
    DoMethod(gui.winobj, WM_ICONIFY);
    gui.win = NULL;
}

/* FALSE if the window could not be opened again */
BOOL gui_uniconify(void)
{
    if (gui.win) return TRUE;
    if (!(gui.win = (struct Window *)DoMethod(gui.winobj, WM_OPEN))) return FALSE;
    SetGadgetAttrs((struct Gadget *)gui.toolbar, gui.win, NULL,
                   SPEEDBAR_Window, (ULONG)gui.win, TAG_DONE);
    gui_activate_editor();
    return TRUE;
}

void gui_activate_editor(void)
{
    if (!gui.win) return;
    ActivateLayoutGadget((struct Gadget *)gui.layout, gui.win, NULL, (ULONG)gui.editor);
}

static struct MenuItem *find_item(ULONG cmd)
{
    struct Menu *menu;
    struct MenuItem *item;

    for (menu = gui.win->MenuStrip; menu; menu = menu->NextMenu)
        for (item = menu->FirstItem; item; item = item->NextItem)
            if (GTMENUITEM_USERDATA(item) == (APTR)cmd) return item;
    return NULL;
}

/* sets a checkmark menu item, e.g. after the settings were changed */
void gui_set_checked(ULONG cmd, BOOL on)
{
    struct MenuItem *item;
    struct Menu *strip;

    if (!gui.win || !(item = find_item(cmd))) return;
    if (!(item->Flags & CHECKED) == !on) return;
    strip = gui.win->MenuStrip;
    ClearMenuStrip(gui.win);
    if (on) item->Flags |= CHECKED;
    else item->Flags &= ~CHECKED;
    ResetMenuStrip(gui.win, strip);
}

/* busy pointer; with a modal dialog the window ignores its input */
void gui_busy(BOOL on)
{
    SetAttrs(gui.winobj, WA_BusyPointer, on, TAG_DONE);
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
