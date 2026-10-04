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
    PW_MODE = 400, PW_DEST, PW_FILE, PW_DEVICE, PW_DPI, PW_FROM, PW_TO, PW_COPIES,
    PW_PAPER, PW_ML, PW_MT, PW_MR, PW_MB, PW_SERIF, PW_SIZE, PW_PAGENUMBERS, PW_BACKGROUNDS,
    PW_PRINT, PW_CANCEL, PW_STOP
};

static const LONG dpis[] = { 150, 300, 600 };
#define NUMDPIS 3
static STRPTR dpi_labels[] = { (STRPTR)"150 dpi", (STRPTR)"300 dpi", (STRPTR)"600 dpi", NULL };
static STRPTR paper_labels[] = { (STRPTR)"A4", (STRPTR)"A5", (STRPTR)"Letter", (STRPTR)"Legal", NULL };
/* the entries of the mode chooser: PostScript in two levels */
enum { MI_PRINTER, MI_PS2, MI_PS1, MI_PDF, NUMMI };
static STRPTR mode_labels[NUMMI + 1], font_labels[3];

static struct {
    Object *winobj, *root;
    Object *mode, *dest, *file, *device, *dpi, *from, *to, *copies;
    Object *paper, *margin[4], *serif, *size, *pagenumbers, *backgrounds, *print;
    struct Window *win;
    struct List destlist;
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
    SetGadgetAttrs((struct Gadget *)pw.dpi, w, NULL, GA_Disabled, !printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.copies, w, NULL, GA_Disabled, !printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.serif, w, NULL, GA_Disabled, printer, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)pw.size, w, NULL, GA_Disabled, printer, TAG_DONE);
}

static Object *build(const struct Settings *s, const char *file, LONG from, LONG to)
{
    struct Node *n;
    LONG i, d;
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

    for (d = 0; d < NUMDPIS - 1 && dpis[d] < s->prdpi; d++) ;
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
    pw.dpi = chooser(PW_DPI, dpi_labels, d);
    pw.from = integer(PW_FROM, from, 1, 9999, 4);
    pw.to = integer(PW_TO, to, 0, 9999, 4);
    pw.copies = integer(PW_COPIES, 1, 1, 99, 2);
    output = NewObject(LAYOUT_GetClass(), NULL, PAGE_GROUP(S(MSG_PR_OUTPUT)),
        LAYOUT_AddChild, (ULONG)pw.mode,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_MODE)),
        LAYOUT_AddChild, (ULONG)pw.dest,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEST)),
        LAYOUT_AddChild, (ULONG)pw.file,   FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_FILE)),
        LAYOUT_AddChild, (ULONG)pw.device, FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DEVICE)),
        LAYOUT_AddChild, (ULONG)pw.dpi,    FIXED, CHILD_Label, (ULONG)label(S(MSG_PR_DPI)),
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
    LONG i, d = get(pw.dpi, CHOOSER_Selected);

    s->prmode = mode_of(get(pw.mode, CHOOSER_Selected));
    if (s->prmode == PRMODE_PS) s->pslevel = get(pw.mode, CHOOSER_Selected) == MI_PS1 ? 1 : 2;
    s->prdest = get(pw.dest, CHOOSER_Selected);
    s->prdevice[0] = 0;
    if ((str = (STRPTR)get(pw.device, STRINGA_TextVal))) strncat(s->prdevice, (const char *)str, sizeof(s->prdevice) - 1);
    s->prdpi = dpis[d >= 0 && d < NUMDPIS ? d : 1];
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
    if (pw.destlist.lh_Head) {
        struct Node *n, *next;
        for (n = pw.destlist.lh_Head; (next = n->ln_Succ); n = next) FreeChooserNode(n);
    }
    memset(&pw, 0, sizeof(pw));
    return rc;
}

/*****************************************************************************/
/* running                                                                   */

static BOOL gadget_can_print(void)
{
    return HTMLBase && (HTMLBase->lib_Version > 1 || (HTMLBase->lib_Version == 1 && HTMLBase->lib_Revision >= 2));
}

