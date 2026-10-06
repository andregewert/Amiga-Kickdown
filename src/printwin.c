/*
 * Kickdown - printing (window "Print"), also as PostScript or PDF
 *
 * Three kinds of output, all through the preview gadget (html.gadget or
 * htmlttf.gadget, V1.2):
 *  - printer: HTMLM_PrintBegin/Render, the pages as pixels through
 *    printer.device PRD_DUMPRPORTTAGS with DRPA_SourceHook (the driver
 *    asks for strips on its own task). Each page is sent with SendIO()
 *    while a modal progress window shows the pages and can stop the
 *    print; the main window waits.
 *    The whole sheet is sent in exactly the driver's dots, the driver
 *    puts it at the start of its printable area (its hardware margins
 *    add to ours, so the margins can be set).
 *  - PostScript: HTMLM_Export to a file, PRT:, PS: (the PostScript
 *    handler of TurboPrint) or any device.
 *  - PDF: HTMLM_Export to a file.
 * The options are part of the settings (tool types).
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <utility/hooks.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/chooser.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <gadgets/string.h>
#include <gadgets/getfile.h>
#include <gadgets/fuelgauge.h>
#include <gadgets/html.h>
#include <images/label.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/chooser.h>
#include <proto/checkbox.h>
#include <proto/integer.h>
#include <proto/string.h>
#include <proto/getfile.h>
#include <proto/fuelgauge.h>
#include <proto/label.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "kickdown.h"
#include "settings.h"

extern struct Library *HTMLBase;        /* kickdown.c */
struct Library *FuelGaugeBase = NULL;   /* only while printing */

enum {
    PW_MODE = 400, PW_DEST, PW_FILE, PW_DEVICE, PW_UNIT, PW_DENSITY, PW_FROM, PW_TO, PW_COPIES,
    PW_PAPER, PW_ML, PW_MT, PW_MR, PW_MB, PW_SERIF, PW_SIZE, PW_PAGENUMBERS, PW_BACKGROUNDS,
    PW_PRINT, PW_CANCEL, PW_STOP
};

/* the gadget renders the pages not finer than this; above, the source
 * hook enlarges them to the printer's dots                            */
#define MAXRENDERDPI 600

/* TurboPrint replaces printer.device; it does not know PRD_DUMPRPORTTAGS
 * but prints 24 bit RastPorts with its own command (tp_devel.lha of
 * IrseeSoft, TurboPrint Pro 3 or newer)                              */
#define TPMATCHWORD         0xf10a57efUL
#define PRD_TPEXTDUMPRPORT  (PRD_DUMPRPORT | 0x80)
#define TPFMT_RGB24         0x14
struct TPExtIODRP {
    UWORD PixAspX, PixAspY;         /* aspect ratio of a pixel */
    UWORD Mode;                     /* TPFMT_... */
};

/* a printer.device unit: what printer.device makes of densities 1-7 */
struct UnitInfo {
    BOOL ok;                        /* the unit could be opened */
    LONG prefdensity;               /* density of the printer settings */
    LONG dpi[8][2];                 /* x, y dpi of density 1-7, 0 = unknown */
};

static void query_unit(LONG unit, struct UnitInfo *ui);
static STRPTR paper_labels[] = { (STRPTR)"A4", (STRPTR)"A5", (STRPTR)"Letter", (STRPTR)"Legal", NULL };
/* the entries of the mode chooser: PostScript in two levels */
enum { MI_PRINTER, MI_PS2, MI_PS1, MI_PDF, NUMMI };
static STRPTR mode_labels[NUMMI + 1], font_labels[3];

static struct {
    Object *winobj, *root;
    Object *mode, *dest, *file, *device, *unit, *density, *from, *to, *copies;
    Object *paper, *margin[4], *serif, *size, *pagenumbers, *backgrounds, *print;
    struct Window *win;
    struct List destlist, unitlist, denslist;
    /* the configured printers (units) and the densities of the chosen one */
    LONG nunits, units[10];
    char unitlabels[10][72], denslabels[8][72];
    struct UnitInfo ui;
} pw;

/*****************************************************************************/
/* helpers                                                                   */

static Object *label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)text, TAG_DONE);
}

static Object *checkbox(ULONG id, const char *text, BOOL on)
{
    return NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text, CHECKBOX_Checked, on, TAG_DONE);
}

static Object *chooser(ULONG id, STRPTR *labels, ULONG selected)
{
    return NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_LabelArray, (ULONG)labels, CHOOSER_Selected, selected, TAG_DONE);
}

static Object *integer(ULONG id, LONG value, LONG min, LONG max, LONG chars)
{
    return NewObject(INTEGER_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        INTEGER_Number, value, INTEGER_Minimum, min, INTEGER_Maximum, max,
        INTEGER_MaxChars, chars, TAG_DONE);
}

static Object *button(ULONG id, const char *text)
{
    return NewObject(BUTTON_GetClass(), NULL,
        GA_ID, id, GA_RelVerify, TRUE, GA_Text, (ULONG)text,
        BUTTON_TextPadding, TRUE, TAG_DONE);
}

static Object *filler(void)
{
    return NewObject(LAYOUT_GetClass(), NULL, TAG_DONE);
}

static ULONG get(Object *o, ULONG attr)
{
    ULONG v = 0;
    GetAttr(attr, o, &v);
    return v;
}

/* the PostScript handler of TurboPrint */
static BOOL have_ps(void)
{
    struct DosList *dl = LockDosList(LDF_DEVICES | LDF_READ);
    BOOL found = FindDosEntry(dl, (STRPTR)"PS", LDF_DEVICES) != NULL;
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return found;
}

/* the file name with the extension of the output */
static void set_extension(char *file, ULONG size, LONG mode)
{
    char *dot = strrchr((char *)FilePart((STRPTR)file), '.');
    if (dot) *dot = 0;
    strncat(file, mode == PRMODE_PDF ? ".pdf" : ".ps", size - strlen(file) - 1);
}

/* points from mm */
static void free_nodes(struct List *l)
{
    struct Node *n, *next;
    if (!l->lh_Head) return;
    for (n = l->lh_Head; (next = n->ln_Succ); n = next) FreeChooserNode(n);
    NewList(l);
}

/* driver and unit name from ENV:Sys/Printer[N].prefs (chunks PTXT, PDEV) */
static BOOL read_unit_prefs(LONG unit, char *driver, char *name)
{
    UBYTE buf[512];
    char path[32];
    LONG len, i;
    BPTR fh;

    driver[0] = name[0] = 0;
    if (unit) sprintf(path, "ENV:Sys/Printer%ld.prefs", (long)unit);
    else strcpy(path, "ENV:Sys/Printer.prefs");
    if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) return FALSE;
    len = Read(fh, buf, sizeof(buf));
    Close(fh);
    if (len < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "PREF", 4)) return TRUE;
    for (i = 12; i + 8 <= len; ) {
        LONG size = (LONG)buf[i + 4] << 24 | (LONG)buf[i + 5] << 16 | (LONG)buf[i + 6] << 8 | buf[i + 7];
        UBYTE *d = buf + i + 8;
        if (size < 0 || i + 8 + size > len) break;
        if (!memcmp(buf + i, "PTXT", 4) && size >= 16 + 30) {
            memcpy(driver, d + 16, 30);
            driver[30] = 0;
        } else if (!memcmp(buf + i, "PDEV", 4) && size >= 20 + 32) {
            memcpy(name, d + 20, 32);
            name[32] = 0;
        }
        i += 8 + size + (size & 1);
    }
    return TRUE;
}

