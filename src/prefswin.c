/*
 * MDEdit - settings window
 *
 * A list of categories on the left, the options of the selected one on
 * the right (page.gadget), Save/Use/Cancel below. The window is modal:
 * the main window shows the busy pointer and its input is dropped.
 *
 * The groups are built with complete tag lists: CHILD_Label applies to
 * the child added in the same tag list, and LAYOUT_AddChild with OM_SET
 * needs layout.gadget V47. The gadgets of a page keep their height
 * (CHILD_WeightedHeight 0); an empty group at the end takes the rest, so
 * a bigger window does not stretch them.
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <gadgets/string.h>
#include <gadgets/getfile.h>
#include <gadgets/getcolor.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/integer.h>
#include <proto/string.h>
#include <proto/getfile.h>
#include <proto/getcolor.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <string.h>

#include "mdedit.h"
#include "settings.h"

/* initialised explicitly, see mdedit.c */
struct Library *ListBrowserBase = NULL, *ChooserBase = NULL, *CheckBoxBase = NULL,
               *IntegerBase = NULL, *StringBase = NULL, *GetFileBase = NULL,
               *GetColorBase = NULL, *LabelBase = NULL;

enum {
    PG_LIST = 100, PG_HIGHLIGHT, PG_LINENUMBERS,
    PG_RENDERER, PG_FONTSET, PG_FONTSIZE, PG_AUTOREFRESH, PG_SYNC,
    PG_DIALECT, PG_CHARSET, PG_TEMPLATE, PG_NOTEMPLATE,
    PG_COLOUR, PG_DEFCOLOURS = PG_COLOUR + NUMCOLOURS,
    PG_WIDTH, PG_HEIGHT, PG_LEFT, PG_TOP, PG_CURSIZE, PG_AUTOSIZE,
    PG_SAVE, PG_USE, PG_CANCEL
};

static const char *const categories[] = { "Editor", "Preview", "Markdown", "Colours", "Window" };
#define NUMCATEGORIES 5

static STRPTR renderers[] = { (STRPTR)"html.gadget (bitmap fonts)",
                              (STRPTR)"htmlttf.gadget (TrueType)", NULL };
static STRPTR fontsets[] = { (STRPTR)"Automatic", (STRPTR)"Vera", (STRPTR)"DejaVu",
                             (STRPTR)"Noto", NULL };
static STRPTR dialects[] = { (STRPTR)"GitHub", (STRPTR)"CommonMark", NULL };
static const char *const colour_labels[NUMCOLOURS] = {
    "_Headings ", "Co_de ", "_Quotes ", "_Markers, rules ", "_Links ", "_URLs ", "HT_ML "
};
static const char *const colour_titles[NUMCOLOURS] = {
    "Headings", "Code", "Quotes", "Markers and rules", "Links", "URLs", "HTML"
};

static struct {
    Object *winobj, *root, *list, *page;
    Object *highlight, *linenumbers, *renderer, *fontset, *fontsize, *autorefresh, *sync;
    Object *dialect, *charset, *template, *colours[NUMCOLOURS];
    Object *width, *height, *left, *top;
    struct Window *win;
    struct List catlist;
} pw;

/*****************************************************************************/

static BOOL open_classes(void)
{
    static const struct { struct Library **base; const char *name; } cl[] = {
        { &ListBrowserBase, "gadgets/listbrowser.gadget" },
        { &ChooserBase,     "gadgets/chooser.gadget" },
        { &CheckBoxBase,    "gadgets/checkbox.gadget" },
        { &IntegerBase,     "gadgets/integer.gadget" },
        { &StringBase,      "gadgets/string.gadget" },
        { &GetFileBase,     "gadgets/getfile.gadget" },
        { &GetColorBase,    "gadgets/getcolor.gadget" },
        { &LabelBase,       "images/label.image" },
    };
    ULONG i;

    for (i = 0; i < sizeof(cl) / sizeof(cl[0]); i++)
        if (!*cl[i].base && !(*cl[i].base = OpenLibrary((STRPTR)cl[i].name, 44))) {
            struct EasyStruct es = { sizeof(es), 0, (STRPTR)APPNAME,
                                     (STRPTR)"Could not open %s.", (STRPTR)"OK" };
            EasyRequest(gui.win, &es, NULL, (ULONG)cl[i].name);
            return FALSE;
        }
    return TRUE;
}

