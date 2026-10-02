/*
 * MDEdit - Markdown editor with HTML preview (ReAction, AmigaOS 3.2)
 *
 *   MDEdit [FILE] <name.md> [TEMPLATE <file>] [CHARSET <name>]
 *          [DIALECT GitHub|CommonMark] [TTF] [NOAUTOREFRESH] [NOSYNC]
 *          [NOHIGHLIGHT]
 *
 * From the Workbench the options are read from the tool types; a project
 * icon with MDEdit as default tool is opened, icons dropped on the window
 * as well. The preview uses html.gadget (htmlttf.gadget with TTF), the
 * speedbar the AISS images in TBIMAGES:.
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/texteditor.h>
#include <gadgets/scroller.h>
#include <gadgets/html.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/asl.h>
#include <proto/icon.h>
#include <proto/html.h>
#include <clib/alib_protos.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "mdedit.h"
#include "mdconv.h"
#include "fileio.h"

#define VERSION_TEXT "1.0 (02.10.2026)"
static const char version[] = "$VER: " APPNAME " " VERSION_TEXT;

/* initialised explicitly: as COMMON symbols they would pull in the
 * auto-open stubs of libstubs.a                                          */
struct Library *AslBase = NULL, *IconBase = NULL;
struct Library *WindowBase = NULL, *LayoutBase = NULL, *ButtonBase = NULL,
               *ScrollerBase = NULL, *SpeedBarBase = NULL, *BitMapBase = NULL,
               *TextFieldBase = NULL, *HTMLBase = NULL;

/* Minimum stack, libnix swaps to a bigger one at startup if needed
 * (linked with -u ___stkinit). From the Workbench a project icon often
 * gives only 4 KB, too little for ReAction, ASL and the datatypes that
 * html.gadget calls on our stack.                                     */
unsigned long __stack = 65536;

/* started from the Workbench: libnix must not open a console window */
char *__stdiowin = NULL;
extern struct WBStartup *_WBenchMsg;

/* intuiticks (about 1/10 s) without typing until the preview follows */
#define REFRESH_DELAY 5

#define PATHLEN 512

/* options from the command line or the tool types */
static struct {
    char file[PATHLEN];
    char template[PATHLEN];
    char charset[40];
    char dialect[20];
    BOOL ttf;
    BOOL noautorefresh;
    BOOL nosync;
    BOOL nohighlight;
} opt;

static struct MDConvOptions conv;
static STRPTR template_text;

static char curfile[PATHLEN];       /* "" for a new document */
static BOOL modified;
static int pending;                 /* ticks until the next automatic refresh */
static ULONG cur_x = ~0UL, cur_y = ~0UL;
static char lastdir[PATHLEN];
static char dropped[PATHLEN];       /* set by the AppWindow hook */

static BOOL open_document(CONST_STRPTR name);

/*****************************************************************************/

static LONG request(CONST_STRPTR gadgets, CONST_STRPTR fmt, ...)
{
    struct EasyStruct es;
    va_list ap;
    LONG r;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)APPNAME;
    es.es_TextFormat = (STRPTR)fmt;
    es.es_GadgetFormat = (STRPTR)gadgets;
    /* on the 68k all arguments are LONGs on the stack */
    va_start(ap, fmt);
    r = EasyRequestArgs(gui.win, &es, NULL, (APTR)ap);
    va_end(ap);
    return r;
}

/* before the window is open: shell output or requester */
static void message(CONST_STRPTR text, CONST_STRPTR arg)
{
    if (_WBenchMsg || gui.win) request((CONST_STRPTR)"OK", text, arg);
    else {
        Printf((STRPTR)text, (ULONG)arg);
        PutStr((STRPTR)"\n");
    }
}

static void dos_error(CONST_STRPTR what, CONST_STRPTR name)
{
    char err[100];
    Fault(IoErr(), NULL, (STRPTR)err, sizeof(err));
    request((CONST_STRPTR)"OK", (CONST_STRPTR)"%s\n%s\n%s", what, name, err);
}

static void busy(BOOL on)
{
    SetAttrs(gui.winobj, WA_BusyPointer, on, TAG_DONE);
}

static const char *doc_name(void)
{
    return curfile[0] ? (const char *)FilePart((STRPTR)curfile) : "Untitled";
}