/* the name of the driver the unit uses (under TurboPrint not the one of
 * the printer settings)                                               */
static void unit_driver(LONG unit, char *driver)
{
    struct MsgPort *port;
    struct IOStdReq *io;

    if (!(port = CreateMsgPort())) return;
    if ((io = (struct IOStdReq *)CreateIORequest(port, sizeof(struct IODRPTagsReq)))) {
        if (!OpenDevice((STRPTR)"printer.device", unit, (struct IORequest *)io, 0)) {
            struct PrinterExtendedData *ped = &((struct PrinterData *)io->io_Device)->pd_SegmentData->ps_PED;
            if (ped->ped_PrinterName && ped->ped_PrinterName[0]) {
                strncpy(driver, (const char *)ped->ped_PrinterName, 30);
                driver[30] = 0;
            }
            CloseDevice((struct IORequest *)io);
        }
        DeleteIORequest((struct IORequest *)io);
    }
    DeleteMsgPort(port);
}

/* the printers to choose from: unit 0 and every unit with settings */
static void scan_units(void)
{
    char driver[32], name[34];
    LONG u;
    struct Node *n;

    NewList(&pw.unitlist);
    pw.nunits = 0;
    for (u = 0; u < 10; u++) {
        char *l = pw.unitlabels[pw.nunits];
        if (!read_unit_prefs(u, driver, name) && u) continue;
        unit_driver(u, driver);
        if (name[0] && driver[0]) snprintf(l, sizeof(pw.unitlabels[0]), "%ld: %s (%s)", (long)u, name, driver);
        else if (name[0] || driver[0]) snprintf(l, sizeof(pw.unitlabels[0]), "%ld: %s", (long)u, name[0] ? name : driver);
        else snprintf(l, sizeof(pw.unitlabels[0]), S(MSG_PR_UNIT), (long)u);
        if ((n = AllocChooserNode(CNA_Text, (ULONG)l, TAG_DONE))) {
            AddTail(&pw.unitlist, n);
            pw.units[pw.nunits++] = u;
        }
    }
}

/* the density entries of a unit: "as in the printer settings", 1-7 */
static void make_densities(LONG unit)
{
    struct Node *n;
    LONG d, pd;

    free_nodes(&pw.denslist);
    query_unit(unit, &pw.ui);
    pd = pw.ui.prefdensity;
    if (pd >= 1 && pd <= 7 && pw.ui.dpi[pd][0])
        snprintf(pw.denslabels[0], sizeof(pw.denslabels[0]), S(MSG_PR_DENSITY_PREFS),
                 (long)pd, (long)pw.ui.dpi[pd][0], (long)pw.ui.dpi[pd][1]);
    else strncpy(pw.denslabels[0], S(MSG_PR_DENSITY_ASPREFS), sizeof(pw.denslabels[0]) - 1);
    for (d = 1; d <= 7; d++)
        if (pw.ui.dpi[d][0])
            snprintf(pw.denslabels[d], sizeof(pw.denslabels[0]), S(MSG_PR_DENSITY_DPI),
                     (long)d, (long)pw.ui.dpi[d][0], (long)pw.ui.dpi[d][1]);
        else sprintf(pw.denslabels[d], "%ld", (long)d);
    for (d = 0; d <= 7; d++)
        if ((n = AllocChooserNode(CNA_Text, (ULONG)pw.denslabels[d], TAG_DONE))) AddTail(&pw.denslist, n);
}

static LONG mode_of(LONG item)
{
    return item == MI_PDF ? PRMODE_PDF : item == MI_PRINTER ? PRMODE_PRINTER : PRMODE_PS;
}

static LONG item_of(const struct Settings *s)
{
    return s->prmode == PRMODE_PDF ? MI_PDF : s->prmode == PRMODE_PS ? (s->pslevel == 1 ? MI_PS1 : MI_PS2) : MI_PRINTER;
}

static LONG mm_pt(LONG mm)
{
    return (mm * 720 + 127) / 254;
}

#define PAGE_GROUP(title) \
    LAYOUT_Orientation, LAYOUT_ORIENT_VERT, LAYOUT_BevelStyle, BVS_GROUP, \
    LAYOUT_Label, (ULONG)(title), LAYOUT_SpaceOuter, TRUE, \
    LAYOUT_LeftSpacing, 8, LAYOUT_RightSpacing, 8, \
    LAYOUT_TopSpacing, 6, LAYOUT_BottomSpacing, 6, LAYOUT_InnerSpacing, 4

#define FIXED       CHILD_WeightedHeight, 0

/*****************************************************************************/
/* the window                                                                */

/* only the gadgets that matter for the kind of output are usable */
static void update_gadgets(void)
{
    LONG mode = mode_of(get(pw.mode, CHOOSER_Selected)), dest = get(pw.dest, CHOOSER_Selected);
    BOOL ps = mode == PRMODE_PS, printer = mode == PRMODE_PRINTER;
    struct Window *w = pw.win;

    SetGadgetAttrs((struct Gadget *)pw.dest, w, NULL, GA_Disabled, !ps, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.file, w, NULL, GA_Disabled,
                   !(mode == PRMODE_PDF || (ps && dest == PRDEST_FILE)), TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.device, w, NULL, GA_Disabled, !(ps && dest == PRDEST_DEVICE), TAG_DONE);
    /* one configured printer: nothing to choose */
    SetGadgetAttrs((struct Gadget *)pw.unit, w, NULL, GA_Disabled, !printer || pw.nunits <= 1, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.density, w, NULL, GA_Disabled, !printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.copies, w, NULL, GA_Disabled, !printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.serif, w, NULL, GA_Disabled, printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.size, w, NULL, GA_Disabled, printer, TAG_DONE);
}

