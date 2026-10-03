/*
 * Kickdown - splash window while the program starts, and the About window
 *
 * Opening the ReAction classes, html.gadget and the toolbar images can
 * take a while. The splash window shows the program icon, name, version
 * and a progress bar meanwhile. It is drawn with Intuition and
 * graphics.library only, so it can open before any class is loaded.
 * The About window has the same head (icon, title, subtitle, version,
 * copyright) with the details and an OK button below.
 *
 * Everything is optional: without icon.library V44 there is no icon,
 * without a big font the title uses the screen font in bold.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <classes/window.h>
#include <gadgets/button.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/diskfont.h>
#include <proto/button.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <string.h>

#include "kickdown.h"

extern struct Library *DiskfontBase;    /* gui.c, closed by gui_close() */

#define MARGIN  14                      /* border to the contents */
#define GAP     12                      /* icon to text, between blocks */
#define BAR_H   6                       /* height of the progress bar */

/* what the head of both windows needs */
struct Card {
    struct Screen *screen;
    struct DrawInfo *dri;
    struct DiskObject *icon;
    struct TextFont *big;               /* title font, NULL: screen font bold */
    struct TextFont *font;              /* screen font */
    LONG pen_bg, pen_title;             /* obtained pens, -1: none */
    UWORD bg, title, text, shine, shadow, fill;
    WORD iconw, iconh;
    WORD textw, texth;                  /* the text block of the head */
    WORD lineh;
    char version[64], copyright[40];
    CONST_STRPTR name;
};

static LONG obtain(struct ColorMap *cm, ULONG rgb)
{
    return ObtainBestPen(cm, (rgb >> 16 & 0xff) * 0x01010101UL, (rgb >> 8 & 0xff) * 0x01010101UL,
                         (rgb & 0xff) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}

/* the background: halfway between the window background and white */
static ULONG light_grey(struct Card *c)
{
    ULONG b[3], s[3];
    struct ColorMap *cm = c->screen->ViewPort.ColorMap;
    GetRGB32(cm, c->dri->dri_Pens[BACKGROUNDPEN], 1, b);
    GetRGB32(cm, c->dri->dri_Pens[SHINEPEN], 1, s);
    return ((b[0] >> 25) + (s[0] >> 25)) << 16 | ((b[1] >> 25) + (s[1] >> 25)) << 8 |
           ((b[2] >> 25) + (s[2] >> 25));
}

/* a designed (not scaled) size of the screen font or Helvetica for
 * the title, the biggest of 24, 18 and 15 pixels                    */
static struct TextFont *title_font(struct Card *c)
{
    static const UWORD sizes[] = { 24, 18, 15 };
    const char *names[2];
    struct TextAttr ta;
    ULONG n, i;

    if (!DiskfontBase && !(DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39)))
        return NULL;
    names[0] = c->screen->Font ? (const char *)c->screen->Font->ta_Name : "helvetica.font";
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

static void text_at(struct RastPort *rp, WORD x, WORD y, UWORD pen, CONST_STRPTR s, ULONG len)
{
    SetAPen(rp, pen);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, len);
}

static WORD text_width(struct RastPort *rp, CONST_STRPTR s)
{
    return (WORD)TextLength(rp, (STRPTR)s, strlen((const char *)s));
}

/* frame: raised (shine top left) or recessed */
static void frame(struct Card *c, struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1, BOOL raised)
{
    SetAPen(rp, raised ? c->shine : c->shadow);
    Move(rp, x0, y1); Draw(rp, x0, y0); Draw(rp, x1, y0);
    SetAPen(rp, raised ? c->shadow : c->shine);
    Move(rp, x1, y0 + 1); Draw(rp, x1, y1); Draw(rp, x0 + 1, y1);
}

static void title_style(struct Card *c, struct RastPort *rp)
{
    if (c->big) SetFont(rp, c->big);
    else {
        SetFont(rp, c->font);
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
    }
}

