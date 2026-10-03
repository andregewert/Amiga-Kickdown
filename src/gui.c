/*
 * Kickdown - window, menus and speedbar
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
#include <images/bevel.h>
#include <images/label.h>
#include <gadgets/chooser.h>
#include <intuition/screens.h>
#include <graphics/view.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/scroller.h>
#include <proto/speedbar.h>
#include <proto/bitmap.h>
#include <proto/label.h>
#include <proto/chooser.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <diskfont/diskfont.h>
#include <proto/texteditor.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "kickdown.h"
#include "settings.h"

extern struct Library *LabelBase;   /* prefswin.c, closed by prefs_cleanup() */
extern struct Library *LayoutBase;  /* kickdown.c */
extern struct Library *ChooserBase; /* prefswin.c, closed by prefs_cleanup() */

/* initialised explicitly, see kickdown.c */
struct Library *DiskfontBase = NULL;

struct GUI gui;

/* The menus; NewMenu is filled in at run time with the translated
 * strings (gui_open()). msg -1: separator bar. Shortcuts stay the same
 * in every language.                                                   */
static const struct {
    UBYTE type;
    LONG msg;
    const char *key;
    UWORD flags;
    ULONG cmd;
} menudef[] = {
    { NM_TITLE, MSG_MENU_PROJECT,      0,   0, 0 },
    { NM_ITEM,  MSG_MENU_NEW,          "N", 0, CMD_NEW },
    { NM_ITEM,  MSG_MENU_OPEN,         "O", 0, CMD_OPEN },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_SAVE,         "S", 0, CMD_SAVE },
    { NM_ITEM,  MSG_MENU_SAVEAS,       "A", 0, CMD_SAVEAS },
    { NM_ITEM,  MSG_MENU_EXPORT,       "E", 0, CMD_EXPORT },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_SETTINGS,     ",", 0, CMD_SETTINGS },
    { NM_ITEM,  MSG_MENU_ICONIFY,      "I", 0, CMD_ICONIFY },
    { NM_ITEM,  MSG_MENU_ABOUT,        "?", 0, CMD_ABOUT },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_QUIT,         "Q", 0, CMD_QUIT },
    { NM_TITLE, MSG_MENU_EDIT,         0,   0, 0 },
    { NM_ITEM,  MSG_MENU_CUT,          "X", 0, CMD_CUT },
    { NM_ITEM,  MSG_MENU_COPY,         "C", 0, CMD_COPY },
    { NM_ITEM,  MSG_MENU_PASTE,        "V", 0, CMD_PASTE },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_UNDO,         "Z", 0, CMD_UNDO },
    { NM_ITEM,  MSG_MENU_REDO,         "Y", 0, CMD_REDO },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_SELECTALL,    0,   0, CMD_SELECTALL },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_FIND,         "F", 0, CMD_FIND },
    { NM_ITEM,  MSG_MENU_FINDNEXT,     "G", 0, CMD_FINDNEXT },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_HIGHLIGHT,    0,   CHECKIT | MENUTOGGLE, CMD_HIGHLIGHT },
    { NM_ITEM,  MSG_MENU_LINENUMBERS,  0,   CHECKIT | MENUTOGGLE, CMD_LINENUMBERS },
    { NM_TITLE, MSG_MENU_FORMAT,       0,   0, 0 },
    { NM_ITEM,  MSG_MENU_HEADING,      "H", 0, CMD_HEADING },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_BOLD,         "B", 0, CMD_BOLD },
    { NM_ITEM,  MSG_MENU_ITALIC,       "T", 0, CMD_ITALIC },
    { NM_ITEM,  MSG_MENU_UNDERLINE,    "U", 0, CMD_UNDERLINE },
    { NM_ITEM,  MSG_MENU_CODE,         "D", 0, CMD_CODE },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_LINK,         "K", 0, CMD_LINK },
    { NM_ITEM,  MSG_MENU_IMAGE,        0,   0, CMD_IMAGE },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_BULLET,       "L", 0, CMD_BULLET },
    { NM_ITEM,  MSG_MENU_NUMBERED,     0,   0, CMD_NUMBERED },
    { NM_ITEM,  MSG_MENU_TASK,         0,   0, CMD_TASK },
    { NM_ITEM,  MSG_MENU_QUOTE,        0,   0, CMD_QUOTE },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_FORMATBAR,    0,   CHECKIT | MENUTOGGLE, CMD_FORMATBAR },
    { NM_TITLE, MSG_MENU_PREVIEW,      0,   0, 0 },
    { NM_ITEM,  MSG_MENU_REFRESH,      "R", 0, CMD_REFRESH },
    { NM_ITEM,  MSG_MENU_AUTOREFRESH,  0,   CHECKIT | MENUTOGGLE, CMD_AUTOREFRESH },
    { NM_ITEM,  MSG_MENU_SYNCSCROLL,   0,   CHECKIT | MENUTOGGLE, CMD_SYNCSCROLL },
    { NM_ITEM,  -1,                    0,   0, 0 },
    { NM_ITEM,  MSG_MENU_COPYPREVIEW,  0,   0, CMD_COPYPREVIEW },
};
#define NUMMENUS (sizeof(menudef) / sizeof(menudef[0]))