static void update_title(void)
{
    char title[160];
    snprintf(title, sizeof(title), APPNAME " - %s%s", doc_name(), modified ? " (modified)" : "");
    gui_title((CONST_STRPTR)title);
}

/*****************************************************************************/
/* editor                                                                    */

static void editor_set(CONST_STRPTR text)
{
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                   GA_TEXTEDITOR_Contents, (ULONG)text, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                   GA_TEXTEDITOR_HasChanged, FALSE, TAG_DONE);
}

/* the editor's text, release with FreeVec() */
static STRPTR editor_text(void)
{
    return (STRPTR)DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL,
                                  GM_TEXTEDITOR_ExportText, 0);
}

/* only commands without a result: the return value is not a string */
static void editor_cmd(CONST_STRPTR cmd)
{
    DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL,
                   GM_TEXTEDITOR_ARexxCmd, 0, (ULONG)cmd);
}

/* Transfers the editor's change flag into 'modified'. HasChanged is reset
 * every time, so it also tells about every later change. Returns TRUE if
 * the text changed since the last call.                                 */
static BOOL poll_changes(void)
{
    ULONG changed = FALSE;

    GetAttr(GA_TEXTEDITOR_HasChanged, gui.editor, &changed);
    if (!changed) return FALSE;
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                   GA_TEXTEDITOR_HasChanged, FALSE, TAG_DONE);
    if (!modified) {
        modified = TRUE;
        update_title();
    }
    return TRUE;
}

/*****************************************************************************/
/* preview                                                                   */

/* file name without path and extension, the title of untitled pages */
static void base_name(char *out, ULONG size)
{
    char *dot;
    strncpy(out, doc_name(), size - 1);
    out[size - 1] = 0;
    if ((dot = strrchr(out, '.')) && dot != out) *dot = 0;
}

/* lock on the document's directory, 0 for a new document */
static BPTR doc_dir(void)
{
    char dir[PATHLEN];
    if (!curfile[0]) return 0;
    strcpy(dir, curfile);
    *PathPart((STRPTR)dir) = 0;
    return Lock((STRPTR)dir, ACCESS_READ);
}

static char *convert(STRPTR text, size_t *len)
{
    char title[108];
    base_name(title, sizeof(title));
    conv.fallback_title = title;
    return mdconv_html((const char *)text, strlen((const char *)text), &conv, len);
}

static void update_preview(void)
{
    STRPTR text;
    char *html;
    ULONG top = 0;
    BPTR dir, olddir = 0;

    pending = 0;
    if (!(text = editor_text())) return;
    if (!(html = convert(text, NULL))) {
        FreeVec(text);
        gui_status((CONST_STRPTR)"Not enough memory for the preview");
        return;
    }

    /* relative <img> paths are resolved against the current directory
     * while the gadget loads the pictures                              */
    GetAttr(HTML_Top, gui.html, &top);
    if ((dir = doc_dir())) olddir = CurrentDir(dir);
    SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Text, (ULONG)html, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Top, top, TAG_DONE);
    if (dir) UnLock(CurrentDir(olddir));
    sync_rebuild(text, html);
    mdconv_free(html);
    FreeVec(text);
    gui_sync_hscroll();
}

/* case-insensitive comparison of ASCII names */
static BOOL same_text(const char *a, const char *b)
{
    while (*a && (*a | 0x20) == (*b | 0x20)) a++, b++;
    return *a == *b;
}

static void follow_link(void)
{
    STRPTR url = NULL;
    char path[PATHLEN], msg[PATHLEN + 8], *hash, *ext;

    GetAttr(HTML_LinkURL, gui.html, (ULONG *)&url);
    if (!url || url[0] == '#') return;         /* HTML_AutoAnchors scrolls */

    if (!strstr((const char *)url, "://") && strncmp((const char *)url, "mailto:", 7)) {
        /* a local file: Markdown documents are opened in the editor */
        if (!strncmp((const char *)url, "file:", 5)) url += 5;
        if (curfile[0]) {
            strcpy(path, curfile);
            *PathPart((STRPTR)path) = 0;
        } else path[0] = 0;
        AddPart((STRPTR)path, url, sizeof(path));
        if ((hash = strchr(path, '#'))) *hash = 0;
        ext = strrchr(path, '.');
        if (ext && (same_text(ext, ".md") || same_text(ext, ".markdown"))) {
            open_document((CONST_STRPTR)path);
            return;
        }
    }
    snprintf(msg, sizeof(msg), "Link: %s", (const char *)url);
    gui_status((CONST_STRPTR)msg);
}