void prefs_cleanup(void)
{
    struct Library **b[] = { &ListBrowserBase, &ChooserBase, &CheckBoxBase, &IntegerBase,
                             &StringBase, &GetFileBase, &GetColorBase, &LabelBase };
    ULONG i;
    for (i = 0; i < sizeof(b) / sizeof(b[0]); i++)
        if (*b[i]) {
            CloseLibrary(*b[i]);
            *b[i] = NULL;
        }
}

static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_DONE);
}

static Object *checkbox(ULONG id, const char *text, BOOL on, BOOL disabled)
{
    return NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, GA_Disabled, disabled,
        CHECKBOX_Checked, on, TAG_DONE);
}

static Object *chooser(ULONG id, STRPTR *labels, ULONG selected, BOOL disabled)
{
    return NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Disabled, disabled, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)labels, CHOOSER_Selected, selected, TAG_DONE);
}

static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, TAG_DONE);
}

static ULONG find_label(STRPTR *labels, const char *text)
{
    ULONG i;
    for (i = 0; labels[i]; i++)
        if (!strcmp((const char *)labels[i], text)) return i;
    return 0;
}

/* Spacing in "virtual pixels" (layout.gadget scales them to the bevel
 * settings). The labels end with a space: label.image has no margin of
 * its own, and a space grows with the font.                            */
#define FRAME_SIDE   8              /* inside the frame of a page */
#define FRAME_TOPBOT 6
#define ROW_SPACING  4              /* between the rows of a page */
#define AREA_SPACING 6              /* window border, list, frame, buttons */

/* tags of a page: vertical group with a frame and the category as title */
#define PAGE_GROUP(title) \
    LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_BevelStyle, BVS_GROUP, \
    LAYOUT_Label, (ULONG)(title), LAYOUT_SpaceOuter, TRUE, \
    LAYOUT_LeftSpacing, FRAME_SIDE, LAYOUT_RightSpacing, FRAME_SIDE, \
    LAYOUT_TopSpacing, FRAME_TOPBOT, LAYOUT_BottomSpacing, FRAME_TOPBOT, \
    LAYOUT_InnerSpacing, ROW_SPACING

/* the child just added keeps its height */
#define FIXED       CHILD_WeightedHeight, 0

/* an empty group, takes the remaining space */
static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_DONE);
}