static struct NewMenu menus[NUMMENUS + 1];
static void remember_checks(void);
static void apply_checks(void);

/* NewMenu from menudef with the translated strings and the check marks */
static void build_menus(const struct Settings *set, BOOL highlight)
{
    ULONG i;

    memset(menus, 0, sizeof(menus));
    for (i = 0; i < NUMMENUS; i++) {
        struct NewMenu *nm = &menus[i];
        ULONG cmd = menudef[i].cmd;
        BOOL on = (cmd == CMD_HIGHLIGHT && highlight) || (cmd == CMD_LINENUMBERS && set->linenumbers) ||
                  (cmd == CMD_AUTOREFRESH && set->autorefresh) || (cmd == CMD_SYNCSCROLL && set->syncscroll) ||
                  (cmd == CMD_FORMATBAR && set->fmtbuttons);
        nm->nm_Type = menudef[i].type;
        nm->nm_Label = menudef[i].msg < 0 ? NM_BARLABEL : (STRPTR)S(menudef[i].msg);
        nm->nm_CommKey = (STRPTR)menudef[i].key;
        nm->nm_Flags = menudef[i].flags | (on ? CHECKED : 0);
        nm->nm_UserData = (APTR)cmd;
    }
    menus[NUMMENUS].nm_Type = NM_END;
}

/* help bubbles of the speedbar buttons (window.class), see make_buttons() */
static struct HintInfo hints[MAXTOOLS + 2];   /* + the overflow chooser */

/* Speedbar buttons. The images come from AISS (TBIMAGES:<name>, the
 * selected state from <name>_s, the ghosted one from <name>_g); "a|b"
 * takes the first one that exists (AISS 4 has more images than AISS
 * Classic).
 *
 * speedbar.gadget (47.10) stores SBNA_Text but never draws it, so
 * texts are label.images given as SBNA_Image: text alone, or the AISS
 * image with the text below. With texts the bar would be too wide, the
 * formatting buttons (row 1) then get a second bar below.          */