/*****************************************************************************/
/* files                                                                     */

static BOOL file_request(BOOL save, CONST_STRPTR title, CONST_STRPTR pattern,
                         CONST_STRPTR initial, char *out, ULONG size)
{
    struct FileRequester *fr;
    char drawer[PATHLEN];
    BOOL ok = FALSE;

    if (initial && *initial) {
        strncpy(drawer, (const char *)initial, sizeof(drawer) - 1);
        drawer[sizeof(drawer) - 1] = 0;
        *PathPart((STRPTR)drawer) = 0;
    } else drawer[0] = 0;
    if (!drawer[0]) strcpy(drawer, lastdir);

    if (!(fr = AllocAslRequestTags(ASL_FileRequest, TAG_DONE))) return FALSE;
    if (AslRequestTags(fr,
            ASLFR_Window,         (ULONG)gui.win,
            ASLFR_SleepWindow,    TRUE,
            ASLFR_TitleText,      (ULONG)title,
            ASLFR_DoSaveMode,     save,
            ASLFR_InitialDrawer,  (ULONG)drawer,
            ASLFR_InitialFile,    initial ? (ULONG)FilePart((STRPTR)initial) : (ULONG)"",
            ASLFR_InitialPattern, (ULONG)pattern,
            ASLFR_DoPatterns,     TRUE,
            ASLFR_RejectIcons,    TRUE,
            TAG_DONE) && fr->fr_File[0]) {
        strncpy(out, (const char *)fr->fr_Drawer, size - 1);
        out[size - 1] = 0;
        strncpy(lastdir, out, sizeof(lastdir) - 1);
        AddPart((STRPTR)out, fr->fr_File, size);
        ok = TRUE;
    }
    FreeAslRequest(fr);
    return ok;
}

static BOOL confirm_overwrite(CONST_STRPTR name)
{
    BPTR lock = Lock((STRPTR)name, ACCESS_READ);
    if (!lock) return TRUE;
    UnLock(lock);
    return request((CONST_STRPTR)"Replace|Cancel",
                   (CONST_STRPTR)"%s\nalready exists. Replace it?", name) == 1;
}

static BOOL save_document(BOOL ask)
{
    char name[PATHLEN];
    STRPTR text;
    BOOL ok;

    poll_changes();
    if (ask || !curfile[0]) {
        if (!file_request(TRUE, (CONST_STRPTR)"Save Markdown file", (CONST_STRPTR)"#?.(md|markdown|txt)",
                          curfile[0] ? (CONST_STRPTR)curfile : NULL, name, sizeof(name)))
            return FALSE;
        if (!confirm_overwrite((CONST_STRPTR)name)) return FALSE;
    } else strcpy(name, curfile);

    if (!(text = editor_text())) {
        request((CONST_STRPTR)"OK", (CONST_STRPTR)"Not enough memory to save the text.");
        return FALSE;
    }
    busy(TRUE);
    ok = write_file((CONST_STRPTR)name, text, strlen((const char *)text));
    busy(FALSE);
    FreeVec(text);
    if (!ok) {
        dos_error((CONST_STRPTR)"Could not save", (CONST_STRPTR)name);
        return FALSE;
    }
    strcpy(curfile, name);
    modified = FALSE;
    update_title();
    gui_status((CONST_STRPTR)"Saved");
    return TRUE;
}

/* TRUE if the current document may be replaced */
static BOOL check_save(void)
{
    poll_changes();
    if (!modified) return TRUE;
    switch (request((CONST_STRPTR)"Save|Discard|Cancel",
                    (CONST_STRPTR)"%s has been modified.\nSave the changes?", (CONST_STRPTR)doc_name())) {
    case 1:  return save_document(FALSE);
    case 2:  return TRUE;
    default: return FALSE;
    }
}