static Object *build(const struct Settings *s, const char *file, LONG from, LONG to)
{
    struct Node *n;
    LONG i, u;
    Object *output, *page;

    mode_labels[MI_PRINTER] = (STRPTR)S(MSG_PR_MODE_PRINTER);
    mode_labels[MI_PS2] = (STRPTR)"PostScript Level 2";
    mode_labels[MI_PS1] = (STRPTR)"PostScript Level 1";
    mode_labels[MI_PDF] = (STRPTR)"PDF";
    font_labels[0] = (STRPTR)S(MSG_PR_SANS);
    font_labels[1] = (STRPTR)S(MSG_PR_SERIF);

    /* destinations of PostScript; PS: only if TurboPrint provides it */
    NewList(&pw.destlist);
    for (i = 0; i < NUMPRDESTS; i++) {
        static const LONG msgs[NUMPRDESTS] = { MSG_PR_DEST_FILE, MSG_PR_DEST_PRT, MSG_PR_DEST_PS, MSG_PR_DEST_DEVICE };
        if ((n = AllocChooserNode(CNA_Text, (ULONG)S(msgs[i]),
                                  CNA_Disabled, i == PRDEST_PS && !have_ps(), TAG_DONE)))
            AddTail(&pw.destlist, n);
    }

    /* printers and the densities of the chosen one */
    scan_units();
    for (u = 0; u < pw.nunits && pw.units[u] != s->prunit; u++) ;
    if (u == pw.nunits) u = 0;
    NewList(&pw.denslist);
    make_densities(pw.units[u]);
    pw.mode = chooser(PW_MODE, mode_labels, item_of(s));
    pw.dest = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PW_DEST, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_Labels, (ULONG)&pw.destlist,
        CHOOSER_Selected, s->prdest >= 0 && s->prdest < NUMPRDESTS ? s->prdest : 0, TAG_DONE);
    pw.file = NewObject(GETFILE_GetClass(), NULL,
        GA_ID, PW_FILE, GA_RelVerify, TRUE,
        GETFILE_TitleText, (ULONG)S(MSG_PR_FILE_TITLE), GETFILE_FullFile, (ULONG)file,
        GETFILE_DoSaveMode, TRUE, GETFILE_RejectIcons, TRUE, TAG_DONE);
    pw.device = NewObject(STRING_GetClass(), NULL,
        GA_ID, PW_DEVICE, GA_RelVerify, TRUE, GA_TabCycle, TRUE,
        STRINGA_TextVal, (ULONG)(s->prdevice[0] ? s->prdevice : "PAR:"),
        STRINGA_MaxChars, sizeof(s->prdevice) - 1, TAG_DONE);
    pw.unit = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PW_UNIT, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_Labels, (ULONG)&pw.unitlist, CHOOSER_Selected, u, TAG_DONE);
    pw.density = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, PW_DENSITY, GA_RelVerify, TRUE, CHOOSER_PopUp, TRUE,
        CHOOSER_Labels, (ULONG)&pw.denslist,
        CHOOSER_Selected, s->prdensity >= 0 && s->prdensity <= 7 ? s->prdensity : 0, TAG_DONE);
    pw.from = integer(PW_FROM, from, 1, 9999, 4);
    pw.to = integer(PW_TO, to, 0, 9999, 4);
    pw.copies = integer(PW_COPIES, 1, 1, 99, 2);
    output = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PR_OUTPUT)),
        LAYOUT_AddChild, (ULONG)pw.mode,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_MODE)),
        LAYOUT_AddChild, (ULONG)pw.dest,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEST)),
        LAYOUT_AddChild, (ULONG)pw.file,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FILE)),
        LAYOUT_AddChild, (ULONG)pw.device, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEVICE)),
        LAYOUT_AddChild, (ULONG)pw.unit,    FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_PRINTER)),
        LAYOUT_AddChild, (ULONG)pw.density, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DENSITY)),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.from,
            LAYOUT_AddChild, (ULONG)pw.to,     CHILD_Label, (ULONG)label(S(MSG_PR_TO)),
            LAYOUT_AddChild, (ULONG)pw.copies, CHILD_Label, (ULONG)label(S(MSG_PR_COPIES)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FROM)),
        LAYOUT_AddImage, (ULONG)label(S(MSG_PR_RANGE_NOTE)), FIXED,
        TAG_DONE);

    pw.paper = chooser(PW_PAPER, paper_labels, s->paper >= 0 && s->paper < NUMPAPERS ? s->paper : 0);
    for (i = 0; i < 4; i++) pw.margin[i] = integer(PW_ML + i, s->margins[i], 0, 100, 3);
    pw.serif = chooser(PW_SERIF, font_labels, s->prserif ? 1 : 0);
    pw.size = integer(PW_SIZE, s->prsize, 4, 36, 2);
    pw.pagenumbers = checkbox(PW_PAGENUMBERS, S(MSG_PR_PAGENUMBERS), s->prpagenumbers);
    pw.backgrounds = checkbox(PW_BACKGROUNDS, S(MSG_PR_BACKGROUNDS), s->prbackgrounds);
    page = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PR_PAGE)),
        LAYOUT_AddChild, (ULONG)pw.paper, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_PAPER)),
        /* the margins in two rows: left/right, top/bottom */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.margin[0], CHILD_Label, (ULONG)label(S(MSG_PR_LEFT)),
            LAYOUT_AddChild, (ULONG)pw.margin[2], CHILD_Label, (ULONG)label(S(MSG_PR_RIGHT)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_MARGINS)),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.margin[1], CHILD_Label, (ULONG)label(S(MSG_PR_TOP)),
            LAYOUT_AddChild, (ULONG)pw.margin[3], CHILD_Label, (ULONG)label(S(MSG_PR_BOTTOM)),
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(" "),
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild, (ULONG)pw.serif,
            LAYOUT_AddChild, (ULONG)pw.size, CHILD_Label, (ULONG)label(S(MSG_PR_SIZE)),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FONT)),
        LAYOUT_AddChild, (ULONG)pw.pagenumbers, FIXED,
        LAYOUT_AddChild, (ULONG)pw.backgrounds, FIXED,
        LAYOUT_AddImage, (ULONG)label(S(MSG_PR_MARGIN_NOTE)), FIXED,
        TAG_DONE);

    pw.print = button(PW_PRINT, S(MSG_PR_PRINT));
    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_LeftSpacing,   6,
        LAYOUT_RightSpacing,  6,
        LAYOUT_TopSpacing,    6,
        LAYOUT_BottomSpacing, 6,
        LAYOUT_InnerSpacing,  6,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_AddChild, (ULONG)output, CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)page,   CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)filler(),
        /* positive action left, negative right */
        LAYOUT_AddChild, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_AddChild,     (ULONG)pw.print,
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild,     (ULONG)filler(),
            LAYOUT_AddChild,     (ULONG)button(PW_CANCEL, S(MSG_PR_CANCEL)),
            CHILD_WeightedWidth, 0,
            TAG_DONE),
        CHILD_WeightedHeight, 0,
        TAG_DONE);
}

static void read_gadgets(struct Settings *s, char *file, ULONG filesize, struct PrintJob *job)
{
    STRPTR str;
    LONG i, u = get(pw.unit, CHOOSER_Selected);

    s->prmode = mode_of(get(pw.mode, CHOOSER_Selected));
    if (s->prmode == PRMODE_PS) s->pslevel = get(pw.mode, CHOOSER_Selected) == MI_PS1 ? 1 : 2;
    s->prdest = get(pw.dest, CHOOSER_Selected);
    s->prdevice[0] = 0;
    if ((str = (STRPTR)get(pw.device, STRINGA_TextVal))) strncat(s->prdevice, (const char *)str, sizeof(s->prdevice) - 1);
    s->prunit = u >= 0 && u < pw.nunits ? pw.units[u] : 0;
    s->prdensity = get(pw.density, CHOOSER_Selected);
    s->paper = get(pw.paper, CHOOSER_Selected);
    for (i = 0; i < 4; i++) s->margins[i] = get(pw.margin[i], INTEGER_Number);
    s->prserif = get(pw.serif, CHOOSER_Selected) == 1;
    s->prsize = get(pw.size, INTEGER_Number);
    s->prpagenumbers = get(pw.pagenumbers, CHECKBOX_Checked) != 0;
    s->prbackgrounds = get(pw.backgrounds, CHECKBOX_Checked) != 0;
    file[0] = 0;
    if ((str = (STRPTR)get(pw.file, GETFILE_FullFile))) strncat(file, (const char *)str, filesize - 1);
    job->first = get(pw.from, INTEGER_Number);
    job->last = get(pw.to, INTEGER_Number);
    job->copies = get(pw.copies, INTEGER_Number);
}