static const struct {
    UWORD cmd;
    WORD spacing;
    UBYTE row;
    const char *image;
    LONG label, help;
} tools[] = {
    { CMD_NEW,      0, 0, "new",      MSG_TB_NEW,      MSG_TBH_NEW },
    { CMD_OPEN,     0, 0, "open",     MSG_TB_OPEN,     MSG_TBH_OPEN },
    { CMD_SAVE,     0, 0, "save",     MSG_TB_SAVE,     MSG_TBH_SAVE },
    { CMD_SAVEAS,   0, 0, "saveas",   MSG_TB_SAVEAS,   MSG_TBH_SAVEAS },
    { CMD_CUT,      8, 0, "cut",      MSG_TB_CUT,      MSG_TBH_CUT },
    { CMD_COPY,     0, 0, "copy",     MSG_TB_COPY,     MSG_TBH_COPY },
    { CMD_PASTE,    0, 0, "paste",    MSG_TB_PASTE,    MSG_TBH_PASTE },
    { CMD_UNDO,     8, 0, "undo",     MSG_TB_UNDO,     MSG_TBH_UNDO },
    { CMD_REDO,     0, 0, "redo",     MSG_TB_REDO,     MSG_TBH_REDO },
    { CMD_FIND,     8, 0, "find",     MSG_TB_FIND,     MSG_TBH_FIND },
    { CMD_HEADING,  8, 1, "font_larger|font", MSG_TB_HEADING, MSG_TBH_HEADING },
    { CMD_BOLD,     0, 1, "font_bold",     MSG_TB_BOLD,       MSG_TBH_BOLD },
    { CMD_ITALIC,   0, 1, "font_italic",   MSG_TB_ITALIC,     MSG_TBH_ITALIC },
    { CMD_UNDERLINE, 0, 1, "font_uline",   MSG_TB_UNDERLINE, MSG_TBH_UNDERLINE },
    { CMD_CODE,     0, 1, "brackets|hexview", MSG_TB_CODE,    MSG_TBH_CODE },
    { CMD_LINK,     8, 1, "hyperlink|internet", MSG_TB_LINK, MSG_TBH_LINK },
    { CMD_IMAGE,    0, 1, "image",         MSG_TB_IMAGE,      MSG_TBH_IMAGE },
    { CMD_BULLET,   8, 1, "capitalpoints", MSG_TB_BULLET,     MSG_TBH_BULLET },
    { CMD_NUMBERED, 0, 1, "capitalnumber", MSG_TB_NUMBERED,   MSG_TBH_NUMBERED },
    { CMD_QUOTE,    0, 1, "quote|definitions", MSG_TB_QUOTE,   MSG_TBH_QUOTE },
    { CMD_TASK,     0, 1, "task",          MSG_TB_TASK,       MSG_TBH_TASK },
    { CMD_REFRESH,  8, 0, "refresh",  MSG_TB_REFRESH,  MSG_TBH_REFRESH },
    { CMD_EXPORT,   0, 0, "copyfile", MSG_TB_EXPORT,   MSG_TBH_EXPORT },
    { CMD_SETTINGS, 8, 0, "prefs",    MSG_TB_SETTINGS, MSG_TBH_SETTINGS },
    { CMD_ABOUT,    0, 0, "info",     MSG_TB_ABOUT,    MSG_TBH_ABOUT },
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

/* *hassel: there is a selected image as well; the ghosted image
 * (<name>_g) goes to *ghost, NULL if there is none; with 'selected' the
 * selected image also as an object of its own (for a label). 'names'
 * is a list of alternatives separated by '|'.                     */
static Object *load_image(const char *names, BOOL *hassel, Object **ghost, Object **selected)
{
    char file[64], sel[64], name[40];
    const char *p = names;

    /* the first alternative that exists, else the last one */
    for (;;) {
        const char *e = strchr(p, '|');
        ULONG len = e ? (ULONG)(e - p) : strlen(p);
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, p, len);
        name[len] = 0;
        sprintf(file, "TBIMAGES:%s", name);
        if (!e || exists((CONST_STRPTR)file)) break;
        p = e + 1;
    }

    sprintf(file, "TBIMAGES:%s_g", name);
    *ghost = exists((CONST_STRPTR)file) ? NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile, (ULONG)file,
        BITMAP_Screen,     (ULONG)gui.screen,
        BITMAP_Masking,    TRUE,
        TAG_DONE) : NULL;
    sprintf(file, "TBIMAGES:%s", name);
    sprintf(sel, "TBIMAGES:%s_s", name);
    *hassel = FALSE;
    if (selected) *selected = NULL;
    if (!exists((CONST_STRPTR)file)) return NULL;
    *hassel = exists((CONST_STRPTR)sel);
    if (selected && *hassel)
        *selected = NewObject(BITMAP_GetClass(), NULL,
            BITMAP_SourceFile, (ULONG)sel,
            BITMAP_Screen,     (ULONG)gui.screen,
            BITMAP_Masking,    TRUE,
            TAG_DONE);
    return NewObject(BITMAP_GetClass(), NULL,
        BITMAP_SourceFile,       (ULONG)file,
        *hassel ? BITMAP_SelectSourceFile : TAG_IGNORE, (ULONG)sel,
        BITMAP_Screen,           (ULONG)gui.screen,
        BITMAP_Masking,          TRUE,
        TAG_DONE);
}

static BOOL same_name(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20)) a++, b++;
    return *a == *b;
}

/* The screen font in its smallest size (at least 6 pixels), opened for
 * the texts below the images. NULL: the screen font as it is.         */
static struct TextAttr *small_font(void)
{
    static struct TextAttr attr;
    static char name[64];
    struct TextAttr *scr = gui.screen->Font;
    struct AvailFontsHeader *afh = NULL;
    struct AvailFonts *af;
    LONG size = 2048, more;
    UWORD best = 0, n;

    if (!scr || !scr->ta_Name) return NULL;
    if (!DiskfontBase && !(DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39))) return NULL;
    for (;;) {
        if (!(afh = AllocVec(size, MEMF_ANY))) return NULL;
        if (!(more = AvailFonts(afh, size, AFF_MEMORY | AFF_DISK))) break;
        FreeVec(afh);
        size += more;
    }
    af = (struct AvailFonts *)(afh + 1);
    for (n = 0; n < afh->afh_NumEntries; n++, af++)
        if (af->af_Attr.ta_YSize >= 6 && (!best || af->af_Attr.ta_YSize < best) &&
            same_name((const char *)af->af_Attr.ta_Name, (const char *)scr->ta_Name))
            best = af->af_Attr.ta_YSize;
    FreeVec(afh);
    if (!best) return NULL;

    strncpy(name, (const char *)scr->ta_Name, sizeof(name) - 1);
    attr.ta_Name = (STRPTR)name;
    attr.ta_YSize = best;
    attr.ta_Style = FS_NORMAL;
    attr.ta_Flags = 0;
    if (!(gui.smallfont = OpenDiskFont(&attr))) return NULL;
    return &attr;
}