static BOOL load_document(CONST_STRPTR name)
{
    STRPTR text;
    ULONG len;
    char msg[PATHLEN + 40];

    busy(TRUE);
    text = read_file(name, &len);
    if (!text) {
        busy(FALSE);
        dos_error((CONST_STRPTR)"Could not open", name);
        return FALSE;
    }
    editor_set(text);
    FreeVec(text);
    strncpy(curfile, (const char *)name, sizeof(curfile) - 1);
    modified = FALSE;
    update_title();
    update_preview();
    SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Top, 0, TAG_DONE);
    sync_reset();
    busy(FALSE);
    snprintf(msg, sizeof(msg), "%s (%lu bytes)", (const char *)name, (unsigned long)len);
    gui_status((CONST_STRPTR)msg);
    gui_activate_editor();
    return TRUE;
}

/* also used by follow_link() */
static BOOL open_document(CONST_STRPTR name)
{
    return check_save() && load_document(name);
}

static void new_document(void)
{
    if (!check_save()) return;
    DoGadgetMethod((struct Gadget *)gui.editor, gui.win, NULL, GM_TEXTEDITOR_ClearText, 0);
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                   GA_TEXTEDITOR_HasChanged, FALSE, TAG_DONE);
    curfile[0] = 0;
    modified = FALSE;
    update_title();
    update_preview();
    gui_status((CONST_STRPTR)"New document");
    gui_activate_editor();
}

static void open_requested(void)
{
    char name[PATHLEN];
    if (!check_save()) return;
    if (file_request(FALSE, (CONST_STRPTR)"Open Markdown file", (CONST_STRPTR)"#?.(md|markdown|txt)",
                     curfile[0] ? (CONST_STRPTR)curfile : NULL, name, sizeof(name)))
        load_document((CONST_STRPTR)name);
}

static void export_html(void)
{
    char name[PATHLEN], *dot, *html;
    STRPTR text;
    size_t len = 0;
    BOOL ok;

    /* default: the document's name with .html */
    if (curfile[0]) {
        strcpy(name, curfile);
        if ((dot = strrchr((char *)FilePart((STRPTR)name), '.'))) *dot = 0;
    } else strcpy(name, "Untitled");
    strncat(name, ".html", sizeof(name) - strlen(name) - 1);

    if (!file_request(TRUE, (CONST_STRPTR)"Export HTML file", (CONST_STRPTR)"#?.(html|htm)",
                      (CONST_STRPTR)name, name, sizeof(name)))
        return;
    if (!confirm_overwrite((CONST_STRPTR)name)) return;

    html = NULL;
    if ((text = editor_text())) {
        html = convert(text, &len);
        FreeVec(text);
    }
    if (!html) {
        request((CONST_STRPTR)"OK", (CONST_STRPTR)"Not enough memory to convert the text.");
        return;
    }
    busy(TRUE);
    ok = write_file((CONST_STRPTR)name, (CONST_STRPTR)html, len);
    busy(FALSE);
    mdconv_free(html);
    if (!ok) dos_error((CONST_STRPTR)"Could not write", (CONST_STRPTR)name);
    else gui_status((CONST_STRPTR)"HTML exported");
}

static void about(void)
{
    request((CONST_STRPTR)"OK",
            (CONST_STRPTR)APPNAME " " VERSION_TEXT "\n"
            "Markdown editor with HTML preview\n\n"
            "Copyright (c) 2026 Andre Gewert\n"
            "Released under the MIT License\n\n"
            "Markdown parser: %s\n"
            "Preview: %s %ld.%ld",
            (CONST_STRPTR)mdconv_version(),
            (CONST_STRPTR)HTMLBase->lib_Node.ln_Name,
            (LONG)HTMLBase->lib_Version, (LONG)HTMLBase->lib_Revision);
}

static void copy_preview(void)
{
    ULONG has = FALSE;
    GetAttr(HTML_HasSelection, gui.html, &has);
    if (has) {
        SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Copy, TRUE, TAG_DONE);
        gui_status((CONST_STRPTR)"Preview selection copied to the clipboard");
    } else gui_status((CONST_STRPTR)"Nothing selected in the preview");
}