int print_dialog(struct Settings *s, char *file, ULONG filesize, struct PrintJob *job)
{
    ULONG sig = 0, mainsig = 0, result;
    UWORD code;
    BOOL done = FALSE;
    int rc = FALSE;

    if (!prefs_open_classes()) return FALSE;
    memset(&pw, 0, sizeof(pw));
    set_extension(file, filesize, s->prmode);
    if (!(pw.root = build(s, file, job->first > 0 ? job->first : 1, job->last))) goto out;
    pw.winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)S(MSG_PR_TITLE),
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
    update_gadgets();

    gui_busy(TRUE);
    GetAttr(WINDOW_SigMask, gui.winobj, &mainsig);
    GetAttr(WINDOW_SigMask, pw.winobj, &sig);
    while (!done) {
        ULONG got = Wait(sig | mainsig);

        /* the main window waits: its input is dropped */
        if (got & mainsig)
            while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;

        while ((result = DoMethod(pw.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
                done = TRUE;
                continue;
            }
            if ((result & WMHI_CLASSMASK) != WMHI_GADGETUP) continue;
            switch (result & WMHI_GADGETMASK) {
            case PW_MODE: {
                char f[PATHLEN];
                STRPTR str = (STRPTR)get(pw.file, GETFILE_FullFile);
                f[0] = 0;
                if (str) strncat(f, (const char *)str, sizeof(f) - 1);
                if (f[0]) {
                    set_extension(f, sizeof(f), mode_of(get(pw.mode, CHOOSER_Selected)));
                    SetGadgetAttrs((struct Gadget *)pw.file, pw.win, NULL, GETFILE_FullFile, (ULONG)f, TAG_DONE);
                }
                update_gadgets();
                break;
            }
            case PW_DEST:
                update_gadgets();
                break;
            case PW_UNIT: {             /* the densities of the other printer */
                LONG u = get(pw.unit, CHOOSER_Selected), d = get(pw.density, CHOOSER_Selected);
                SetGadgetAttrs((struct Gadget *)pw.density, pw.win, NULL, CHOOSER_Labels, ~0, TAG_DONE);
                make_densities(u >= 0 && u < pw.nunits ? pw.units[u] : 0);
                SetGadgetAttrs((struct Gadget *)pw.density, pw.win, NULL,
                               CHOOSER_Labels, (ULONG)&pw.denslist, CHOOSER_Selected, d, TAG_DONE);
                break;
            }
            case PW_FILE:
                gfRequestFile(pw.file, pw.win);
                break;
            case PW_PRINT:
                read_gadgets(s, file, filesize, job);
                rc = TRUE;
                done = TRUE;
                break;
            case PW_CANCEL:
                done = TRUE;
                break;
            }
        }
    }
    gui_busy(FALSE);
out:
    if (pw.winobj) DisposeObject(pw.winobj);
    free_nodes(&pw.destlist);
    free_nodes(&pw.unitlist);
    free_nodes(&pw.denslist);
    memset(&pw, 0, sizeof(pw));
    return rc;
}

/*****************************************************************************/
/* running                                                                   */

static BOOL gadget_can_print(void)
{
    return HTMLBase && (HTMLBase->lib_Version > 1 || (HTMLBase->lib_Version == 1 && HTMLBase->lib_Revision >= 2));
}

struct PrintSrc {
    Object        *gadget;
    LONG           page;
    struct Task   *task;            /* told about each strip, for the progress */
    ULONG          sigmask;
    volatile LONG  row;             /* rows of the page delivered so far */
    LONG           sw, sh;          /* the page as the gadget renders it */
    LONG           dw, dh;          /* the page in printer dots */
    ULONG         *rowbuf;          /* one row of the gadget's page, sw pixels */
    LONG           cached;          /* the row in rowbuf, -1: none */
};

/* Called by printer.device on its task: the pixels of a strip, in
 * printer dots. printer.device gets the page in exactly its dots and
 * scales nothing (its scaling squeezed the page vertically); if the
 * gadget's page has another size, it is scaled here, nearest pixel.  */
static ULONG print_source(struct Hook *h __asm("a0"), APTR o __asm("a2"), struct DRPSourceMsg *m __asm("a1"))
{
    struct PrintSrc *ps = h->h_Data;
    ULONG rc = TRUE;

    if (ps->sw == ps->dw && ps->sh == ps->dh)
        rc = DoMethod(ps->gadget, HTMLM_PrintRender, ps->page, m->x, m->y, m->width, m->height, (ULONG)m->buf);
    else {
        ULONG step = ((ULONG)ps->sw << 16) / ps->dw, pos, *out = m->buf;
        LONG r, i, sy;
        for (r = 0; r < m->height; r++) {
            sy = ((2 * (m->y + r) + 1) * ps->sh) / (2 * ps->dh);
            if (sy >= ps->sh) sy = ps->sh - 1;
            if (sy != ps->cached) {
                if (!DoMethod(ps->gadget, HTMLM_PrintRender, ps->page, 0, sy, ps->sw, 1, (ULONG)ps->rowbuf))
                    rc = FALSE;
                ps->cached = sy;
            }
            pos = m->x * step + step / 2;
            for (i = 0; i < m->width; i++, pos += step) {
                ULONG sx = pos >> 16;
                *out++ = ps->rowbuf[sx < (ULONG)ps->sw ? sx : (ULONG)ps->sw - 1];
            }
        }
    }
    if ((LONG)(m->y + m->height) > ps->row) {
        ps->row = m->y + m->height;
        if (ps->sigmask) Signal(ps->task, ps->sigmask);
    }
    return rc;
}

/* a print to printer.device */
struct BitmapPrint {
    struct MsgPort      *port;
    struct IODRPTagsReq *io;
    struct Hook          hook;
    struct PrintSrc      src;
    struct RastPort      rp;
    struct ColorMap     *cmap;
    struct TagItem       drtags[4];
    LONG                 paper_w, paper_h, sw, sh;
    LONG                 dw, dh;    /* the page in printer dots */
    LONG                 density;   /* 1-7 */
    LONG                 olddensity;/* of the printer settings, restored at the end */
    BOOL                 opened;
    /* TurboPrint: the page in bands of RGB24 for PRD_TPEXTDUMPRPORT */
    BOOL                 tp;
    UBYTE               *tpbuf;     /* band rows * dw * 3 bytes */
    ULONG               *tprow;     /* one row 0x00RRGGBB, dw pixels */
    LONG                 band;      /* rows per band (dh: the whole page) */
    struct TPExtIODRP    ext;
    struct RastPort      tprp;
    struct BitMap        tpbm;
};