/* A label.image with the text of button i, below 'image' if there is
 * one (the label disposes it), in pen 'pen'. NULL on failure.         */
static Object *make_label(ULONG i, Object *image, LONG pen)
{
    Object *o;

    snprintf(gui.labels[i], sizeof(gui.labels[i]), "%s%s", image ? "\n" : "", S(tools[i].label));
    o = LabelBase ? NewObject(LABEL_GetClass(), NULL,
        LABEL_DrawInfo,      (ULONG)gui.dri,
        LABEL_Justification, LJ_CENTRE,
        LABEL_Underscore,    0,
        IA_FGPen,            pen,
        /* below an image the smallest size of the screen font */
        image && gui.smallattr ? IA_Font : TAG_IGNORE, (ULONG)gui.smallattr,
        image ? LABEL_DisposeImage : TAG_IGNORE, TRUE,
        image ? LABEL_Image : TAG_IGNORE,        (ULONG)image,
        LABEL_Text,          (ULONG)gui.labels[i],
        TAG_DONE) : NULL;
    if (!o && image) DisposeObject(image);
    return o;
}

/* pen for ghosted texts: halfway between text and background */
static void obtain_ghostpen(void)
{
    struct ColorMap *cm = gui.screen->ViewPort.ColorMap;
    ULONG t[3], b[3];

    GetRGB32(cm, gui.dri->dri_Pens[TEXTPEN], 1, t);
    GetRGB32(cm, gui.dri->dri_Pens[BACKGROUNDPEN], 1, b);
    gui.ghostpen = ObtainBestPen(cm, (t[0] >> 1) + (b[0] >> 1), (t[1] >> 1) + (b[1] >> 1),
                                 (t[2] >> 1) + (b[2] >> 1), OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}

static LONG ghost_pen(void)
{
    return gui.ghostpen >= 0 ? gui.ghostpen : gui.dri->dri_Pens[TEXTPEN];
}

static BOOL make_buttons(const struct Settings *set)
{
    BOOL rows = gui.rows = set->tbmode != TBMODE_IMAGES;     /* texts: two bars */
    LONG text = gui.dri->dri_Pens[TEXTPEN];
    ULONG i;

    NewList(&gui.buttons);
    NewList(&gui.buttons2);
    if (set->tbmode == TBMODE_BOTH) gui.smallattr = small_font();
    for (i = 0; i < NUMTOOLS; i++) {
        Object *bm = NULL, *ghost = NULL, *selbm = NULL, *selimg = NULL, *img;
        BOOL sel = FALSE, second = tools[i].row && (rows || !set->fmtbuttons);
        struct Node *node;

        /* text only: no image is loaded at all */
        if (set->tbmode != TBMODE_TEXT)
            bm = load_image(tools[i].image, &sel, &ghost, set->tbmode == TBMODE_BOTH ? &selbm : NULL);
        if (set->tbmode == TBMODE_IMAGES && bm) {
            img = bm;
        } else {
            /* the text, with the image above it if there is one; a
             * missing image in image mode gives the text as well    */
            img = make_label(i, set->tbmode == TBMODE_BOTH ? bm : NULL, text);
            if (set->tbmode != TBMODE_BOTH && bm) DisposeObject(bm);
            if (ghost && bm && set->tbmode == TBMODE_BOTH) ghost = make_label(i, ghost, ghost_pen());
            else {
                if (ghost) DisposeObject(ghost);
                ghost = !bm || set->tbmode == TBMODE_TEXT ? make_label(i, NULL, ghost_pen()) : NULL;
            }
            /* pressed: the label with the AISS selected image */
            if (selbm && bm && img) selimg = make_label(i, selbm, text);
            else if (selbm) DisposeObject(selbm);
            sel = selimg != NULL;
        }
        gui.images[i] = img;
        gui.ghosts[i] = ghost;
        gui.selimgs[i] = selimg;
        splash_step();
        /* pressed: the AISS selected image if there is one, else recessed */
        node = AllocSpeedButtonNode(tools[i].cmd,
            img ? SBNA_Image : SBNA_Text, img ? (ULONG)img : (ULONG)S(tools[i].label),
            SBNA_Enabled,   TRUE,
            SBNA_Spacing,   rows && tools[i].row && !tools[i - 1].row ? 0 : tools[i].spacing,
            SBNA_Highlight, sel ? SBH_IMAGE : SBH_RECESS,
            selimg ? SBNA_SelImage : TAG_IGNORE, (ULONG)selimg,
            TAG_DONE);
        if (!node) return FALSE;
        gui.nodes[i] = node;
        AddTail(second ? &gui.buttons2 : &gui.buttons, node);
        hints[i].hi_GadgetID = rows && tools[i].row ? GID_TOOLBAR2 : GID_TOOLBAR;
        hints[i].hi_Code = tools[i].cmd;
        hints[i].hi_Text = (STRPTR)S(tools[i].help);
        hints[i].hi_Flags = 0;
    }
    hints[i].hi_GadgetID = GID_OVERFLOW;
    hints[i].hi_Code = -1;
    hints[i].hi_Text = (STRPTR)S(MSG_TBH_MORE);
    hints[i].hi_Flags = 0;
    i++;
    hints[i].hi_GadgetID = hints[i].hi_Code = -1;
    return TRUE;
}

static Object *make_bar(ULONG id, struct List *buttons, BOOL frames)
{
    return NewObject(SPEEDBAR_GetClass(), NULL,
        GA_ID,                id,
        GA_RelVerify,         TRUE,
        SPEEDBAR_Orientation, SBORIENT_HORIZ,
        SPEEDBAR_Buttons,     (ULONG)buttons,
        /* No frame around the bar. Flat buttons get one only while
         * pressed (ButtonBevelStyle is V47, older versions ignore it);
         * with BVS_NONE bevel.image draws no ghost pattern for disabled
         * buttons either, they show the ghosted image instead.        */
        SPEEDBAR_BevelStyle,       BVS_NONE,
        SPEEDBAR_ButtonBevelStyle, frames ? BVS_BUTTON : BVS_NONE,
        TAG_DONE);
}

/* the window for the help texts of the speedbars */
static void bars_window(struct Window *win)
{
    Object *bars[2];
    ULONG i;
    bars[0] = gui.toolbar;
    bars[1] = gui.toolbar2;
    for (i = 0; i < 2; i++)
        if (bars[i]) SetGadgetAttrs((struct Gadget *)bars[i], win, NULL, SPEEDBAR_Window, (ULONG)win, TAG_DONE);
}

ULONG gui_tool_count(void)
{
    return NUMTOOLS;
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

    /* the tool images are remapped for this screen */
    gui.ghostpen = -1;
    if (!(gui.screen = LockPubScreen(NULL)) || !(gui.dri = GetScreenDrawInfo(gui.screen)))
        return FALSE;
    /* texts of the buttons; prefs_cleanup() closes it at the end */
    if (!LabelBase) LabelBase = OpenLibrary((STRPTR)"images/label.image", 44);
    obtain_ghostpen();
    splash_status((CONST_STRPTR)S(set->tbmode == TBMODE_TEXT ? MSG_SPLASH_TOOLBAR : MSG_SPLASH_IMAGES));
    if (!make_buttons(set)) return FALSE;
    build_menus(set, highlight);

    gui.tbframes = set->tbframes;
    gui.toolbar = make_bar(GID_TOOLBAR, &gui.buttons, set->tbframes);
    if (gui.rows && set->fmtbuttons &&
        !(gui.toolbar2 = make_bar(GID_TOOLBAR2, &gui.buttons2, set->tbframes)))
        return FALSE;
    /* the bars in a group of their own: the second one can be removed
     * and added again (gui_show_format())                           */
    gui.tbgroup = gui.toolbar ? NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_InnerSpacing,  0,
        LAYOUT_AddChild,      (ULONG)gui.toolbar,
        /* its size changes when the formatting buttons come and go */
        CHILD_CacheDomain,    FALSE,
        gui.toolbar2 ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)gui.toolbar2,
        TAG_DONE) : NULL;
    if (!gui.tbgroup) return FALSE;
    /* right of the bars: a drop-down chooser (only its arrow) with the
     * buttons that do not fit (gui_update_overflow())               */
    NewList(&gui.oflist);
    if (!ChooserBase) ChooserBase = OpenLibrary((STRPTR)"gadgets/chooser.gadget", 44);
    if (ChooserBase)
        gui.overflow = NewObject(CHOOSER_GetClass(), NULL,
            GA_ID,             GID_OVERFLOW,
            GA_RelVerify,      TRUE,
            GA_Disabled,       TRUE,
            CHOOSER_DropDown,  TRUE,        /* no title: only the arrow */
            CHOOSER_Labels,    (ULONG)&gui.oflist,
            CHOOSER_MaxLabels, MAXTOOLS,
            CHOOSER_AutoFit,   TRUE,
            TAG_DONE);
    gui.tbouter = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,    LAYOUT_ORIENT_HORIZ,
        LAYOUT_VertAlignment,  LALIGN_CENTER,
        LAYOUT_AddChild,       (ULONG)gui.tbgroup,
        CHILD_CacheDomain,     FALSE,
        gui.overflow ? LAYOUT_AddChild : TAG_IGNORE, (ULONG)gui.overflow,
        gui.overflow ? CHILD_WeightedWidth : TAG_IGNORE, 0,
        /* just wide enough for the arrow (chooser style guide) */
        gui.overflow ? CHILD_MinWidth : TAG_IGNORE, 20,
        gui.overflow ? CHILD_MaxWidth : TAG_IGNORE, 20,
        /* as high as a text line, not as the bars */
        gui.overflow ? CHILD_MaxHeight : TAG_IGNORE, gui.screen->Font->ta_YSize + 6,
        TAG_DONE);
    if (!gui.tbouter) {
        DisposeObject(gui.tbgroup);
        gui.tbgroup = NULL;
        if (gui.overflow) DisposeObject(gui.overflow);
        gui.overflow = NULL;
        return FALSE;
    }
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

        LAYOUT_AddChild,    (ULONG)gui.tbouter,
        CHILD_WeightedHeight, 0,
        CHILD_CacheDomain,  FALSE,

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
    splash_status((CONST_STRPTR)S(MSG_SPLASH_WINDOW));
    if (!(gui.win = (struct Window *)DoMethod(gui.winobj, WM_OPEN))) return FALSE;
    apply_checks();

    bars_window(gui.win);
    gui_activate_editor();
    return TRUE;
}

