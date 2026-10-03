/*
 * MDEdit - splash window while the program starts
 *
 * Opening the ReAction classes, html.gadget and the toolbar images can
 * take a while. The splash window shows the program icon, name, version
 * and a progress bar meanwhile. It is drawn with Intuition and
 * graphics.library only, so it can open before any class is loaded.
 * Everything is optional: without icon.library V44 there is no icon,
 * without a big font the title uses the screen font in bold.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/diskfont.h>

#include <stdio.h>
#include <string.h>

#include "mdedit.h"

extern struct Library *DiskfontBase;    /* gui.c, closed by gui_close() */

#define MARGIN  14                      /* border to the contents */
#define GAP     12                      /* icon to text, text block to status */
#define BAR_H   6                       /* height of the progress bar */

static struct {
    struct Screen *screen;
    struct Window *win;
    struct DrawInfo *dri;
    struct DiskObject *icon;
    struct TextFont *big;               /* title font, NULL: screen font bold */
    LONG pen_bg, pen_title;             /* obtained pens, -1: none */
    UWORD bg, title, text, shine, shadow, fill;
    WORD iconw, iconh;
    WORD barx, bary, barw;              /* inside of the progress bar */
    WORD statusy;
    ULONG steps, done;
} sp;

static LONG obtain(struct ColorMap *cm, ULONG rgb)
{
    return ObtainBestPen(cm, (rgb >> 16 & 0xff) * 0x01010101UL, (rgb >> 8 & 0xff) * 0x01010101UL,
                         (rgb & 0xff) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}

/* the background: halfway between the window background and white */
static ULONG light_grey(void)
{
    ULONG b[3], s[3];
    struct ColorMap *cm = sp.screen->ViewPort.ColorMap;
    GetRGB32(cm, sp.dri->dri_Pens[BACKGROUNDPEN], 1, b);
    GetRGB32(cm, sp.dri->dri_Pens[SHINEPEN], 1, s);
    return ((b[0] >> 25) + (s[0] >> 25)) << 16 | ((b[1] >> 25) + (s[1] >> 25)) << 8 |
           ((b[2] >> 25) + (s[2] >> 25));
}

/* a designed (not scaled) size of the screen font or Helvetica for
 * the title, the biggest of 24, 18 and 15 pixels                    */
static struct TextFont *title_font(void)
{
    static const UWORD sizes[] = { 24, 18, 15 };
    const char *names[2];
    struct TextAttr ta;
    ULONG n, i;

    if (!DiskfontBase && !(DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39)))
        return NULL;
    names[0] = sp.screen->Font ? (const char *)sp.screen->Font->ta_Name : "helvetica.font";
    names[1] = "helvetica.font";
    for (n = 0; n < 2; n++)
        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            struct TextFont *f;
            ta.ta_Name = (STRPTR)names[n];
            ta.ta_YSize = sizes[i];
            ta.ta_Style = FS_NORMAL;
            ta.ta_Flags = FPF_DESIGNED;
            if ((f = OpenDiskFont(&ta))) {
                if (f->tf_YSize == sizes[i] && (f->tf_Flags & FPF_DESIGNED)) return f;
                CloseFont(f);
            }
        }
    return NULL;
}

static void text_at(struct RastPort *rp, WORD x, WORD y, UWORD pen, CONST_STRPTR s)
{
    SetAPen(rp, pen);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, strlen((const char *)s));
}

static WORD text_width(struct RastPort *rp, CONST_STRPTR s)
{
    return (WORD)TextLength(rp, (STRPTR)s, strlen((const char *)s));
}

/* frame: raised (shine top left) or recessed */
static void frame(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1, BOOL raised)
{
    SetAPen(rp, raised ? sp.shine : sp.shadow);
    Move(rp, x0, y1); Draw(rp, x0, y0); Draw(rp, x1, y0);
    SetAPen(rp, raised ? sp.shadow : sp.shine);
    Move(rp, x1, y0 + 1); Draw(rp, x1, y1); Draw(rp, x0 + 1, y1);
}