static Object *build(const struct Settings *s)
{
    Object *editor, *preview, *markdown, *colours, *window;
    ULONG i;

    /* categories */
    NewList(&pw.catlist);
    for (i = 0; i < NUMCATEGORIES; i++) {
        struct Node *n = AllocListBrowserNode(1, LBNCA_Text, (ULONG)categories[i], TAG_DONE);
        if (n) AddTail(&pw.catlist, n);
    }
    pw.list = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID,                    PG_LIST,
        GA_RelVerify,             TRUE,
        LISTBROWSER_Labels,       (ULONG)&pw.catlist,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_Selected,     0,
        LISTBROWSER_AutoFit,      TRUE,
        LISTBROWSER_VerticalProp, FALSE,
        TAG_DONE);

    /* Editor */
    pw.highlight = checkbox(PG_HIGHLIGHT, "Syntax _highlighting",
                            s->highlight && highlight_hook(), !highlight_hook());
    pw.linenumbers = checkbox(PG_LINENUMBERS, "Line _numbers", s->linenumbers, FALSE);
    editor = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(categories[0]),
        LAYOUT_AddChild, (ULONG)pw.highlight,   FIXED,
        LAYOUT_AddChild, (ULONG)pw.linenumbers, FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_DONE);

    /* Preview */
    pw.renderer = chooser(PG_RENDERER, renderers, s->ttf ? 1 : 0, FALSE);
    pw.fontset = chooser(PG_FONTSET, fontsets, s->fontset[0] ? find_label(fontsets, s->fontset) : 0,
                         !s->ttf);
    pw.fontsize = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, PG_FONTSIZE, GA_RelVerify, TRUE, GA_Disabled, !s->ttf,
        INTEGER_Number, s->fontsize, INTEGER_Minimum, 0, INTEGER_Maximum, 64,
        INTEGER_MaxChars, 3, TAG_DONE);
    pw.autorefresh = checkbox(PG_AUTOREFRESH, "_Refresh while typing", s->autorefresh, FALSE);
    pw.sync = checkbox(PG_SYNC, "_Synchronize scrolling", s->syncscroll, FALSE);
    preview = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(categories[1]),
        LAYOUT_AddChild, (ULONG)pw.renderer,    FIXED,
        CHILD_Label,     (ULONG)label("R_enderer "),
        LAYOUT_AddChild, (ULONG)pw.fontset,     FIXED,
        CHILD_Label,     (ULONG)label("_TrueType fonts "),
        LAYOUT_AddChild, (ULONG)pw.fontsize,    FIXED,
        CHILD_Label,     (ULONG)label("_Font size (0 = auto) "),
        LAYOUT_AddImage, (ULONG)label("Renderer and fonts take effect at the next start."),
        FIXED,
        LAYOUT_AddChild, (ULONG)pw.autorefresh, FIXED,
        LAYOUT_AddChild, (ULONG)pw.sync,        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_DONE);

    /* Markdown */
    pw.dialect = chooser(PG_DIALECT, dialects, find_label(dialects, s->dialect), FALSE);
    pw.charset = NewObject(STRING_GetClass(), NULL,
        GA_ID, PG_CHARSET, GA_RelVerify, TRUE, STRINGA_TextVal, (ULONG)s->charset,
        STRINGA_MaxChars, sizeof(s->charset) - 1, TAG_DONE);
    pw.template = NewObject(GETFILE_GetClass(), NULL,
        GA_ID, PG_TEMPLATE, GA_RelVerify, TRUE,
        GETFILE_TitleText, (ULONG)"Page template", GETFILE_FullFile, (ULONG)s->template,
        GETFILE_Pattern, (ULONG)"#?.(html|htm)", GETFILE_DoPatterns, TRUE,
        GETFILE_RejectIcons, TRUE, TAG_DONE);
    markdown = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(categories[2]),
        LAYOUT_AddChild, (ULONG)pw.dialect,     FIXED,
        CHILD_Label,     (ULONG)label("_Dialect "),
        LAYOUT_AddChild, (ULONG)pw.charset,     FIXED,
        CHILD_Label,     (ULONG)label("_Charset (empty = detect) "),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild,     (ULONG)pw.template,
            LAYOUT_AddChild,     (ULONG)button(PG_NOTEMPLATE, "Built-_in"),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        FIXED,
        CHILD_Label,     (ULONG)label("Page _template "),
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_DONE);

    /* Colours */
    for (i = 0; i < NUMCOLOURS; i++)
        pw.colours[i] = NewObject(GETCOLOR_GetClass(), NULL,
            GA_ID, PG_COLOUR + i, GA_RelVerify, TRUE,
            GETCOLOR_Screen, (ULONG)gui.screen, GETCOLOR_Color, s->colours[i],
            GETCOLOR_TitleText, (ULONG)colour_titles[i], TAG_DONE);
    colours = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(categories[3]),
        LAYOUT_AddChild, (ULONG)pw.colours[0], FIXED, CHILD_Label, (ULONG)label(colour_labels[0]),
        LAYOUT_AddChild, (ULONG)pw.colours[1], FIXED, CHILD_Label, (ULONG)label(colour_labels[1]),
        LAYOUT_AddChild, (ULONG)pw.colours[2], FIXED, CHILD_Label, (ULONG)label(colour_labels[2]),
        LAYOUT_AddChild, (ULONG)pw.colours[3], FIXED, CHILD_Label, (ULONG)label(colour_labels[3]),
        LAYOUT_AddChild, (ULONG)pw.colours[4], FIXED, CHILD_Label, (ULONG)label(colour_labels[4]),
        LAYOUT_AddChild, (ULONG)pw.colours[5], FIXED, CHILD_Label, (ULONG)label(colour_labels[5]),
        LAYOUT_AddChild, (ULONG)pw.colours[6], FIXED, CHILD_Label, (ULONG)label(colour_labels[6]),
        /* the button on the right, not across the page */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild,     (ULONG)filler(),
            LAYOUT_AddChild,     (ULONG)button(PG_DEFCOLOURS, "Default c_olours"),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_DONE);

    /* Window */
    pw.width = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, PG_WIDTH, GA_RelVerify, TRUE, INTEGER_Number, s->winwidth,
        INTEGER_Minimum, 0, INTEGER_Maximum, 9999, INTEGER_MaxChars, 4, TAG_DONE);
    pw.height = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, PG_HEIGHT, GA_RelVerify, TRUE, INTEGER_Number, s->winheight,
        INTEGER_Minimum, 0, INTEGER_Maximum, 9999, INTEGER_MaxChars, 4, TAG_DONE);
    pw.left = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, PG_LEFT, GA_RelVerify, TRUE, INTEGER_Number, s->winleft,
        INTEGER_Minimum, -1, INTEGER_Maximum, 9999, INTEGER_MaxChars, 4, TAG_DONE);
    pw.top = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, PG_TOP, GA_RelVerify, TRUE, INTEGER_Number, s->wintop,
        INTEGER_Minimum, -1, INTEGER_Maximum, 9999, INTEGER_MaxChars, 4, TAG_DONE);
    window = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(categories[4]),
        LAYOUT_AddChild, (ULONG)pw.width,  FIXED,
        CHILD_Label,     (ULONG)label("_Width (0 = auto) "),
        LAYOUT_AddChild, (ULONG)pw.height, FIXED,
        CHILD_Label,     (ULONG)label("H_eight (0 = auto) "),
        LAYOUT_AddChild, (ULONG)pw.left,   FIXED,
        CHILD_Label,     (ULONG)label("_Left (-1 = centred) "),
        LAYOUT_AddChild, (ULONG)pw.top,    FIXED,
        CHILD_Label,     (ULONG)label("_Top (-1 = centred) "),
        LAYOUT_AddImage, (ULONG)label("Size and position of the main window at the next start."),
        FIXED,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_EvenSize,     TRUE,
            LAYOUT_AddChild,     (ULONG)filler(),
            LAYOUT_AddChild,     (ULONG)button(PG_CURSIZE, "Cu_rrent"),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild,     (ULONG)button(PG_AUTOSIZE, "_Automatic"),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        FIXED,
        LAYOUT_AddChild, (ULONG)filler(),
        TAG_DONE);

    pw.page = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add,     (ULONG)editor,
        PAGE_Add,     (ULONG)preview,
        PAGE_Add,     (ULONG)markdown,
        PAGE_Add,     (ULONG)colours,
        PAGE_Add,     (ULONG)window,
        PAGE_Current, 0,
        TAG_DONE);

    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_LeftSpacing,   AREA_SPACING,
        LAYOUT_RightSpacing,  AREA_SPACING,
        LAYOUT_TopSpacing,    AREA_SPACING,
        LAYOUT_BottomSpacing, AREA_SPACING,
        LAYOUT_InnerSpacing,  AREA_SPACING,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_InnerSpacing, AREA_SPACING,
            LAYOUT_AddChild,     (ULONG)pw.list,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      gui.screen->RastPort.TxWidth * 12,
            LAYOUT_AddChild,     (ULONG)pw.page,
            TAG_DONE),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_EvenSize,    TRUE,
            LAYOUT_AddChild,    (ULONG)button(PG_SAVE, "_Save"),
            LAYOUT_AddChild,    (ULONG)button(PG_USE, "_Use"),
            LAYOUT_AddChild,    (ULONG)button(PG_CANCEL, "_Cancel"),
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);
}