static void free_nodes(struct List *l)
{
    struct Node *node, *next;
    if (l->lh_Head)
        for (node = l->lh_Head; (next = node->ln_Succ); node = next)
            FreeSpeedButtonNode(node);
}

void gui_close(void)
{
    ULONG i;

    if (gui.winobj) DisposeObject(gui.winobj);      /* disposes all gadgets */
    else if (gui.layout) DisposeObject(gui.layout);
    else {
        Object *objs[] = { gui.tbouter ? gui.tbouter : gui.toolbar, gui.tbouter ? NULL : gui.toolbar2,
                           gui.editor, gui.escroll, gui.html,
                           gui.vscroll, gui.hscroll, gui.status, gui.pos };
        for (i = 0; i < sizeof(objs) / sizeof(objs[0]); i++)
            if (objs[i]) DisposeObject(objs[i]);
    }
    /* the speedbar neither frees its nodes nor their images; a label
     * disposes its image                                          */
    free_nodes(&gui.buttons);
    free_nodes(&gui.buttons2);
    if (gui.oflist.lh_Head) {
        struct Node *node, *next;
        for (node = gui.oflist.lh_Head; (next = node->ln_Succ); node = next)
            FreeChooserNode(node);
    }
    for (i = 0; i < MAXTOOLS; i++)
        if (gui.images[i]) DisposeObject(gui.images[i]);
    for (i = 0; i < MAXTOOLS; i++)
        if (gui.ghosts[i]) DisposeObject(gui.ghosts[i]);
    for (i = 0; i < MAXTOOLS; i++)
        if (gui.selimgs[i]) DisposeObject(gui.selimgs[i]);
    if (gui.smallfont) CloseFont(gui.smallfont);   /* after the labels */
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    DiskfontBase = NULL;
    highlight_release();                            /* after the editor is gone */
    if (gui.screen) {
        if (gui.ghostpen >= 0) ReleasePen(gui.screen->ViewPort.ColorMap, gui.ghostpen);
        if (gui.dri) FreeScreenDrawInfo(gui.screen, gui.dri);
        UnlockPubScreen(NULL, gui.screen);
    }
    memset(&gui, 0, sizeof(gui));
}