/* returns TRUE to quit */
static BOOL command(ULONG cmd)
{
    switch (cmd) {
    case CMD_NEW:         new_document(); break;
    case CMD_OPEN:        open_requested(); break;
    case CMD_SAVE:        save_document(FALSE); break;
    case CMD_SAVEAS:      save_document(TRUE); break;
    case CMD_EXPORT:      export_html(); break;
    case CMD_ABOUT:       about(); break;
    case CMD_QUIT:        return check_save();
    case CMD_CUT:         editor_cmd((CONST_STRPTR)"CUT"); break;
    case CMD_COPY:        editor_cmd((CONST_STRPTR)"COPY"); break;
    case CMD_PASTE:       editor_cmd((CONST_STRPTR)"PASTE"); break;
    case CMD_UNDO:        editor_cmd((CONST_STRPTR)"UNDO"); break;
    case CMD_REDO:        editor_cmd((CONST_STRPTR)"REDO"); break;
    case CMD_SELECTALL:
        editor_cmd((CONST_STRPTR)"POSITION SOF");
        editor_cmd((CONST_STRPTR)"MARK ON");
        editor_cmd((CONST_STRPTR)"POSITION EOF");
        break;
    case CMD_REFRESH:     update_preview(); break;
    case CMD_AUTOREFRESH: if (gui_checked(CMD_AUTOREFRESH)) update_preview(); break;
    case CMD_SYNCSCROLL:  sync_reset(); break;
    case CMD_HIGHLIGHT:
        /* setting the hook (or NULL) formats the whole text anew */
        SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL, GA_TEXTEDITOR_HighlighterHook,
                       gui_checked(CMD_HIGHLIGHT) ? (ULONG)highlight_hook() : 0, TAG_DONE);
        break;
    case CMD_COPYPREVIEW: copy_preview(); break;
    }
    return FALSE;
}

/* about ten times a second while the window is active */
static void tick(void)
{
    ULONG x = 0, y = 0;

    if (poll_changes()) pending = REFRESH_DELAY;
    else if (pending && --pending == 0 && gui_checked(CMD_AUTOREFRESH)) update_preview();
    if (gui_checked(CMD_SYNCSCROLL)) sync_poll();

    GetAttr(GA_TEXTEDITOR_CursorX, gui.editor, &x);
    GetAttr(GA_TEXTEDITOR_CursorY, gui.editor, &y);
    if (x != cur_x || y != cur_y) {
        cur_x = x;
        cur_y = y;
        gui_position(y + 1, x + 1);
    }
}

static BOOL mouse_over(Object *o)
{
    struct Gadget *g = (struct Gadget *)o;
    WORD x = gui.win->MouseX, y = gui.win->MouseY;
    return x >= g->LeftEdge && y >= g->TopEdge &&
           x < g->LeftEdge + g->Width && y < g->TopEdge + g->Height;
}

/* mouse wheel (NewMouse raw keys) scrolls the pane below the pointer */
static void wheel(LONG dir)
{
    if (mouse_over(gui.html)) {
        ULONG top = 0, lh = 8;
        LONG t;
        GetAttr(HTML_Top, gui.html, &top);
        GetAttr(HTML_LineHeight, gui.html, &lh);
        t = (LONG)top + dir * 3 * (LONG)lh;
        SetGadgetAttrs((struct Gadget *)gui.html, gui.win, NULL, HTML_Top, t < 0 ? 0 : t, TAG_DONE);
    } else if (mouse_over(gui.editor)) {
        ULONG first = 0;
        LONG f;
        GetAttr(GA_TEXTEDITOR_Prop_First, gui.editor, &first);
        f = (LONG)first + dir * 3;
        if (f < 0) f = 0;
        SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL, GA_TEXTEDITOR_Prop_First, f, TAG_DONE);
        SetGadgetAttrs((struct Gadget *)gui.escroll, gui.win, NULL, SCROLLER_Top, f, TAG_DONE);
    }
}

/* AppWindow: remember the first dropped file, it is opened by the main loop */
static ULONG appmsg_func(struct Hook *hook, Object *winobj, struct AppMessage *msg)
{
    if (msg->am_NumArgs > 0 && msg->am_ArgList[0].wa_Lock &&
        NameFromLock(msg->am_ArgList[0].wa_Lock, (STRPTR)dropped, sizeof(dropped)))
        AddPart((STRPTR)dropped, msg->am_ArgList[0].wa_Name, sizeof(dropped));
    return 0;
}

static struct Hook apphook = { { NULL, NULL }, (APTR)HookEntry, (APTR)appmsg_func, NULL };

/*****************************************************************************/
/* startup                                                                   */

static void copy_opt(char *dst, ULONG size, CONST_STRPTR src)
{
    strncpy(dst, (const char *)src, size - 1);
    dst[size - 1] = 0;
}