static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

static void read_gadgets(struct Settings *s)
{
    STRPTR str;
    ULONG i;

    if (highlight_hook()) s->highlight = get(pw.highlight, CHECKBOX_Checked) != 0;
    s->linenumbers = get(pw.linenumbers, CHECKBOX_Checked) != 0;
    s->ttf = get(pw.renderer, CHOOSER_Selected) == 1;
    i = get(pw.fontset, CHOOSER_Selected);
    strcpy(s->fontset, i ? (const char *)fontsets[i] : "");
    s->fontsize = (LONG)get(pw.fontsize, INTEGER_Number);
    s->autorefresh = get(pw.autorefresh, CHECKBOX_Checked) != 0;
    s->syncscroll = get(pw.sync, CHECKBOX_Checked) != 0;
    strcpy(s->dialect, (const char *)dialects[get(pw.dialect, CHOOSER_Selected) ? 1 : 0]);
    s->charset[0] = 0;
    if ((str = (STRPTR)get(pw.charset, STRINGA_TextVal)))
        strncat(s->charset, (const char *)str, sizeof(s->charset) - 1);
    s->template[0] = 0;
    if ((str = (STRPTR)get(pw.template, GETFILE_FullFile)) && *FilePart(str))
        strncat(s->template, (const char *)str, sizeof(s->template) - 1);
    for (i = 0; i < NUMCOLOURS; i++)
        s->colours[i] = get(pw.colours[i], GETCOLOR_Color);
    s->winwidth = (LONG)get(pw.width, INTEGER_Number);
    s->winheight = (LONG)get(pw.height, INTEGER_Number);
    s->winleft = (LONG)get(pw.left, INTEGER_Number);
    s->wintop = (LONG)get(pw.top, INTEGER_Number);
}