/* Ghosts the buttons whose bit (TOOLBIT(cmd)) is set in 'mask': they
 * show the ghosted image or text and cannot be selected. The node list
 * has to be taken from the speedbar while it is changed and the bar
 * redrawn, so this only happens for a bar whose buttons change.     */
void gui_tools_disabled(ULONG mask)
{
    ULONG changed = mask ^ gui.disabled, b, i;

    if (!gui.toolbar || !changed) return;
    gui.disabled = mask;
    for (b = 0; b < 2; b++) {
        Object *bar = b ? gui.toolbar2 : gui.toolbar;
        struct List *list = b ? &gui.buttons2 : &gui.buttons;
        struct Node *node;
        BOOL detached = FALSE;

        if (!bar) continue;
        for (node = list->lh_Head; node->ln_Succ; node = node->ln_Succ) {
            BOOL off;
            Object *img;
            for (i = 0; i < NUMTOOLS && gui.nodes[i] != node; i++) ;
            if (i == NUMTOOLS || !(changed & TOOLBIT(tools[i].cmd))) continue;
            off = (mask & TOOLBIT(tools[i].cmd)) != 0;
            img = off && gui.images[i] && gui.ghosts[i] ? gui.ghosts[i] : gui.images[i];
            if (!detached) {
                SetGadgetAttrs((struct Gadget *)bar, gui.win, NULL, SPEEDBAR_Buttons, ~0UL, TAG_DONE);
                detached = TRUE;
            }
            SetSpeedButtonNodeAttrs(gui.nodes[i],
                SBNA_Disabled, off,
                img ? SBNA_Image : TAG_IGNORE, (ULONG)img,
                TAG_DONE);
        }
        if (!detached) continue;
        SetGadgetAttrs((struct Gadget *)bar, gui.win, NULL, SPEEDBAR_Buttons, (ULONG)list, TAG_DONE);
        /* the speedbar does not draw itself when the list is attached */
        if (gui.win) RefreshGList((struct Gadget *)bar, gui.win, NULL, 1);
    }
}