/* Workbench start: tool types of MDEdit.info and a project argument */
static void wb_options(struct WBStartup *wbs)
{
    struct WBArg *wa = wbs->sm_ArgList;
    struct DiskObject *dob;
    LONG i;
    BPTR old;

    for (i = 0; i < wbs->sm_NumArgs && i < 2; i++) {
        if (!wa[i].wa_Lock) continue;
        old = CurrentDir(wa[i].wa_Lock);
        if ((dob = GetDiskObject(wa[i].wa_Name))) {
            CONST_STRPTR *tt = (CONST_STRPTR *)dob->do_ToolTypes;
            STRPTR v;
            /* the template is relative to the icon's drawer */
            if ((v = FindToolType(tt, (STRPTR)"TEMPLATE")) &&
                NameFromLock(wa[i].wa_Lock, (STRPTR)opt.template, sizeof(opt.template)))
                AddPart((STRPTR)opt.template, v, sizeof(opt.template));
            if ((v = FindToolType(tt, (STRPTR)"CHARSET"))) copy_opt(opt.charset, sizeof(opt.charset), v);
            if ((v = FindToolType(tt, (STRPTR)"DIALECT"))) copy_opt(opt.dialect, sizeof(opt.dialect), v);
            if (FindToolType(tt, (STRPTR)"TTF")) opt.ttf = TRUE;
            if (FindToolType(tt, (STRPTR)"NOAUTOREFRESH")) opt.noautorefresh = TRUE;
            if (FindToolType(tt, (STRPTR)"NOSYNC")) opt.nosync = TRUE;
            if (FindToolType(tt, (STRPTR)"NOHIGHLIGHT")) opt.nohighlight = TRUE;
            FreeDiskObject(dob);
        }
        CurrentDir(old);
    }
    if (wbs->sm_NumArgs > 1 && wa[1].wa_Lock &&
        NameFromLock(wa[1].wa_Lock, (STRPTR)opt.file, sizeof(opt.file)))
        AddPart((STRPTR)opt.file, wa[1].wa_Name, sizeof(opt.file));
}

static BOOL shell_options(void)
{
    LONG args[8] = { 0 };
    struct RDArgs *rda = ReadArgs((STRPTR)"FILE,TEMPLATE/K,CHARSET/K,DIALECT/K,TTF/S,NOAUTOREFRESH/S,"
                                  "NOSYNC/S,NOHIGHLIGHT/S",
                                  args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (STRPTR)APPNAME);
        return FALSE;
    }
    if (args[0]) copy_opt(opt.file, sizeof(opt.file), (CONST_STRPTR)args[0]);
    if (args[1]) copy_opt(opt.template, sizeof(opt.template), (CONST_STRPTR)args[1]);
    if (args[2]) copy_opt(opt.charset, sizeof(opt.charset), (CONST_STRPTR)args[2]);
    if (args[3]) copy_opt(opt.dialect, sizeof(opt.dialect), (CONST_STRPTR)args[3]);
    opt.ttf = args[4] != 0;
    opt.noautorefresh = args[5] != 0;
    opt.nosync = args[6] != 0;
    opt.nohighlight = args[7] != 0;
    FreeArgs(rda);
    return TRUE;
}

static struct Library *open_class(CONST_STRPTR name, ULONG ver)
{
    struct Library *base = OpenLibrary((STRPTR)name, ver);
    if (!base) message((CONST_STRPTR)"Could not open %s.", name);
    return base;
}

/* html.gadget/htmlttf.gadget: installed or next to the program */
static struct Library *open_html(BOOL ttf)
{
    CONST_STRPTR name = (CONST_STRPTR)(ttf ? "htmlttf.gadget" : "html.gadget");
    char path[64];
    struct Library *base;

    sprintf(path, "gadgets/%s", (const char *)name);
    if ((base = OpenLibrary((STRPTR)path, 1))) return base;
    sprintf(path, "PROGDIR:%s", (const char *)name);
    if ((base = OpenLibrary((STRPTR)path, 1))) return base;
    message((CONST_STRPTR)"%s not found (SYS:Classes/Gadgets/ or program directory).", name);
    return NULL;
}