static void print_send(struct BitmapPrint *bp, LONG page)
{
    struct IODRPTagsReq *io = bp->io;

    bp->src.page = page;
    bp->src.row = 0;
    bp->src.cached = -1;
    io->io_Command = PRD_DUMPRPORTTAGS;
    io->io_RastPort = &bp->rp;
    io->io_ColorMap = bp->cmap;
    io->io_Modes = 0;
    io->io_SrcX = 0;
    io->io_SrcY = 0;
    /* the whole sheet, one source pixel per printer dot */
    io->io_SrcWidth = bp->dw;
    io->io_SrcHeight = bp->dh;
    io->io_DestCols = bp->dw;
    io->io_DestRows = bp->dh;
    io->io_Special = bp->density << 8;     /* SPECIAL_DENSITY1-7 */
    io->io_TagList = bp->drtags;
    SendIO((struct IORequest *)io);
}

/* the modal progress window of printing and export */
struct Progress {
    Object        *winobj, *text, *gauge, *stop;
    struct Window *win;
    ULONG          sig, mainsig;
    LONG           shown;
    BOOL           stopped;
    LONG           first, last;     /* export: the page range (last 0 = to the end) */
    LONG           max;
    char           buf[100];
};

/* 'widest': a text as wide as the longest one to come */
static void progress_open(struct Progress *p, const char *widest, LONG max)
{
    memset(p, 0, sizeof(*p));
    p->shown = -1;
    p->max = max > 0 ? max : 1;
    strncpy(p->buf, widest, sizeof(p->buf) - 1);
    p->text = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE, GA_Text, (ULONG)p->buf, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Justification, BCJ_LEFT, TAG_DONE);
    p->gauge = NewObject(FUELGAUGE_GetClass(), NULL,
        FUELGAUGE_Min, 0, FUELGAUGE_Max, p->max, FUELGAUGE_Level, 0,
        FUELGAUGE_Percent, TRUE, FUELGAUGE_Ticks, 0, TAG_DONE);
    p->stop = button(PW_STOP, S(MSG_PR_STOP));
    p->winobj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title,           (ULONG)S(MSG_PR_PROGRESS_TITLE),
        WA_PubScreen,       (ULONG)gui.screen,
        WA_Activate,        TRUE,
        WA_DepthGadget,     TRUE,
        WA_DragBar,         TRUE,
        WA_CloseGadget,     TRUE,
        WA_IDCMP,           IDCMP_RAWKEY,
        WINDOW_RefWindow,   (ULONG)gui.win,
        WINDOW_Position,    WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, (ULONG)NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,   TRUE,
            LAYOUT_DeferLayout,  TRUE,
            LAYOUT_AddChild,     (ULONG)p->text,
            LAYOUT_AddChild,     (ULONG)p->gauge,
            CHILD_MinWidth,      240,
            LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,    (ULONG)filler(),
                LAYOUT_AddChild,    (ULONG)p->stop,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,    (ULONG)filler(),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (p->winobj) p->win = (struct Window *)DoMethod(p->winobj, WM_OPEN);
    if (p->win) GetAttr(WINDOW_SigMask, p->winobj, &p->sig);
    GetAttr(WINDOW_SigMask, gui.winobj, &p->mainsig);
    gui_busy(TRUE);
}

/* a new text (also in the status bar) or NULL, and the gauge */
static void progress_set(struct Progress *p, const char *text, LONG level)
{
    if (text) {
        strncpy(p->buf, text, sizeof(p->buf) - 1);
        gui_status((CONST_STRPTR)p->buf);
        if (p->win) SetGadgetAttrs((struct Gadget *)p->text, p->win, NULL, GA_Text, (ULONG)p->buf, TAG_DONE);
    }
    if (p->win && level != p->shown)
        SetGadgetAttrs((struct Gadget *)p->gauge, p->win, NULL, FUELGAUGE_Level, p->shown = level, TAG_DONE);
}

/* The window's input; TRUE once Stop, the close gadget or Esc came. The
 * main window's input is dropped if 'main' (not while the gadget works
 * on our stack: the preview must not be drawn then).                   */
static BOOL progress_input(struct Progress *p, BOOL main)
{
    ULONG result;
    UWORD code;
    BOOL stop = FALSE;

    if (main) while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
    if (p->win)
        while ((result = DoMethod(p->winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG)
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW: stop = TRUE; break;
            case WMHI_GADGETUP: if ((result & WMHI_GADGETMASK) == PW_STOP) stop = TRUE; break;
            case WMHI_RAWKEY: if ((result & WMHI_KEYMASK) == 0x45) stop = TRUE; break;     /* Esc */
            }
    if (stop && !p->stopped) {
        p->stopped = TRUE;
        if (p->win) SetGadgetAttrs((struct Gadget *)p->stop, p->win, NULL, GA_Disabled, TRUE, TAG_DONE);
        return TRUE;
    }
    return FALSE;
}

static void progress_close(struct Progress *p)
{
    gui_busy(FALSE);
    if (p->winobj) DisposeObject(p->winobj);
    p->winobj = NULL;
    p->win = NULL;
}

/* The pages one after the other with the progress window: the page being
 * printed, a gauge over all pages and copies, Stop. Returns the pages
 * printed, -1 when stopped, or the negative printer error - 1.        */
/* waits for the request; TRUE when it is done (also when stopped) */
static void wait_io(struct BitmapPrint *bp, struct Progress *pr, LONG printed)
{
    ULONG portsig = 1UL << bp->port->mp_SigBit;

    for (;;) {
        ULONG got = Wait(pr->sig | pr->mainsig | portsig | bp->src.sigmask);

        if (!bp->tp && (got & bp->src.sigmask) && bp->dh > 0) {
            LONG row = bp->src.row;
            if (row > bp->dh) row = bp->dh;
            progress_set(pr, NULL, printed * 100 + row * 100 / bp->dh);
        }
        if (progress_input(pr, TRUE)) AbortIO((struct IORequest *)bp->io);
        if (CheckIO((struct IORequest *)bp->io)) {
            WaitIO((struct IORequest *)bp->io);
            return;
        }
    }
}

/* TurboPrint: rows y0..y0+h-1 of the page into the RGB24 band; FALSE
 * when stopped meanwhile                                              */
static BOOL tp_fill(struct BitmapPrint *bp, struct Progress *pr, LONG printed, LONG y0, LONG h)
{
    struct DRPSourceMsg m;
    UBYTE *d = bp->tpbuf;
    LONG r, x;

    for (r = 0; r < h; r++) {
        m.x = 0;
        m.y = y0 + r;
        m.width = bp->dw;
        m.height = 1;
        m.buf = bp->tprow;
        print_source(&bp->hook, NULL, &m);
        for (x = 0; x < bp->dw; x++) {
            ULONG v = bp->tprow[x];
            *d++ = (UBYTE)(v >> 16);
            *d++ = (UBYTE)(v >> 8);
            *d++ = (UBYTE)v;
        }
        if (!(r & 31)) {
            progress_set(pr, NULL, printed * 100 + (y0 + r) * 100 / bp->dh);
            if (progress_input(pr, TRUE) || pr->stopped) return FALSE;
        }
    }
    return TRUE;
}

/* TurboPrint: the band as a 24 bit RastPort; all bands of a page but
 * the last without form feed                                         */
static void tp_send(struct BitmapPrint *bp, LONG h, BOOL more)
{
    struct IODRPTagsReq *io = bp->io;

    InitBitMap(&bp->tpbm, 1, bp->dw, h);
    bp->tpbm.BytesPerRow = bp->dw * 3;
    bp->tpbm.Rows = h;
    bp->tpbm.Planes[0] = bp->tpbuf;
    InitRastPort(&bp->tprp);
    bp->tprp.BitMap = &bp->tpbm;
    bp->ext.PixAspX = 1;
    bp->ext.PixAspY = 1;
    bp->ext.Mode = TPFMT_RGB24;
    io->io_Command = PRD_TPEXTDUMPRPORT;
    io->io_RastPort = &bp->tprp;
    io->io_ColorMap = NULL;
    io->io_Modes = (ULONG)&bp->ext;
    io->io_SrcX = 0;
    io->io_SrcY = 0;
    io->io_SrcWidth = bp->dw;
    io->io_SrcHeight = h;
    io->io_DestCols = bp->dw;
    io->io_DestRows = h;
    io->io_Special = (bp->density << 8) | (more ? SPECIAL_NOFORMFEED : 0);
    io->io_TagList = NULL;
    SendIO((struct IORequest *)io);
}

/* The pages one after the other with the progress window: the page being
 * printed, a gauge over all pages and copies, Stop. Returns the pages
 * printed, -1 when stopped, or the negative printer error - 1.        */
static LONG print_pages(struct BitmapPrint *bp, LONG first, LONG last, LONG copies)
{
    struct Progress pr;
    LONG page = first, copy = 0, printed = 0, total = (last - first + 1) * copies, err = 0, stripbit, y;
    char buf[100];

    snprintf(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)last, (long)last);
    progress_open(&pr, buf, total * 100);
    /* the source hook signals each strip: the gauge moves within a page,
     * in 1/100 page                                                    */
    bp->src.task = FindTask(NULL);
    if ((stripbit = AllocSignal(-1)) >= 0) bp->src.sigmask = 1UL << stripbit;

    for (;;) {
        snprintf(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)page, (long)last);
        progress_set(&pr, buf, printed * 100);
        if (!bp->tp) {
            print_send(bp, page);
            wait_io(bp, &pr, printed);
            err = bp->io->io_Error;
        } else {
            bp->src.page = page;
            bp->src.cached = -1;
            for (y = 0; y < bp->dh && !pr.stopped && !err; y += bp->band) {
                LONG h = bp->dh - y < bp->band ? bp->dh - y : bp->band;
                if (!tp_fill(bp, &pr, printed, y, h)) break;
                tp_send(bp, h, y + h < bp->dh);
                wait_io(bp, &pr, printed);
                err = bp->io->io_Error;
            }
        }
        if (pr.stopped || err) break;
        printed++;
        if (++page > last) {
            page = first;
            if (++copy >= copies) break;
        }
    }

    bp->src.sigmask = 0;
    if (stripbit >= 0) FreeSignal(stripbit);
    progress_close(&pr);
    if (pr.stopped || err == PDERR_CANCEL) return -1;
    if (err) return -err - 1;
    return printed;
}