/* Shows or hides the formatting buttons. With one bar their nodes move
 * between the bar's list and gui.buttons2; the second bar is removed
 * from the layout (which disposes it) and made anew. Adding a child to
 * a layout needs layout.gadget V47: FALSE if it cannot be done now.  */
BOOL gui_show_format(BOOL on)
{
    ULONG i;

    if (!gui.toolbar) return FALSE;
    if (gui.rows) {
        if (!on == !gui.toolbar2) return TRUE;
        if (on) {
            if (LayoutBase->lib_Version < 47) return FALSE;
            if (!(gui.toolbar2 = make_bar(GID_TOOLBAR2, &gui.buttons2, gui.tbframes))) return FALSE;
            /* The bar draws itself when it is added, before the layout
             * places it: put it where it goes, below the first bar,
             * not at 0/0 over the window title.                      */
            {
                struct Gadget *g = (struct Gadget *)gui.toolbar;
                SetAttrs(gui.toolbar2, GA_Left, g->LeftEdge, GA_Top, g->TopEdge + g->Height,
                         GA_Width, g->Width, GA_Height, g->Height, TAG_DONE);
            }
            SetGadgetAttrs((struct Gadget *)gui.tbgroup, gui.win, NULL,
                           LAYOUT_AddChild, (ULONG)gui.toolbar2, TAG_DONE);
            if (gui.win) bars_window(gui.win);
        } else {
            SetGadgetAttrs((struct Gadget *)gui.tbgroup, gui.win, NULL,
                           LAYOUT_RemoveChild, (ULONG)gui.toolbar2, TAG_DONE);
            gui.toolbar2 = NULL;
        }
    } else {
        struct Node *pred = NULL;
        BOOL shown = FALSE;
        for (i = 0; i < NUMTOOLS; i++)
            if (tools[i].row) {
                struct Node *n;
                for (n = gui.buttons.lh_Head; n->ln_Succ; n = n->ln_Succ)
                    if (n == gui.nodes[i]) shown = TRUE;
                break;
            }
        if (!on == !shown) return TRUE;
        SetGadgetAttrs((struct Gadget *)gui.toolbar, gui.win, NULL, SPEEDBAR_Buttons, ~0UL, TAG_DONE);
        for (i = 0; i < NUMTOOLS; i++) {
            if (!tools[i].row) {
                if (on && !pred && i + 1 < NUMTOOLS && tools[i + 1].row) pred = gui.nodes[i];
                continue;
            }
            Remove(gui.nodes[i]);
            if (on) {
                /* in their place, after the button before them */
                Insert(&gui.buttons, gui.nodes[i], pred);
                pred = gui.nodes[i];
            } else AddTail(&gui.buttons2, gui.nodes[i]);
        }
        SetGadgetAttrs((struct Gadget *)gui.toolbar, gui.win, NULL,
                       SPEEDBAR_Buttons, (ULONG)&gui.buttons, TAG_DONE);
    }
    if (gui.win) RethinkLayout((struct Gadget *)gui.layout, gui.win, NULL, TRUE);
    gui_update_overflow();
    return TRUE;
}

/* index of button node n in tools[], NUMTOOLS if none */
static ULONG tool_of(struct Node *n)
{
    ULONG i;
    for (i = 0; i < NUMTOOLS && gui.nodes[i] != n; i++) ;
    return i;
}

/* The buttons a bar does not show: those after the visible ones (the
 * bar is never scrolled, the first button is always the first one
 * shown). Adds them to the overflow list.                           */
static void add_hidden(Object *bar, struct List *list, ULONG mask)
{
    ULONG vis = 0, n = 0, i;
    struct Node *node, *c;

    if (!bar) return;
    GetAttr(SPEEDBAR_Visible, bar, &vis);
    for (node = list->lh_Head; node->ln_Succ; node = node->ln_Succ, n++) {
        if (n < vis || (i = tool_of(node)) == NUMTOOLS) continue;
        if ((c = AllocChooserNode(
                CNA_Text,     (ULONG)S(tools[i].label),
                CNA_UserData, (ULONG)tools[i].cmd,
                CNA_Disabled, (mask & TOOLBIT(tools[i].cmd)) != 0,
                TAG_DONE)))
            AddTail(&gui.oflist, c);
    }
}