static void normal_style(struct Card *c, struct RastPort *rp)
{
    SetFont(rp, c->font);
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

static void card_free(struct Card *c)
{
    struct ColorMap *cm = c->screen ? c->screen->ViewPort.ColorMap : NULL;

    if (c->icon) FreeDiskObject(c->icon);
    if (c->big) CloseFont(c->big);
    if (cm && c->pen_bg >= 0) ReleasePen(cm, c->pen_bg);
    if (cm && c->pen_title >= 0) ReleasePen(cm, c->pen_title);
    if (c->dri) FreeScreenDrawInfo(c->screen, c->dri);
    memset(c, 0, sizeof(*c));
    c->pen_bg = c->pen_title = -1;
}

/* colours, icon and fonts on 'screen'; measures the head */
static BOOL card_init(struct Card *c, struct Screen *screen, CONST_STRPTR iconname,
                      CONST_STRPTR name, CONST_STRPTR version)
{
    struct ColorMap *cm = screen->ViewPort.ColorMap;
    struct Rectangle rect;
    struct RastPort rp;
    CONST_STRPTR sub = (CONST_STRPTR)S(MSG_SPLASH_SUBTITLE);

    memset(c, 0, sizeof(*c));
    c->pen_bg = c->pen_title = -1;
    c->screen = screen;
    c->name = name;
    if (!(c->dri = GetScreenDrawInfo(screen))) return FALSE;
    c->font = c->dri->dri_Font;

    /* colours: light background, navy title (as headings in the editor) */
    c->text = c->dri->dri_Pens[TEXTPEN];
    c->shine = c->dri->dri_Pens[SHINEPEN];
    c->shadow = c->dri->dri_Pens[SHADOWPEN];
    c->fill = c->dri->dri_Pens[FILLPEN];
    c->bg = c->dri->dri_Pens[BACKGROUNDPEN];
    if ((c->pen_bg = obtain(cm, light_grey(c))) >= 0) c->bg = (UWORD)c->pen_bg;
    c->title = c->text;
    if ((c->pen_title = obtain(cm, 0x0a2a8a)) >= 0 && c->pen_title != c->bg) c->title = (UWORD)c->pen_title;

    /* the program icon, laid out for this screen (icon.library V44) */
    if (IconBase && IconBase->lib_Version >= 44 && (c->icon = GetDiskObjectNew((STRPTR)iconname))) {
        LayoutIconA(c->icon, screen, NULL);
        if (GetIconRectangle(NULL, c->icon, NULL, &rect,
                             ICONDRAWA_Frameless, TRUE, ICONDRAWA_Borderless, TRUE, TAG_DONE)) {
            c->iconw = rect.MaxX - rect.MinX + 1;
            c->iconh = rect.MaxY - rect.MinY + 1;
        }
    }
    c->big = title_font(c);

    snprintf(c->version, sizeof(c->version), S(MSG_SPLASH_VERSION), (const char *)version);
    snprintf(c->copyright, sizeof(c->copyright), "© 2026 André Gewert");
    InitRastPort(&rp);
    c->lineh = c->font->tf_YSize + 2;
    normal_style(c, &rp);
    c->textw = text_width(&rp, sub);
    if (text_width(&rp, (CONST_STRPTR)c->version) > c->textw) c->textw = text_width(&rp, (CONST_STRPTR)c->version);
    if (text_width(&rp, (CONST_STRPTR)c->copyright) > c->textw) c->textw = text_width(&rp, (CONST_STRPTR)c->copyright);
    title_style(c, &rp);
    if (text_width(&rp, name) > c->textw) c->textw = text_width(&rp, name);
    c->texth = (c->big ? c->big->tf_YSize : c->font->tf_YSize) + 4 + 3 * c->lineh;
    return TRUE;
}

static WORD head_width(struct Card *c)
{
    return (c->iconw ? c->iconw + GAP : 0) + c->textw;
}

static WORD head_height(struct Card *c)
{
    return c->texth > c->iconh ? c->texth : c->iconh;
}

/* the head at (x, y): icon, title, subtitle, version, copyright */
static void draw_head(struct Card *c, struct RastPort *r, WORD x, WORD y)
{
    CONST_STRPTR sub = (CONST_STRPTR)S(MSG_SPLASH_SUBTITLE);
    WORD tx = x + (c->iconw ? c->iconw + GAP : 0), block = head_height(c);

    SetDrMd(r, JAM1);
    if (c->iconw)
        DrawIconState(r, c->icon, NULL, x, y + (block - c->iconh) / 2, IDS_NORMAL,
                      ICONDRAWA_DrawInfo, (ULONG)c->dri, ICONDRAWA_Frameless, TRUE,
                      ICONDRAWA_Borderless, TRUE, ICONDRAWA_EraseBackground, FALSE, TAG_DONE);
    y += (block - c->texth) / 2;
    title_style(c, r);
    text_at(r, tx, y, c->title, c->name, strlen((const char *)c->name));
    y += (c->big ? c->big->tf_YSize : c->font->tf_YSize) + 4;
    normal_style(c, r);
    text_at(r, tx, y, c->text, sub, strlen((const char *)sub));
    y += c->lineh;
    text_at(r, tx, y, c->text, (CONST_STRPTR)c->version, strlen(c->version));
    y += c->lineh;
    text_at(r, tx, y, c->text, (CONST_STRPTR)c->copyright, strlen(c->copyright));
}

/* background and double frame of the area (x0, y0) - (x1, y1) */
static void draw_back(struct Card *c, struct RastPort *r, WORD x0, WORD y0, WORD x1, WORD y1)
{
    SetAPen(r, c->bg);
    RectFill(r, x0, y0, x1, y1);
    frame(c, r, x0, y0, x1, y1, TRUE);
    frame(c, r, x0 + 2, y0 + 2, x1 - 2, y1 - 2, FALSE);
}

/*****************************************************************************/
/* splash window                                                             */

static struct {
    struct Card card;
    struct Window *win;
    WORD barx, bary, barw;              /* inside of the progress bar */
    WORD statusy;
    ULONG steps, done;
} sp;

static void draw_bar(void)
{
    struct RastPort *rp;
    WORD w;

    if (!sp.win) return;
    rp = sp.win->RPort;
    w = sp.steps ? (WORD)((ULONG)sp.barw * (sp.done < sp.steps ? sp.done : sp.steps) / sp.steps) : 0;
    if (w > 0) {
        SetAPen(rp, sp.card.fill);
        RectFill(rp, sp.barx, sp.bary, sp.barx + w - 1, sp.bary + BAR_H - 1);
    }
    if (w < sp.barw) {
        SetAPen(rp, sp.card.bg);
        RectFill(rp, sp.barx + w, sp.bary, sp.barx + sp.barw - 1, sp.bary + BAR_H - 1);
    }
}

void splash_open(CONST_STRPTR iconname, CONST_STRPTR name, CONST_STRPTR version, ULONG steps)
{
    struct Card *c = &sp.card;
    struct Screen *screen;
    WORD w, h;

    if (sp.win || !(screen = LockPubScreen(NULL))) return;
    sp.steps = steps;
    sp.done = 0;
    if (!card_init(c, screen, iconname, name, version)) goto fail;

    w = MARGIN + (head_width(c) > 180 ? head_width(c) : 180) + MARGIN;
    h = MARGIN + head_height(c) + GAP + c->lineh + 4 + BAR_H + 2 + MARGIN;
    if (!(sp.win = OpenWindowTags(NULL,
            WA_PubScreen,    (ULONG)screen,
            WA_Left,         (screen->Width - w) / 2,
            WA_Top,          (screen->Height - h) / 2,
            WA_Width,        w,
            WA_Height,       h,
            WA_Borderless,   TRUE,
            WA_SmartRefresh, TRUE,
            WA_RMBTrap,      TRUE,
            WA_Activate,     FALSE,
            TAG_DONE)))
        goto fail;

    draw_back(c, sp.win->RPort, 0, 0, w - 1, h - 1);
    draw_head(c, sp.win->RPort, MARGIN, MARGIN);
    /* status line and progress bar at the bottom */
    sp.statusy = MARGIN + head_height(c) + GAP;
    sp.barx = MARGIN + 1;
    sp.bary = sp.statusy + c->lineh + 4 + 1;
    sp.barw = w - 2 * MARGIN - 2;
    frame(c, sp.win->RPort, MARGIN, sp.bary - 1, MARGIN + sp.barw + 1, sp.bary + BAR_H, FALSE);
    draw_bar();
    return;

fail:
    card_free(c);
    UnlockPubScreen(NULL, screen);
    memset(&sp, 0, sizeof(sp));
}

/* the current step, below the texts */
void splash_status(CONST_STRPTR text)
{
    struct RastPort *r;
    if (!sp.win) return;
    r = sp.win->RPort;
    SetAPen(r, sp.card.bg);
    RectFill(r, MARGIN, sp.statusy, sp.win->Width - MARGIN - 1, sp.statusy + r->TxHeight + 1);
    text_at(r, MARGIN, sp.statusy, sp.card.text, text, strlen((const char *)text));
}

void splash_step(void)
{
    if (!sp.win) return;
    sp.done++;
    draw_bar();
}

void splash_close(void)
{
    struct Screen *screen = sp.card.screen;

    if (sp.win) CloseWindow(sp.win);
    card_free(&sp.card);
    if (screen) UnlockPubScreen(NULL, screen);
    memset(&sp, 0, sizeof(sp));
}

/*****************************************************************************/
/* About window                                                              */

/* draws the contents into the window's inner area */
static void about_draw(struct Card *c, struct Window *win, const char *details, WORD sepy)
{
    struct RastPort *r = win->RPort;
    WORD x0 = win->BorderLeft, y0 = win->BorderTop;
    WORD x1 = win->Width - win->BorderRight - 1, y1 = win->Height - win->BorderBottom - 1;
    WORD y;
    const char *p = details;

    draw_back(c, r, x0, y0, x1, y1);
    draw_head(c, r, x0 + MARGIN, y0 + MARGIN);
    /* a groove between head and details */
    SetAPen(r, c->shadow);
    Move(r, x0 + MARGIN, y0 + sepy); Draw(r, x1 - MARGIN, y0 + sepy);
    SetAPen(r, c->shine);
    Move(r, x0 + MARGIN, y0 + sepy + 1); Draw(r, x1 - MARGIN, y0 + sepy + 1);
    normal_style(c, r);
    for (y = y0 + sepy + 2 + GAP; *p; y += c->lineh) {
        const char *e = strchr(p, '\n');
        ULONG len = e ? (ULONG)(e - p) : strlen(p);
        text_at(r, x0 + MARGIN, y, c->text, (CONST_STRPTR)p, len);
        p += len;
        if (*p) p++;
    }
}

/* Returns FALSE if the window could not be opened (the caller shows a
 * requester instead). Modal: the main window waits.                  */
BOOL about_window(CONST_STRPTR iconname, CONST_STRPTR name, CONST_STRPTR version, const char *details)
{
    struct Card c;
    struct Window *win;
    struct Gadget *ok;
    struct RastPort rp;
    struct IntuiMessage *msg;
    const char *p;
    WORD w, h, dw = 0, dlines = 0, bw, bh, sepy;
    ULONG mainsig = 0;
    BOOL done = FALSE;
    UWORD code;

    if (!gui.win || !card_init(&c, gui.screen, iconname, name, version)) return FALSE;

    /* measure the details and the button */
    InitRastPort(&rp);
    normal_style(&c, &rp);
    for (p = details; *p; dlines++) {
        const char *e = strchr(p, '\n');
        ULONG len = e ? (ULONG)(e - p) : strlen(p);
        WORD tw = (WORD)TextLength(&rp, (STRPTR)p, len);
        if (tw > dw) dw = tw;
        p += len;
        if (*p) p++;
    }
    bw = text_width(&rp, (CONST_STRPTR)S(MSG_OK)) + 32;
    bh = c.font->tf_YSize + 6;
    w = head_width(&c) > dw ? head_width(&c) : dw;
    w += 2 * MARGIN;
    sepy = MARGIN + head_height(&c) + GAP;
    h = sepy + 2 + GAP + dlines * c.lineh + GAP + bh + MARGIN;

    ok = (struct Gadget *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        1,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)S(MSG_OK),
        GA_Width,     bw,
        GA_Height,    bh,
        TAG_DONE);
    win = ok ? OpenWindowTags(NULL,
        WA_PubScreen,    (ULONG)gui.screen,
        WA_Title,        (ULONG)S(MSG_TBH_ABOUT),
        WA_InnerWidth,   w,
        WA_InnerHeight,  h,
        WA_Left,         gui.win->LeftEdge + (gui.win->Width - w) / 2,
        WA_Top,          gui.win->TopEdge + (gui.win->Height - h) / 2,
        WA_DragBar,      TRUE,
        WA_DepthGadget,  TRUE,
        WA_CloseGadget,  TRUE,
        WA_Activate,     TRUE,
        WA_SmartRefresh, TRUE,
        WA_RMBTrap,      TRUE,
        WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW,
        TAG_DONE) : NULL;
    if (!win) {
        if (ok) DisposeObject((Object *)ok);
        card_free(&c);
        return FALSE;
    }
    about_draw(&c, win, details, sepy);
    SetAttrs((Object *)ok, GA_Left, win->BorderLeft + (w - bw) / 2,
             GA_Top, win->BorderTop + h - MARGIN - bh, TAG_DONE);
    AddGList(win, ok, -1, 1, NULL);
    RefreshGList(ok, win, NULL, 1);

    gui_busy(TRUE);
    GetAttr(WINDOW_SigMask, gui.winobj, &mainsig);
    while (!done) {
        ULONG got = Wait(1UL << win->UserPort->mp_SigBit | mainsig);

        /* the main window waits: its input is dropped */
        if (got & mainsig)
            while (DoMethod(gui.winobj, WM_HANDLEINPUT, &code) != WMHI_LASTMSG) ;
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG class = msg->Class;
            UWORD key = msg->Code;
            ReplyMsg((struct Message *)msg);
            switch (class) {
            case IDCMP_CLOSEWINDOW:
            case IDCMP_GADGETUP:
                done = TRUE;
                break;
            case IDCMP_VANILLAKEY:
                if (key == 13 || key == 27) done = TRUE;        /* Return, Esc */
                break;
            case IDCMP_REFRESHWINDOW:
                BeginRefresh(win);
                EndRefresh(win, TRUE);
                break;
            }
        }
    }
    gui_busy(FALSE);
    RemoveGList(win, ok, 1);
    CloseWindow(win);
    DisposeObject((Object *)ok);
    card_free(&c);
    return TRUE;
}