/* Sets a density: in io_Special as documented, and in printer.device's
 * copy of the printer settings, which some drivers (AmiAirPrint) use
 * instead. Returns the density of the settings before.               */
static LONG set_density(struct IODRPTagsReq *io, LONG density)
{
    struct Preferences *pr = &((struct PrinterData *)io->io_Device)->pd_Preferences;
    LONG old = pr->PrintDensity;
    pr->PrintDensity = (UBYTE)density;
    return old;
}

#define QUERYPLANE 4096

static BOOL is_turboprint(struct IODRPTagsReq *io)
{
    struct PrinterData *pd = (struct PrinterData *)io->io_Device;
    return ((ULONG *)pd->pd_OldStk)[2] == TPMATCHWORD && io->io_Device->dd_Library.lib_Version >= 39;
}

/* The resolution of a density: a dump with SPECIAL_NOPRINT sets the
 * density and updates XDotsInch/YDotsInch of the driver, without
 * printing (the printer settings are restored afterwards). The classic
 * PRD_DUMPRPORT with a small empty bitmap, which every printer.device
 * knows (TurboPrint's too). FALSE if it fails.                         */
static BOOL density_dpi(struct IODRPTagsReq *io, LONG density, LONG *x, LONG *y)
{
    struct PrinterExtendedData *ped = &((struct PrinterData *)io->io_Device)->pd_SegmentData->ps_PED;
    struct RastPort rp;
    struct BitMap bm;
    struct TPExtIODRP ext;
    PLANEPTR plane;
    LONG old, err;
    BOOL tp = is_turboprint(io);

    *x = *y = 0;
    /* 32 (RGB24: 768) bytes would do; the reserve is for printer.devices
     * that take more than the bitmap says: it must not hit other memory */
    if (!(plane = AllocVec(QUERYPLANE, (tp ? MEMF_ANY : MEMF_CHIP) | MEMF_CLEAR))) return FALSE;
    InitBitMap(&bm, 1, 16, 16);
    bm.Planes[0] = plane;
    InitRastPort(&rp);
    rp.BitMap = &bm;
    if (tp) {
        /* TurboPrint: its own command with RGB24 and no ColorMap */
        bm.BytesPerRow = 16 * 3;
        ext.PixAspX = ext.PixAspY = 1;
        ext.Mode = TPFMT_RGB24;
        io->io_Command = PRD_TPEXTDUMPRPORT;
        io->io_ColorMap = NULL;
        io->io_Modes = (ULONG)&ext;
    } else {
        io->io_Command = PRD_DUMPRPORT;
        io->io_ColorMap = gui.screen->ViewPort.ColorMap;
        io->io_Modes = 0;
    }
    io->io_RastPort = &rp;
    io->io_SrcX = io->io_SrcY = 0;
    io->io_SrcWidth = io->io_SrcHeight = 16;
    io->io_DestCols = io->io_DestRows = 1000;
    io->io_Special = SPECIAL_MILCOLS | SPECIAL_MILROWS | SPECIAL_NOPRINT | (density << 8);
    io->io_TagList = NULL;
    old = set_density(io, density);
    err = DoIO((struct IORequest *)io);
    set_density(io, old);
    FreeVec(plane);
    if (err || !ped->ped_XDotsInch || !ped->ped_YDotsInch) return FALSE;
    *x = ped->ped_XDotsInch;
    *y = ped->ped_YDotsInch;
    return TRUE;
}