/* Updates the overflow list when the visible buttons or the ghosted
 * ones changed; called on every tick. The chooser is ghosted when
 * every button fits.                                                      */
void gui_update_overflow(void)
{
    ULONG key[3] = { 0, 0, 0 };
    struct Node *node, *next;

    if (!gui.overflow || !gui.win) return;
    if (gui.toolbar) GetAttr(SPEEDBAR_Visible, gui.toolbar, &key[0]);
    if (gui.toolbar2) GetAttr(SPEEDBAR_Visible, gui.toolbar2, &key[1]);
    else key[1] = ~0UL;
    /* the number of buttons in the first bar changes with the
     * formatting buttons, the ghosted ones with the mask          */
    for (node = gui.buttons.lh_Head; node->ln_Succ; node = node->ln_Succ) key[0] += 0x10000;
    key[2] = gui.disabled;
    if (!memcmp(key, gui.ofkey, sizeof(key))) return;
    memcpy(gui.ofkey, key, sizeof(key));

    SetGadgetAttrs((struct Gadget *)gui.overflow, gui.win, NULL, CHOOSER_Labels, ~0UL, TAG_DONE);
    for (node = gui.oflist.lh_Head; (next = node->ln_Succ); node = next) {
        Remove(node);
        FreeChooserNode(node);
    }
    add_hidden(gui.toolbar, &gui.buttons, gui.disabled);
    /* a separator between the buttons of the two bars */
    if (!IsListEmpty(&gui.oflist)) {
        struct Node *sep = AllocChooserNode(CNA_Separator, TRUE, TAG_DONE);
        if (sep) AddTail(&gui.oflist, sep);
        add_hidden(gui.toolbar2, &gui.buttons2, gui.disabled);
        if (sep && gui.oflist.lh_TailPred == sep) {
            Remove(sep);                /* nothing hidden in the second bar */
            FreeChooserNode(sep);
        }
    } else add_hidden(gui.toolbar2, &gui.buttons2, gui.disabled);
    SetGadgetAttrs((struct Gadget *)gui.overflow, gui.win, NULL,
                   CHOOSER_Labels, (ULONG)&gui.oflist,
                   GA_Disabled,    IsListEmpty(&gui.oflist),
                   TAG_DONE);
}

/* the command of entry 'index' of the overflow list, 0 if none */
ULONG gui_overflow_cmd(ULONG index)
{
    struct Node *node;
    ULONG cmd = 0;

    for (node = gui.oflist.lh_Head; node->ln_Succ && index; node = node->ln_Succ) index--;
    if (!node->ln_Succ) return 0;
    GetChooserNodeAttrs(node, CNA_UserData, (ULONG)&cmd, TAG_DONE);
    return cmd;
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
    snprintf(buf, sizeof(buf), S(MSG_POSITION), (unsigned long)line, (unsigned long)col);
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
    remember_checks();
    SetAttrs(gui.toolbar, SPEEDBAR_Window, 0, TAG_DONE);
    if (gui.toolbar2) SetAttrs(gui.toolbar2, SPEEDBAR_Window, 0, TAG_DONE);
    DoMethod(gui.winobj, WM_ICONIFY);
    gui.win = NULL;
}

/* FALSE if the window could not be opened again */
BOOL gui_uniconify(void)
{
    if (gui.win) return TRUE;
    if (!(gui.win = (struct Window *)DoMethod(gui.winobj, WM_OPEN))) return FALSE;
    apply_checks();
    bars_window(gui.win);
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
/* window.class makes the menu strip anew from menus[] at every WM_OPEN
 * (also after iconifying), so the table keeps the current check marks */
static void remember_check(ULONG cmd, BOOL on)
{
    ULONG i;
    for (i = 0; i < NUMMENUS; i++)
        if (menus[i].nm_UserData == (APTR)cmd) {
            if (on) menus[i].nm_Flags |= CHECKED;
            else menus[i].nm_Flags &= ~CHECKED;
        }
}

/* before the window closes: the check marks the user set in the menu */
static void remember_checks(void)
{
    struct MenuItem *item;
    ULONG i;
    if (!gui.win) return;
    for (i = 0; i < NUMMENUS; i++)
        if ((menudef[i].flags & CHECKIT) && (item = find_item(menudef[i].cmd)))
            remember_check(menudef[i].cmd, (item->Flags & CHECKED) != 0);
}

/* after the window opened: the menu shows the check marks of the table */
static void apply_checks(void)
{
    ULONG i;
    for (i = 0; i < NUMMENUS; i++)
        if (menudef[i].flags & CHECKIT)
            gui_set_checked(menudef[i].cmd, (menus[i].nm_Flags & CHECKED) != 0);
}

void gui_set_checked(ULONG cmd, BOOL on)
{
    struct MenuItem *item;
    struct Menu *strip;

    remember_check(cmd, on);
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