static ULONG export_progress(struct Hook *h __asm("a0"), Object *o __asm("a2"),
                             struct HTMLExportProgress *p __asm("a1"))
{
    char buf[80];
    snprintf(buf, sizeof(buf), S(MSG_PR_WRITING), (long)p->Page, (long)p->Pages);
    gui_status((CONST_STRPTR)buf);
    return 0;
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
    BOOL                 opened;
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
    io->io_Special = 0;
    io->io_TagList = bp->drtags;
    SendIO((struct IORequest *)io);
}

/* The pages one after the other with a modal progress window: the page
 * being printed, a gauge over all pages and copies, Stop (also the close
 * gadget and Esc). Returns the pages printed, -1 when stopped, or the
 * negative printer error - 1.                                         */
static LONG print_pages(struct BitmapPrint *bp, LONG first, LONG last, LONG copies)
{
    Object *winobj, *text, *gauge, *stop;
    struct Window *win = NULL;
    ULONG sig = 0, mainsig = 0, portsig = 1UL << bp->port->mp_SigBit, result;
    UWORD code;
    LONG page = first, copy = 0, printed = 0, total = (last - first + 1) * copies, err = 0;
    LONG level, shown = -1, stripbit;
    BOOL stopped = FALSE, done = FALSE;
    char buf[100];

    snprintf(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)last, (long)last);
    text = NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly, TRUE, GA_Text, (ULONG)buf, BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Justification, BCJ_LEFT, TAG_DONE);
    gauge = NewObject(FUELGAUGE_GetClass(), NULL,
        FUELGAUGE_Min, 0, FUELGAUGE_Max, total * 100, FUELGAUGE_Level, 0,
        FUELGAUGE_Percent, TRUE, FUELGAUGE_Ticks, 0, TAG_DONE);
    stop = button(PW_STOP, S(MSG_PR_STOP));
    winobj = NewObject(WINDOW_GetClass(), NULL,
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
            LAYOUT_AddChild,     (ULONG)text,
            LAYOUT_AddChild,     (ULONG)gauge,
            CHILD_MinWidth,      240,
            LAYOUT_AddChild,     (ULONG)NewObject(LAYOUT_GetClass(), NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild,    (ULONG)filler(),
                LAYOUT_AddChild,    (ULONG)stop,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,    (ULONG)filler(),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);
    if (winobj) win = (struct Window *)DoMethod(winobj, WM_OPEN);
    if (win) GetAttr(WINDOW_SigMask, winobj, &sig);
    GetAttr(WINDOW_SigMask, gui.winobj, &mainsig);
    gui_busy(TRUE);
    /* the source hook signals each strip: the gauge moves within a page,
     * in 1/100 page                                                    */
    bp->src.task = FindTask(NULL);
    if ((stripbit = AllocSignal(-1)) >= 0) bp->src.sigmask = 1UL << stripbit;

    for (;;) {
        snprintf(buf, sizeof(buf), S(MSG_PR_PRINTING), (long)page, (long)last);
        gui_status((CONST_STRPTR)buf);
        if (win) {
            SetGadgetAttrs((struct Gadget *)text, win, NULL, GA_Text, (ULONG)buf, TAG_DONE);
            if ((level = printed * 100) != shown)
                SetGadgetAttrs((struct Gadget *)gauge, win, NULL, FUELGAUGE_Level, shown = level, TAG_DONE);
        }
        print_send(bp, page);
        done = FALSE;
        while (!done) {
            ULONG got = Wait(sig | mainsig | portsig | bp->src.sigmask);

            if ((got & bp->src.sigmask) && win && bp->dh > 0) {
                LONG row = bp->src.row;
                if (row > bp->dh) row = bp->dh;
                if ((level = printed * 100 + row * 100 / bp->dh) != shown)
                    SetGadgetAttrs((struct Gadget *)gauge, win, NULL, FUELGAUGE_Level, shown = level, TAG_DONE);
            }

            /* the main window waits: its input is dropped */
            if (got & mainsig)
                while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
            if (win)
                while ((result = DoMethod(winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
                    switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW: break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) == PW_STOP) break;
                        continue;
                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == 0x45) break;     /* Esc */
                        continue;
                    default:
                        continue;
                    }
                    if (!stopped) {
                        stopped = TRUE;
                        AbortIO((struct IORequest *)bp->io);
                        SetGadgetAttrs((struct Gadget *)stop, win, NULL, GA_Disabled, TRUE, TAG_DONE);
                    }
                }
            if (CheckIO((struct IORequest *)bp->io)) {
                WaitIO((struct IORequest *)bp->io);
                done = TRUE;
            }
        }
        if (stopped || (err = bp->io->io_Error)) break;
        printed++;
        if (++page > last) {
            page = first;
            if (++copy >= copies) break;
        }
    }

    bp->src.sigmask = 0;
    if (stripbit >= 0) FreeSignal(stripbit);
    gui_busy(FALSE);
    if (winobj) DisposeObject(winobj);
    if (stopped || err == PDERR_CANCEL) return -1;
    if (err) return -err - 1;
    return printed;
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
    if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)bp.io, 0)) {
        DeleteIORequest((struct IORequest *)bp.io);
        bp.io = NULL;
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_PR_NODEVICE));
        goto out;
    }
    bp.opened = TRUE;

    /* the sheet in the driver's dots; the gadget renders at the chosen
     * quality, but not finer than the printer                          */
    ped = &((struct PrinterData *)bp.io->io_Device)->pd_SegmentData->ps_PED;
    xdpi = ped->ped_XDotsInch ? ped->ped_XDotsInch : s->prdpi;
    ydpi = ped->ped_YDotsInch ? ped->ped_YDotsInch : xdpi;
    bp.dw = (tenthmm[s->paper][0] * xdpi + 127) / 254;
    bp.dh = (tenthmm[s->paper][1] * ydpi + 127) / 254;
    if (ped->ped_MaxXDots && bp.dw > (LONG)ped->ped_MaxXDots) bp.dw = ped->ped_MaxXDots;
    if (ped->ped_MaxYDots && bp.dh > (LONG)ped->ped_MaxYDots) bp.dh = ped->ped_MaxYDots;
    dpi = s->prdpi < xdpi ? s->prdpi : xdpi;

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
    if (bp.opened) CloseDevice((struct IORequest *)bp.io);
    if (bp.io) DeleteIORequest((struct IORequest *)bp.io);
    if (bp.src.rowbuf) FreeVec(bp.src.rowbuf);
    if (bp.port) DeleteMsgPort(bp.port);
    DoMethod(gui.html, HTMLM_PrintEnd);
    CloseLibrary(FuelGaugeBase);
    FuelGaugeBase = NULL;
}