/* density of the printer settings and the resolutions of 1-7 */
static void query_unit(LONG unit, struct UnitInfo *ui)
{
    struct MsgPort *port;
    struct IODRPTagsReq *io = NULL;
    LONG d;

    memset(ui, 0, sizeof(*ui));
    if (!(port = CreateMsgPort())) return;
    if ((io = (struct IODRPTagsReq *)CreateIORequest(port, sizeof(*io))) &&
        !OpenDevice((STRPTR)"printer.device", unit, (struct IORequest *)io, 0)) {
        ui->ok = TRUE;
        ui->prefdensity = ((struct PrinterData *)io->io_Device)->pd_Preferences.PrintDensity;
        for (d = 1; d <= 7; d++) density_dpi(io, d, &ui->dpi[d][0], &ui->dpi[d][1]);
        CloseDevice((struct IORequest *)io);
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
}

static void print_bitmap(const struct Settings *s, const struct PrintJob *job)
{
    /* the paper in 1/10 mm: the sizes in points are rounded, A4 would
     * miss a column at 300 dpi                                        */
    static const LONG tenthmm[NUMPAPERS][2] = { { 2100, 2970 }, { 1480, 2100 }, { 2159, 2794 }, { 2159, 3556 } };
    struct BitmapPrint bp;
    struct PrinterExtendedData *ped;
    LONG pages = 0, first, last, n, xdpi, ydpi, dpi;
    char buf[100];
    struct TagItem tags[] = {
        { HTMLEX_DPI, 0 }, { HTMLEX_PaperWidth, 0 }, { HTMLEX_PaperHeight, 0 },
        { HTMLEX_MarginLeft, 0 }, { HTMLEX_MarginTop, 0 }, { HTMLEX_MarginRight, 0 }, { HTMLEX_MarginBottom, 0 },
        { HTMLEX_FontSize, 100 }, { HTMLEX_Backgrounds, 0 }, { HTMLEX_Footer, 0 },
        { HTMLEX_Pages, 0 }, { HTMLEX_PageWidth, 0 }, { HTMLEX_PageHeight, 0 }, { TAG_DONE, 0 }
    };

    memset(&bp, 0, sizeof(bp));
    if (!FuelGaugeBase && !(FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 44))) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_CLASS_MISSING), "gadgets/fuelgauge.gadget");
        return;
    }
    bp.paper_w = paper_sizes[s->paper][0];
    bp.paper_h = paper_sizes[s->paper][1];
    if (!(bp.port = CreateMsgPort()) || !(bp.io = (struct IODRPTagsReq *)CreateIORequest(bp.port, sizeof(*bp.io)))) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_NOMEM_CONVERT));
        goto out;
    }
    if (OpenDevice((STRPTR)"printer.device", s->prunit, (struct IORequest *)bp.io, 0)) {
        DeleteIORequest((struct IORequest *)bp.io);
        bp.io = NULL;
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_PR_NODEVICE));
        goto out;
    }
    bp.opened = TRUE;
    bp.olddensity = ((struct PrinterData *)bp.io->io_Device)->pd_Preferences.PrintDensity;

    /* the density chosen or that of the printer settings, and the sheet
     * in the driver's dots at it; the gadget renders not finer than
     * MAXRENDERDPI                                                     */
    bp.density = s->prdensity >= 1 && s->prdensity <= 7 ? s->prdensity :
                 ((struct PrinterData *)bp.io->io_Device)->pd_Preferences.PrintDensity;
    if (bp.density < 1 || bp.density > 7) bp.density = 1;
    ped = &((struct PrinterData *)bp.io->io_Device)->pd_SegmentData->ps_PED;
    if (!density_dpi(bp.io, bp.density, &xdpi, &ydpi)) {
        xdpi = ped->ped_XDotsInch ? ped->ped_XDotsInch : 300;
        ydpi = ped->ped_YDotsInch ? ped->ped_YDotsInch : xdpi;
    }
    /* for the drivers that read the density from the settings */
    bp.olddensity = set_density(bp.io, bp.density);
    bp.dw = (tenthmm[s->paper][0] * xdpi + 127) / 254;
    bp.dh = (tenthmm[s->paper][1] * ydpi + 127) / 254;
    if (ped->ped_MaxXDots && bp.dw > (LONG)ped->ped_MaxXDots) bp.dw = ped->ped_MaxXDots;
    if (ped->ped_MaxYDots && bp.dh > (LONG)ped->ped_MaxYDots) bp.dh = ped->ped_MaxYDots;
    dpi = xdpi < MAXRENDERDPI ? xdpi : MAXRENDERDPI;

    tags[0].ti_Data = dpi;
    tags[1].ti_Data = bp.paper_w;
    tags[2].ti_Data = bp.paper_h;
    tags[3].ti_Data = mm_pt(s->margins[0]);
    tags[4].ti_Data = mm_pt(s->margins[1]);
    tags[5].ti_Data = mm_pt(s->margins[2]);
    tags[6].ti_Data = mm_pt(s->margins[3]);
    /* FontSize stays 10 pt: the fonts of the preview are printed */
    tags[8].ti_Data = s->prbackgrounds;
    tags[9].ti_Data = s->prpagenumbers ? (ULONG)S(MSG_PR_FOOTER) : 0;
    tags[10].ti_Data = (ULONG)&pages;
    tags[11].ti_Data = (ULONG)&bp.sw;
    tags[12].ti_Data = (ULONG)&bp.sh;
    gui_busy(TRUE);
    n = (LONG)DoMethod(gui.html, HTMLM_PrintBegin, (ULONG)tags);
    gui_busy(FALSE);
    if (n <= 0 || bp.sw <= 0 || bp.sh <= 0) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_PR_NOPAGES));
        goto out;
    }
    first = job->first > 0 ? job->first : 1;
    last = job->last > 0 && job->last < pages ? job->last : pages;
    if (first > last) goto out;
    if (!(bp.src.rowbuf = AllocVec(bp.sw * 4, MEMF_ANY))) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_NOMEM_CONVERT));
        goto out;
    }
    bp.src.sw = bp.sw;
    bp.src.sh = bp.sh;
    bp.src.dw = bp.dw;
    bp.src.dh = bp.dh;
    /* TurboPrint: the page as RGB24, at once if the memory allows (1 MB
     * stays free), else in bands of about 1 MB; PRINT_MAXMEM (KB) sets
     * the limit instead                                                 */
    if ((bp.tp = is_turboprint(bp.io))) {
        ULONG rowbytes = bp.dw * 3, full = rowbytes * bp.dh, avail = AvailMem(MEMF_ANY | MEMF_LARGEST);
        ULONG limit = s->prmaxmem > 0 ? (ULONG)s->prmaxmem * 1024 : avail > 1024 * 1024 ? avail - 1024 * 1024 : 0;
        if (s->prmaxmem > 0 && limit > avail) limit = avail;
        bp.band = full <= limit ? bp.dh : (LONG)((s->prmaxmem > 0 ? limit : 1024 * 1024) / rowbytes);
        if (bp.band < 16) bp.band = 16;
        if (bp.band > bp.dh) bp.band = bp.dh;
        if (!(bp.tprow = AllocVec(bp.dw * 4, MEMF_ANY)) || !(bp.tpbuf = AllocVec(rowbytes * bp.band, MEMF_ANY))) {
            request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_NOMEM_CONVERT));
            goto out;
        }
    }
    InitRastPort(&bp.rp);
    bp.cmap = gui.screen->ViewPort.ColorMap;
    bp.hook.h_Entry = (ULONG (*)())print_source;
    bp.hook.h_Data = &bp.src;
    bp.src.gadget = gui.html;
    bp.drtags[0].ti_Tag = DRPA_SourceHook;
    bp.drtags[0].ti_Data = (ULONG)&bp.hook;
    bp.drtags[1].ti_Tag = DRPA_AspectX;
    bp.drtags[1].ti_Data = 1;
    bp.drtags[2].ti_Tag = DRPA_AspectY;
    bp.drtags[2].ti_Data = 1;
    bp.drtags[3].ti_Tag = TAG_DONE;

    n = print_pages(&bp, first, last, job->copies > 0 ? job->copies : 1);
    if (n == -1) gui_status((CONST_STRPTR)S(MSG_PR_STOPPED));
    else if (n < 0) {
        gui_status((CONST_STRPTR)"");
        snprintf(buf, sizeof(buf), S(MSG_PR_ERROR), (long)(-n - 1));
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)"%s", buf);
    } else {
        snprintf(buf, sizeof(buf), S(MSG_PR_PRINTED), (long)n);
        gui_status((CONST_STRPTR)buf);
    }