static void draw_bar(void)
{
    struct RastPort *rp;
    WORD w;

    if (!sp.win) return;
    rp = sp.win->RPort;
    w = sp.steps ? (WORD)((ULONG)sp.barw * (sp.done < sp.steps ? sp.done : sp.steps) / sp.steps) : 0;
    if (w > 0) {
        SetAPen(rp, sp.fill);
        RectFill(rp, sp.barx, sp.bary, sp.barx + w - 1, sp.bary + BAR_H - 1);
    }
    if (w < sp.barw) {
        SetAPen(rp, sp.bg);
        RectFill(rp, sp.barx + w, sp.bary, sp.barx + sp.barw - 1, sp.bary + BAR_H - 1);
    }
}

void splash_open(CONST_STRPTR iconname, CONST_STRPTR name, CONST_STRPTR version, ULONG steps)
{
    struct RastPort rp;
    struct TextFont *scrfont;
    struct ColorMap *cm;
    char ver[64], copy[64];
    CONST_STRPTR sub = (CONST_STRPTR)S(MSG_SPLASH_SUBTITLE);
    WORD tw, th, w, h, x, y, lineh;
    struct Rectangle rect;

    if (sp.win || !(sp.screen = LockPubScreen(NULL))) return;
    sp.steps = steps;
    sp.done = 0;
    sp.pen_bg = sp.pen_title = -1;
    if (!(sp.dri = GetScreenDrawInfo(sp.screen))) goto fail;
    cm = sp.screen->ViewPort.ColorMap;
    scrfont = sp.dri->dri_Font;

    /* colours: light background, navy title (as headings in the editor) */
    sp.text = sp.dri->dri_Pens[TEXTPEN];
    sp.shine = sp.dri->dri_Pens[SHINEPEN];
    sp.shadow = sp.dri->dri_Pens[SHADOWPEN];
    sp.fill = sp.dri->dri_Pens[FILLPEN];
    sp.bg = sp.dri->dri_Pens[BACKGROUNDPEN];
    if ((sp.pen_bg = obtain(cm, light_grey())) >= 0) sp.bg = (UWORD)sp.pen_bg;
    sp.title = sp.text;
    if ((sp.pen_title = obtain(cm, 0x0a2a8a)) >= 0 && sp.pen_title != sp.bg) sp.title = (UWORD)sp.pen_title;

    /* the program icon, laid out for this screen (icon.library V44) */
    sp.iconw = sp.iconh = 0;
    if (IconBase && IconBase->lib_Version >= 44 && (sp.icon = GetDiskObjectNew((STRPTR)iconname))) {
        LayoutIconA(sp.icon, sp.screen, NULL);
        if (GetIconRectangle(NULL, sp.icon, NULL, &rect,
                             ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE, TAG_DONE)) {
            sp.iconw = rect.MaxX - rect.MinX + 1;
            sp.iconh = rect.MaxY - rect.MinY + 1;
        }
    }
    sp.big = title_font();

    /* measure */
    snprintf(ver, sizeof(ver), S(MSG_SPLASH_VERSION), (const char *)version);
    snprintf(copy, sizeof(copy), "© 2026 André Gewert");
    InitRastPort(&rp);
    lineh = scrfont->tf_YSize + 2;
    SetFont(&rp, scrfont);
    tw = text_width(&rp, sub);
    if (text_width(&rp, (CONST_STRPTR)ver) > tw) tw = text_width(&rp, (CONST_STRPTR)ver);
    if (text_width(&rp, (CONST_STRPTR)copy) > tw) tw = text_width(&rp, (CONST_STRPTR)copy);
    if (sp.big) SetFont(&rp, sp.big);
    else SetSoftStyle(&rp, FSF_BOLD, FSF_BOLD);
    if (text_width(&rp, name) > tw) tw = text_width(&rp, name);
    th = (sp.big ? sp.big->tf_YSize : scrfont->tf_YSize) + 4 + 3 * lineh;
    if (tw < 180) tw = 180;

    w = MARGIN + (sp.iconw ? sp.iconw + GAP : 0) + tw + MARGIN;
    h = MARGIN + (th > sp.iconh ? th : sp.iconh) + GAP + lineh + 4 + BAR_H + 2 + MARGIN;
    if (!(sp.win = OpenWindowTags(NULL,
            WA_PubScreen,   (ULONG)sp.screen,
            WA_Left,        (sp.screen->Width - w) / 2,
            WA_Top,         (sp.screen->Height - h) / 2,
            WA_Width,       w,
            WA_Height,      h,
            WA_Borderless,  TRUE,
            WA_SmartRefresh, TRUE,
            WA_RMBTrap,     TRUE,
            WA_Activate,    FALSE,
            TAG_DONE)))
        goto fail;

    /* draw */
    {
        struct RastPort *r = sp.win->RPort;
        WORD tx = MARGIN + (sp.iconw ? sp.iconw + GAP : 0);
        WORD block = th > sp.iconh ? th : sp.iconh;

        SetAPen(r, sp.bg);
        RectFill(r, 0, 0, w - 1, h - 1);
        frame(r, 0, 0, w - 1, h - 1, TRUE);
        frame(r, 2, 2, w - 3, h - 3, FALSE);
        SetDrMd(r, JAM1);

        if (sp.iconw)
            DrawIconState(r, sp.icon, NULL, MARGIN, MARGIN + (block - sp.iconh) / 2, IDS_NORMAL,
                          ICONDRAWA_DrawInfo, (ULONG)sp.dri, ICONDRAWA_Frameless, TRUE,
                          ICONDRAWA_Borderless, TRUE, ICONDRAWA_EraseBackground, FALSE, TAG_DONE);

        y = MARGIN + (block - th) / 2;
        if (sp.big) SetFont(r, sp.big);
        else {
            SetFont(r, scrfont);
            SetSoftStyle(r, FSF_BOLD, FSF_BOLD);
        }
        text_at(r, tx, y, sp.title, name);
        y += (sp.big ? sp.big->tf_YSize : scrfont->tf_YSize) + 4;
        SetFont(r, scrfont);
        SetSoftStyle(r, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        text_at(r, tx, y, sp.text, sub);
        y += lineh;
        text_at(r, tx, y, sp.text, (CONST_STRPTR)ver);
        y += lineh;
        text_at(r, tx, y, sp.text, (CONST_STRPTR)copy);

        /* status line and progress bar at the bottom */
        sp.statusy = MARGIN + block + GAP;
        x = MARGIN;
        sp.barx = x + 1;
        sp.bary = sp.statusy + lineh + 4 + 1;
        sp.barw = w - 2 * MARGIN - 2;
        frame(r, x, sp.bary - 1, x + sp.barw + 1, sp.bary + BAR_H, FALSE);
        draw_bar();
    }
    return;

fail:
    splash_close();
}

/* the current step, below the texts */
void splash_status(CONST_STRPTR text)
{
    struct RastPort *r;
    if (!sp.win) return;
    r = sp.win->RPort;
    SetAPen(r, sp.bg);
    RectFill(r, MARGIN, sp.statusy, sp.win->Width - MARGIN - 1, sp.statusy + r->TxHeight + 1);
    text_at(r, MARGIN, sp.statusy, sp.text, text);
}

void splash_step(void)
{
    if (!sp.win) return;
    sp.done++;
    draw_bar();
}

void splash_close(void)
{
    struct ColorMap *cm = sp.screen ? sp.screen->ViewPort.ColorMap : NULL;

    if (sp.win) CloseWindow(sp.win);
    if (sp.icon) FreeDiskObject(sp.icon);
    if (sp.big) CloseFont(sp.big);
    if (cm && sp.pen_bg >= 0) ReleasePen(cm, sp.pen_bg);
    if (cm && sp.pen_title >= 0) ReleasePen(cm, sp.pen_title);
    if (sp.dri) FreeScreenDrawInfo(sp.screen, sp.dri);
    if (sp.screen) UnlockPubScreen(NULL, sp.screen);
    memset(&sp, 0, sizeof(sp));
}