/* gadgets on a hidden page need SetPageGadgetAttrs() */
static void set_page_gadget(Object *o, ULONG tag, ULONG value)
{
    SetPageGadgetAttrs((struct Gadget *)o, pw.page, pw.win, NULL, tag, value, TAG_DONE);
}

/*****************************************************************************/

int prefs_dialog(struct Settings *s)
{
    ULONG sig = 0, mainsig = 0, result, i;
    UWORD code;
    int rc = PREFS_CANCEL;
    BOOL done = FALSE;

    if (!open_classes()) return PREFS_CANCEL;
    memset(&pw, 0, sizeof(pw));
    if (!(pw.root = build(s))) goto out;

    pw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)APPNAME " settings",
        WA_PubScreen,       (ULONG)gui.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_SizeGadget,      TRUE,
        WINDOW_RefWindow,   (ULONG)gui.win,
        WINDOW_Position,    WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)pw.root,
        TAG_DONE);
    if (!pw.winobj) {
        DisposeObject(pw.root);
        goto out;
    }
    if (!(pw.win = (struct Window *)DoMethod(pw.winobj, WM_OPEN))) goto out;

    gui_busy(TRUE);
    GetAttr(WINDOW_SigMask, gui.winobj, &mainsig);
    GetAttr(WINDOW_SigMask, pw.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);

        /* the main window waits: its input is dropped */
        if (got & mainsig)
            while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;

        while ((result = DoMethod(pw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            ULONG id = result & WMHI_GADGETMASK;

            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
                done = TRUE;
                continue;
            }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;

            switch (id) {
            case PG_LIST:
                SetGadgetAttrs((struct Gadget *)pw.page, pw.win, NULL,
                               PAGE_Current, get(pw.list, LISTBROWSER_Selected), TAG_DONE);
                RethinkLayout((struct Gadget *)pw.root, pw.win, NULL, TRUE);
                break;
            case PG_RENDERER: {
                BOOL off = get(pw.renderer, CHOOSER_Selected) != 1;
                set_page_gadget(pw.fontset, GA_Disabled, off);
                set_page_gadget(pw.fontsize, GA_Disabled, off);
                break;
            }
            case PG_TEMPLATE:
                gfRequestFile(pw.template, pw.win);
                break;
            case PG_NOTEMPLATE:
                set_page_gadget(pw.template, GETFILE_FullFile, (ULONG)"");
                break;
            case PG_DEFCOLOURS:
                for (i = 0; i < NUMCOLOURS; i++)
                    set_page_gadget(pw.colours[i], GETCOLOR_Color, default_colours[i]);
                break;
            case PG_CURSIZE:
                set_page_gadget(pw.width, INTEGER_Number, gui.win->Width);
                set_page_gadget(pw.height, INTEGER_Number, gui.win->Height);
                set_page_gadget(pw.left, INTEGER_Number, gui.win->LeftEdge);
                set_page_gadget(pw.top, INTEGER_Number, gui.win->TopEdge);
                break;
            case PG_AUTOSIZE:
                set_page_gadget(pw.width, INTEGER_Number, 0);
                set_page_gadget(pw.height, INTEGER_Number, 0);
                set_page_gadget(pw.left, INTEGER_Number, (ULONG)-1);
                set_page_gadget(pw.top, INTEGER_Number, (ULONG)-1);
                break;
            case PG_SAVE:
            case PG_USE:
                read_gadgets(s);
                rc = id == PG_SAVE ? PREFS_SAVE : PREFS_USE;
                done = TRUE;
                break;
            case PG_CANCEL:
                done = TRUE;
                break;
            default:
                if (id >= PG_COLOUR && id < PG_COLOUR + NUMCOLOURS) {
                    struct gcRequest msg;
                    msg.MethodID = GCOLOR_REQUEST;
                    msg.gcr_Window = pw.win;
                    DoMethodA(pw.colours[id - PG_COLOUR], (Msg)&msg);
                }
                break;
            }
        }
    }
    gui_busy(FALSE);

out:
    if (pw.winobj) DisposeObject(pw.winobj);        /* the gadgets with it */
    if (pw.catlist.lh_Head) FreeListBrowserList(&pw.catlist);
    memset(&pw, 0, sizeof(pw));
    return rc;
}