static void export_ps_pdf(const struct Settings *s, const char *file, const struct PrintJob *job)
{
    const char *name;
    LONG n, pages = 0;
    BPTR fh;
    char buf[100];
    struct Hook hook;
    struct TagItem tags[] = {
        { HTMLEX_File, 0 }, { HTMLEX_Format, 0 }, { HTMLEX_PaperWidth, 0 }, { HTMLEX_PaperHeight, 0 },
        { HTMLEX_MarginLeft, 0 }, { HTMLEX_MarginTop, 0 }, { HTMLEX_MarginRight, 0 }, { HTMLEX_MarginBottom, 0 },
        { HTMLEX_FontSize, 0 }, { HTMLEX_Serif, 0 }, { HTMLEX_Backgrounds, 0 }, { HTMLEX_Footer, 0 },
        { HTMLEX_FirstPage, 0 }, { HTMLEX_LastPage, 0 }, { HTMLEX_ProgressHook, 0 }, { HTMLEX_Pages, 0 },
        { HTMLEX_PSLevel, 2 }, { TAG_DONE, 0 }
    };

    if (s->prmode == PRMODE_PDF || s->prdest == PRDEST_FILE) {
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
    gui_busy(TRUE);
    n = (LONG)DoMethod(gui.html, HTMLM_Export, (ULONG)tags);
    gui_busy(FALSE);
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