int main(void)
{
    struct MsgPort *appport = NULL;
    ULONG sigmask = 0, result, appsig = 0;
    UWORD code;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (_WBenchMsg) {
        if (IconBase) wb_options(_WBenchMsg);
    } else if (!shell_options()) goto out;

    if (!(AslBase = open_class((CONST_STRPTR)"asl.library", 39)) ||
        !(WindowBase = open_class((CONST_STRPTR)"window.class", 44)) ||
        !(LayoutBase = open_class((CONST_STRPTR)"gadgets/layout.gadget", 44)) ||
        !(ButtonBase = open_class((CONST_STRPTR)"gadgets/button.gadget", 44)) ||
        !(ScrollerBase = open_class((CONST_STRPTR)"gadgets/scroller.gadget", 44)) ||
        !(SpeedBarBase = open_class((CONST_STRPTR)"gadgets/speedbar.gadget", 44)) ||
        !(BitMapBase = open_class((CONST_STRPTR)"images/bitmap.image", 44)) ||
        !(TextFieldBase = open_class((CONST_STRPTR)"gadgets/texteditor.gadget", 45)) ||
        !(HTMLBase = open_html(opt.ttf)))
        goto out;

    /* conversion options */
    conv.flags = mdconv_default_flags();
    if (opt.dialect[0] && !mdconv_dialect(opt.dialect, &conv.flags))
        message((CONST_STRPTR)"Unknown dialect \"%s\", using GitHub.", (CONST_STRPTR)opt.dialect);
    if (opt.charset[0]) conv.charset = opt.charset;
    if (opt.template[0]) {
        if ((template_text = read_file((CONST_STRPTR)opt.template, NULL)))
            conv.tmpl = (const char *)template_text;
        else
            message((CONST_STRPTR)"Could not read the template %s.", (CONST_STRPTR)opt.template);
    }

    appport = CreateMsgPort();
    if (!gui_open(HTML_GetClass(), appport, appport ? &apphook : NULL,
                  !opt.noautorefresh, !opt.nosync, !opt.nohighlight)) {
        message((CONST_STRPTR)"Could not open the window.", NULL);
        goto out;
    }
    update_title();

    if (opt.file[0]) load_document((CONST_STRPTR)opt.file);
    else update_preview();

    GetAttr(WINDOW_SigMask, gui.winobj, &sigmask);
    if (appport) appsig = 1UL << appport->mp_SigBit;
    while (!done) {
        ULONG sig = Wait(sigmask | appsig | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((result = DoMethod(gui.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                done = check_save();
                break;
            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                case GID_TOOLBAR: done = command(code); break;
                case GID_HTML:    follow_link(); break;
                }
                break;
            case WMHI_MENUPICK: {
                UWORD num = result & WMHI_MENUMASK;
                while (num != MENUNULL && !done) {
                    struct MenuItem *item = ItemAddress(gui.win->MenuStrip, num);
                    if (!item) break;
                    done = command((ULONG)GTMENUITEM_USERDATA(item));
                    num = item->NextSelect;
                }
                break;
            }
            case WMHI_INTUITICK:
                tick();
                break;
            case WMHI_NEWSIZE:
                /* new line breaks in editor and preview: new fixed points */
                update_preview();
                sync_reset();
                break;
            case WMHI_RAWKEY:
                switch (result & WMHI_KEYMASK) {
                case 0x7A: wheel(-1); break;        /* wheel up */
                case 0x7B: wheel(1); break;         /* wheel down */
                }
                break;
            }
            if (done) break;
        }
        if (dropped[0] && !done) {
            char name[PATHLEN];
            strcpy(name, dropped);
            dropped[0] = 0;
            open_document((CONST_STRPTR)name);
        }
    }
    rc = RETURN_OK;

out:
    gui_close();
    sync_free();
    if (appport) DeleteMsgPort(appport);
    if (template_text) FreeVec(template_text);
    if (HTMLBase) CloseLibrary(HTMLBase);
    if (TextFieldBase) CloseLibrary(TextFieldBase);
    if (BitMapBase) CloseLibrary(BitMapBase);
    if (SpeedBarBase) CloseLibrary(SpeedBarBase);
    if (ScrollerBase) CloseLibrary(ScrollerBase);
    if (ButtonBase) CloseLibrary(ButtonBase);
    if (LayoutBase) CloseLibrary(LayoutBase);
    if (WindowBase) CloseLibrary(WindowBase);
    if (AslBase) CloseLibrary(AslBase);
    if (IconBase) CloseLibrary(IconBase);
    (void)version;
    return rc;
}