out:
    if (bp.opened) {
        set_density(bp.io, bp.olddensity);
        CloseDevice((struct IORequest *)bp.io);
    }
    if (bp.io) DeleteIORequest((struct IORequest *)bp.io);
    if (bp.src.rowbuf) FreeVec(bp.src.rowbuf);
    if (bp.tpbuf) FreeVec(bp.tpbuf);
    if (bp.tprow) FreeVec(bp.tprow);
    if (bp.port) DeleteMsgPort(bp.port);
    DoMethod(gui.html, HTMLM_PrintEnd);
    CloseLibrary(FuelGaugeBase);
    FuelGaugeBase = NULL;
}

/* HTMLM_Export calls it before each page, on our task but on the
 * gadget's stack: only the progress window is served (see
 * progress_input()). Non-zero stops the export.                       */
static ULONG export_progress(struct Hook *h __asm("a0"), Object *o __asm("a2"),
                             struct HTMLExportProgress *m __asm("a1"))
{
    struct Progress *p = h->h_Data;
    LONG last = p->last > 0 && p->last < m->Pages ? p->last : m->Pages;
    char buf[100];

    if (p->win && last - p->first + 1 != p->max && last >= p->first) {
        p->max = last - p->first + 1;
        SetGadgetAttrs((struct Gadget *)p->gauge, p->win, NULL, FUELGAUGE_Max, p->max, TAG_DONE);
    }
    snprintf(buf, sizeof(buf), S(MSG_PR_WRITING), (long)m->Page, (long)last);
    progress_set(p, buf, m->Page - p->first);
    progress_input(p, FALSE);
    return p->stopped;
}

static void export_ps_pdf(const struct Settings *s, const char *file, const struct PrintJob *job)
{
    const char *name;
    LONG n, pages = 0;
    BPTR fh;
    char buf[100];
    struct Hook hook;
    struct Progress pr;
    BOOL tofile = s->prmode == PRMODE_PDF || s->prdest == PRDEST_FILE;
    struct TagItem tags[] = {
        { HTMLEX_File, 0 }, { HTMLEX_Format, 0 }, { HTMLEX_PaperWidth, 0 }, { HTMLEX_PaperHeight, 0 },
        { HTMLEX_MarginLeft, 0 }, { HTMLEX_MarginTop, 0 }, { HTMLEX_MarginRight, 0 }, { HTMLEX_MarginBottom, 0 },
        { HTMLEX_FontSize, 0 }, { HTMLEX_Serif, 0 }, { HTMLEX_Backgrounds, 0 }, { HTMLEX_Footer, 0 },
        { HTMLEX_FirstPage, 0 }, { HTMLEX_LastPage, 0 }, { HTMLEX_ProgressHook, 0 }, { HTMLEX_Pages, 0 },
        { HTMLEX_PSLevel, 2 }, { TAG_DONE, 0 }
    };

    if (tofile) {
        name = file;
        if (!name[0]) return;
        if (!confirm_overwrite((CONST_STRPTR)name)) return;
    } else if (s->prdest == PRDEST_PRT) name = "PRT:";
    else if (s->prdest == PRDEST_PS) {
        if (!have_ps()) {
            request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_PR_NOPS));
            return;
        }
        name = "PS:";
    } else name = s->prdevice[0] ? s->prdevice : "PAR:";

    if (!(fh = Open((STRPTR)name, MODE_NEWFILE))) {
        dos_error((CONST_STRPTR)S(MSG_PR_CANNOT_OPEN), (CONST_STRPTR)name);
        return;
    }
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (ULONG (*)())export_progress;
    hook.h_Data = &pr;
    tags[0].ti_Data = (ULONG)fh;
    tags[1].ti_Data = s->prmode == PRMODE_PDF ? HTMLEXF_PDF : HTMLEXF_PS;
    tags[2].ti_Data = paper_sizes[s->paper][0];
    tags[3].ti_Data = paper_sizes[s->paper][1];
    tags[4].ti_Data = mm_pt(s->margins[0]);
    tags[5].ti_Data = mm_pt(s->margins[1]);
    tags[6].ti_Data = mm_pt(s->margins[2]);
    tags[7].ti_Data = mm_pt(s->margins[3]);
    tags[8].ti_Data = s->prsize * 10;
    tags[9].ti_Data = s->prserif;
    tags[10].ti_Data = s->prbackgrounds;
    tags[11].ti_Data = s->prpagenumbers ? (ULONG)S(MSG_PR_FOOTER) : 0;
    tags[12].ti_Data = job->first > 0 ? job->first : 1;
    tags[13].ti_Data = job->last;
    tags[14].ti_Data = (ULONG)&hook;
    tags[15].ti_Data = (ULONG)&pages;
    tags[16].ti_Data = s->pslevel == 1 ? 1 : 2;
    /* the progress window, without fuelgauge.gadget only the status bar */
    snprintf(buf, sizeof(buf), S(MSG_PR_WRITING), 999L, 999L);
    if (!FuelGaugeBase) FuelGaugeBase = OpenLibrary((STRPTR)"gadgets/fuelgauge.gadget", 44);
    if (FuelGaugeBase) progress_open(&pr, buf, 1);
    else {
        memset(&pr, 0, sizeof(pr));
        gui_busy(TRUE);
    }
    pr.first = job->first > 0 ? job->first : 1;
    pr.last = job->last;
    n = (LONG)DoMethod(gui.html, HTMLM_Export, (ULONG)tags);
    if (FuelGaugeBase) {
        progress_close(&pr);
        CloseLibrary(FuelGaugeBase);
        FuelGaugeBase = NULL;
    } else gui_busy(FALSE);
    if (n == -2) {                  /* stopped: no half file */
        Close(fh);
        if (tofile) DeleteFile((STRPTR)name);
        gui_status((CONST_STRPTR)S(MSG_PR_STOPPED));
        return;
    }
    if (n < 0) {
        LONG err = IoErr();
        Close(fh);
        SetIoErr(err);
        dos_error((CONST_STRPTR)S(MSG_WRITE_FAILED), (CONST_STRPTR)name);
        gui_status((CONST_STRPTR)"");
        return;
    }
    if (!Close(fh)) {
        dos_error((CONST_STRPTR)S(MSG_WRITE_FAILED), (CONST_STRPTR)name);
        return;
    }
    snprintf(buf, sizeof(buf), S(MSG_PR_WRITTEN), (long)n, (const char *)name);
    gui_status((CONST_STRPTR)buf);
}

void print_run(const struct Settings *s, const char *file, const struct PrintJob *job)
{
    if (!gadget_can_print()) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_PR_NEEDGADGET));
        return;
    }
    if (s->prmode == PRMODE_PRINTER) print_bitmap(s, job);
    else export_ps_pdf(s, file, job);
}
