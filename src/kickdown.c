/*
 * Kickdown - Markdown editor with HTML preview (ReAction, AmigaOS 3.2)
 *
 *   Kickdown [FILE] <name.md> [TEMPLATE <file>] [CHARSET <name>]
 *          [DIALECT GitHub|CommonMark] [TTF] [NOAUTOREFRESH] [NOSYNC]
 *          [NOHIGHLIGHT] [LINENUMBERS] [FONTSET Vera|DejaVu|Noto] [SIZE n]
 *
 * The settings are the tool types of the program icon (also when started
 * from the Shell), Shell arguments or the tool types of a project icon
 * take precedence. Project/Settings edits them and saves them back into
 * the program icon. A project icon with Kickdown as default tool is opened,
 * icons dropped on the window as well. The preview uses html.gadget (htmlttf.gadget with TTF), the
 * speedbar the AISS images in TBIMAGES:.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
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

#include "kickdown.h"
#include "mdconv.h"
#include "fileio.h"
#include "settings.h"

#define VERSION_TEXT "1.0 (03.10.2026)"
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

/* settings: tool types of the program icon, then of a project icon or
 * the Shell arguments                                                   */
static struct Settings set;
static char startfile[PATHLEN];     /* document from the Shell or the Workbench */
static char iconname[PATHLEN];      /* "PROGDIR:Kickdown", where the settings are saved */

static struct MDConvOptions conv;
static STRPTR template_text;

static char curfile[PATHLEN];       /* "" for a new document */
static BOOL modified;
static int pending;                 /* ticks until the next automatic refresh */
static ULONG cur_x = ~0UL, cur_y = ~0UL;
static char lastdir[PATHLEN];
static char dropped[PATHLEN];       /* set by the AppWindow hook */

static BOOL open_document(CONST_STRPTR name);
static void update_preview(void);

/*****************************************************************************/

/* Requester with printf style text: centred over the main window (see
 * dialog.c), before it is open with EasyRequest().                    */
static LONG vrequest(CONST_STRPTR gadgets, BOOL centred, CONST_STRPTR fmt, va_list ap)
{
    char text[1024];
    struct EasyStruct es;
    LONG r;

    vsnprintf(text, sizeof(text), (const char *)fmt, ap);
    if ((r = dialog((CONST_STRPTR)APPNAME, (CONST_STRPTR)text, gadgets, centred)) >= 0) return r;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)APPNAME;
    es.es_TextFormat = (STRPTR)"%s";        /* the text may contain '%' */
    es.es_GadgetFormat = (STRPTR)gadgets;
    return EasyRequest(gui.win, &es, NULL, (ULONG)text);
}

static LONG request(CONST_STRPTR gadgets, CONST_STRPTR fmt, ...)
{
    va_list ap;
    LONG r;
    va_start(ap, fmt);
    r = vrequest(gadgets, FALSE, fmt, ap);
    va_end(ap);
    return r;
}

static LONG request_centred(CONST_STRPTR gadgets, CONST_STRPTR fmt, ...)
{
    va_list ap;
    LONG r;
    va_start(ap, fmt);
    r = vrequest(gadgets, TRUE, fmt, ap);
    va_end(ap);
    return r;
}

/* before the window is open: shell output or requester */
static void message(CONST_STRPTR text, CONST_STRPTR arg)
{
    splash_close();                     /* not over the requester */
    if (_WBenchMsg || gui.win) request((CONST_STRPTR)S(MSG_OK), text, arg);
    else {
        Printf((STRPTR)text, (ULONG)arg);
        PutStr((STRPTR)"\n");
    }
}

static void dos_error(CONST_STRPTR what, CONST_STRPTR name)
{
    char err[100];
    Fault(IoErr(), NULL, (STRPTR)err, sizeof(err));
    request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)"%s\n%s\n%s", what, name, err);
}

static void busy(BOOL on)
{
    SetAttrs(gui.winobj, WA_BusyPointer, on, TAG_DONE);
}

static const char *doc_name(void)
{
    return curfile[0] ? (const char *)FilePart((STRPTR)curfile) : S(MSG_UNTITLED);
}

/* ghosts the buttons that would do nothing now */
static void update_tools(void)
{
    ULONG undo = TRUE, redo = TRUE, marked = TRUE, mask = 0;

    GetAttr(GA_TEXTEDITOR_UndoAvailable, gui.editor, &undo);
    GetAttr(GA_TEXTEDITOR_RedoAvailable, gui.editor, &redo);
    GetAttr(GA_TEXTEDITOR_AreaMarked, gui.editor, &marked);
    if (!modified) mask |= TOOLBIT(CMD_SAVE);
    if (!undo)     mask |= TOOLBIT(CMD_UNDO);
    if (!redo)     mask |= TOOLBIT(CMD_REDO);
    if (!marked)   mask |= TOOLBIT(CMD_CUT) | TOOLBIT(CMD_COPY);
    gui_tools_disabled(mask);
    gui_update_overflow();
}

static void update_title(void)
{
    char title[160];
    snprintf(title, sizeof(title), APPNAME " - %s%s", doc_name(), modified ? S(MSG_MODIFIED) : "");
    gui_title((CONST_STRPTR)title);
    gui_icon_title((CONST_STRPTR)doc_name());
    update_tools();
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

/* the text was changed by find.c: the editor's window gets no ticks then */
void editor_changed(void)
{
    if (poll_changes() && set.autorefresh) update_preview();
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
        gui_status((CONST_STRPTR)S(MSG_NOMEM_PREVIEW));
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
    snprintf(msg, sizeof(msg), S(MSG_LINK), (const char *)url);
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
    return request((CONST_STRPTR)S(MSG_REPLACE_CANCEL), (CONST_STRPTR)S(MSG_FILE_EXISTS), name) == 1;
}

static BOOL save_document(BOOL ask)
{
    char name[PATHLEN];
    STRPTR text;
    BOOL ok;

    poll_changes();
    if (ask || !curfile[0]) {
        if (!file_request(TRUE, (CONST_STRPTR)S(MSG_REQ_SAVE), (CONST_STRPTR)"#?.(md|markdown|txt)",
                          curfile[0] ? (CONST_STRPTR)curfile : NULL, name, sizeof(name)))
            return FALSE;
        if (!confirm_overwrite((CONST_STRPTR)name)) return FALSE;
    } else strcpy(name, curfile);

    if (!(text = editor_text())) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_NOMEM_SAVE));
        return FALSE;
    }
    busy(TRUE);
    ok = write_file((CONST_STRPTR)name, text, strlen((const char *)text));
    busy(FALSE);
    FreeVec(text);
    if (!ok) {
        dos_error((CONST_STRPTR)S(MSG_SAVE_FAILED), (CONST_STRPTR)name);
        return FALSE;
    }
    strcpy(curfile, name);
    modified = FALSE;
    update_title();
    gui_status((CONST_STRPTR)S(MSG_SAVED));
    return TRUE;
}

/* TRUE if the current document may be replaced */
static BOOL check_save(void)
{
    poll_changes();
    if (!modified) return TRUE;
    switch (request((CONST_STRPTR)S(MSG_SAVE_DISCARD_CANCEL), (CONST_STRPTR)S(MSG_MODIFIED_ASK),
                    (CONST_STRPTR)doc_name())) {
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
        dos_error((CONST_STRPTR)S(MSG_OPEN_FAILED), name);
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
    snprintf(msg, sizeof(msg), S(MSG_LOADED), (const char *)name, (unsigned long)len);
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
    gui_status((CONST_STRPTR)S(MSG_NEW_DOCUMENT));
    gui_activate_editor();
}

static void open_requested(void)
{
    char name[PATHLEN];
    if (!check_save()) return;
    if (file_request(FALSE, (CONST_STRPTR)S(MSG_REQ_OPEN), (CONST_STRPTR)"#?.(md|markdown|txt)",
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
    } else strcpy(name, S(MSG_UNTITLED));
    strncat(name, ".html", sizeof(name) - strlen(name) - 1);

    if (!file_request(TRUE, (CONST_STRPTR)S(MSG_REQ_EXPORT), (CONST_STRPTR)"#?.(html|htm)",
                      (CONST_STRPTR)name, name, sizeof(name)))
        return;
    if (!confirm_overwrite((CONST_STRPTR)name)) return;

    html = NULL;
    if ((text = editor_text())) {
        html = convert(text, &len);
        FreeVec(text);
    }
    if (!html) {
        request((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_NOMEM_CONVERT));
        return;
    }
    busy(TRUE);
    ok = write_file((CONST_STRPTR)name, (CONST_STRPTR)html, len);
    busy(FALSE);
    mdconv_free(html);
    if (!ok) dos_error((CONST_STRPTR)S(MSG_WRITE_FAILED), (CONST_STRPTR)name);
    else gui_status((CONST_STRPTR)S(MSG_EXPORTED));
}

static void about(void)
{
    char details[300];

    snprintf(details, sizeof(details), S(MSG_ABOUT_DETAILS), mdconv_version(),
             (const char *)HTMLBase->lib_Node.ln_Name,
             (long)HTMLBase->lib_Version, (long)HTMLBase->lib_Revision);
    if (about_window((CONST_STRPTR)iconname, (CONST_STRPTR)APPNAME, (CONST_STRPTR)VERSION_TEXT, details))
        return;
    /* without the window: a requester */
    request_centred((CONST_STRPTR)S(MSG_OK), (CONST_STRPTR)S(MSG_ABOUT),
            (CONST_STRPTR)APPNAME, (CONST_STRPTR)VERSION_TEXT,
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
        gui_status((CONST_STRPTR)S(MSG_PREVIEW_COPIED));
    } else gui_status((CONST_STRPTR)S(MSG_PREVIEW_NOSEL));
}

/*****************************************************************************/
/* settings                                                                  */

/* conversion options from the settings; reads the template */
static void setup_conversion(void)
{
    if (template_text) FreeVec(template_text);
    template_text = NULL;
    conv.flags = mdconv_default_flags();
    if (set.dialect[0] && !mdconv_dialect(set.dialect, &conv.flags))
        message((CONST_STRPTR)S(MSG_UNKNOWN_DIALECT), (CONST_STRPTR)set.dialect);
    conv.charset = set.charset[0] ? set.charset : NULL;
    conv.tmpl = NULL;
    if (set.template[0]) {
        if ((template_text = read_file((CONST_STRPTR)set.template, NULL)))
            conv.tmpl = (const char *)template_text;
        else
            message((CONST_STRPTR)S(MSG_NO_TEMPLATE), (CONST_STRPTR)set.template);
    }
}

/* setting the hook (or NULL) formats the whole text anew */
static void set_highlighting(void)
{
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL, GA_TEXTEDITOR_HighlighterHook,
                   set.highlight ? (ULONG)highlight_hook() : 0, TAG_DONE);
}

static void set_linenumbers(void)
{
    SetGadgetAttrs((struct Gadget *)gui.editor, gui.win, NULL,
                   GA_TEXTEDITOR_ShowLineNumbers, set.linenumbers, TAG_DONE);
    /* the text gets narrower or wider: other line breaks for the sync */
    update_preview();
    sync_reset();
}

/* takes over changed settings while the program runs */
/* formatting buttons as set.fmtbuttons says, at once if possible */
static void show_format(void)
{
    gui_set_checked(CMD_FORMATBAR, set.fmtbuttons);
    if (!gui_show_format(set.fmtbuttons))
        gui_status((CONST_STRPTR)S(MSG_TOOLBAR_NEXT_START));
}

static void apply_settings(const struct Settings *n)
{
    struct Settings old = set;
    BOOL colours = memcmp(old.colours, n->colours, sizeof(old.colours)) != 0;

    set = *n;
    if (colours) highlight_colours(gui.screen, set.colours);
    if (colours || old.highlight != set.highlight) {
        gui_set_checked(CMD_HIGHLIGHT, set.highlight);
        set_highlighting();
    }
    gui_set_checked(CMD_AUTOREFRESH, set.autorefresh);
    gui_set_checked(CMD_SYNCSCROLL, set.syncscroll);
    if (old.syncscroll != set.syncscroll) sync_reset();
    if (old.linenumbers != set.linenumbers) {
        gui_set_checked(CMD_LINENUMBERS, set.linenumbers);
        set_linenumbers();
    }
    if (strcmp(old.dialect, set.dialect) || strcmp(old.charset, set.charset) ||
        strcmp(old.template, set.template)) {
        setup_conversion();
        update_preview();
    }
    if (old.ttf != set.ttf || strcmp(old.fontset, set.fontset) || old.fontsize != set.fontsize)
        gui_status((CONST_STRPTR)S(MSG_FONTS_NEXT_START));
    else if (old.tbmode != set.tbmode || old.tbframes != set.tbframes)
        gui_status((CONST_STRPTR)S(MSG_TOOLBAR_NEXT_START));
    if (old.fmtbuttons != set.fmtbuttons) show_format();
}

static void edit_settings(void)
{
    struct Settings n = set;
    int r = prefs_dialog(&n);

    if (r == PREFS_CANCEL) return;
    apply_settings(&n);
    if (r == PREFS_SAVE) {
        if (settings_save_icon(&set, (CONST_STRPTR)iconname))
            gui_status((CONST_STRPTR)S(MSG_SETTINGS_SAVED));
        else
            dos_error((CONST_STRPTR)S(MSG_SETTINGS_SAVE_FAILED), (CONST_STRPTR)iconname);
    }
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
    case CMD_AUTOREFRESH:
        if ((set.autorefresh = gui_checked(CMD_AUTOREFRESH))) update_preview();
        break;
    case CMD_SYNCSCROLL:
        set.syncscroll = gui_checked(CMD_SYNCSCROLL);
        sync_reset();
        break;
    case CMD_HIGHLIGHT:
        set.highlight = gui_checked(CMD_HIGHLIGHT);
        set_highlighting();
        break;
    case CMD_LINENUMBERS:
        set.linenumbers = gui_checked(CMD_LINENUMBERS);
        set_linenumbers();
        break;
    case CMD_SETTINGS:    edit_settings(); break;
    case CMD_ICONIFY:     find_cleanup(FALSE); gui_iconify(); break;
    case CMD_FIND:        find_open(); break;
    case CMD_FINDNEXT:    find_next(); break;
    case CMD_COPYPREVIEW: copy_preview(); break;
    case CMD_FORMATBAR:
        set.fmtbuttons = gui_checked(CMD_FORMATBAR);
        show_format();
        break;
    default:
        if (cmd >= CMD_BOLD && cmd <= CMD_QUOTE) format_apply((int)(cmd - CMD_BOLD));
        break;
    }
    return FALSE;
}

/* about ten times a second while the window is active */
static void tick(void)
{
    ULONG x = 0, y = 0;

    if (poll_changes()) pending = REFRESH_DELAY;
    else if (pending && --pending == 0 && set.autorefresh) update_preview();
    if (set.syncscroll) sync_poll();
    update_tools();

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

static void copy_str(char *dst, ULONG size, CONST_STRPTR src)
{
    strncpy(dst, (const char *)src, size - 1);
    dst[size - 1] = 0;
}

/* the program icon holds the settings: PROGDIR:<program name> */
static void set_iconname(CONST_STRPTR program)
{
    strcpy(iconname, "PROGDIR:");
    AddPart((STRPTR)iconname, FilePart((STRPTR)program), sizeof(iconname));
}

/* Workbench start: tool types of Kickdown.info, then of a project icon */
static void wb_options(struct WBStartup *wbs)
{
    struct WBArg *wa = wbs->sm_ArgList;
    struct DiskObject *dob;
    BPTR old;

    set_iconname(wa[0].wa_Name);
    settings_load_icon(&set, (CONST_STRPTR)iconname);
    if (wbs->sm_NumArgs > 1 && wa[1].wa_Lock) {
        old = CurrentDir(wa[1].wa_Lock);
        if ((dob = GetDiskObject(wa[1].wa_Name))) {
            settings_from_tooltypes(&set, (CONST_STRPTR *)dob->do_ToolTypes, wa[1].wa_Lock);
            FreeDiskObject(dob);
        }
        CurrentDir(old);
        if (NameFromLock(wa[1].wa_Lock, (STRPTR)startfile, sizeof(startfile)))
            AddPart((STRPTR)startfile, wa[1].wa_Name, sizeof(startfile));
    }
}

/* Shell start: tool types of the program icon, then the arguments */
static BOOL shell_options(void)
{
    enum { A_FILE, A_TEMPLATE, A_CHARSET, A_DIALECT, A_TTF, A_FONTSET, A_SIZE,
           A_NOAUTOREFRESH, A_NOSYNC, A_NOHIGHLIGHT, A_LINENUMBERS, A_COUNT };
    LONG args[A_COUNT] = { 0 };
    char program[PATHLEN];
    struct RDArgs *rda;

    if (!GetProgramName((STRPTR)program, sizeof(program))) strcpy(program, APPNAME);
    set_iconname((CONST_STRPTR)program);
    if (IconBase) settings_load_icon(&set, (CONST_STRPTR)iconname);

    if (!(rda = ReadArgs((STRPTR)"FILE,TEMPLATE/K,CHARSET/K,DIALECT/K,TTF/S,FONTSET/K,SIZE/K/N,"
                         "NOAUTOREFRESH/S,NOSYNC/S,NOHIGHLIGHT/S,LINENUMBERS/S", args, NULL))) {
        PrintFault(IoErr(), (STRPTR)APPNAME);
        return FALSE;
    }
    if (args[A_FILE]) copy_str(startfile, sizeof(startfile), (CONST_STRPTR)args[A_FILE]);
    if (args[A_TEMPLATE]) copy_str(set.template, sizeof(set.template), (CONST_STRPTR)args[A_TEMPLATE]);
    if (args[A_CHARSET]) copy_str(set.charset, sizeof(set.charset), (CONST_STRPTR)args[A_CHARSET]);
    if (args[A_DIALECT]) copy_str(set.dialect, sizeof(set.dialect), (CONST_STRPTR)args[A_DIALECT]);
    if (args[A_TTF]) set.ttf = TRUE;
    if (args[A_FONTSET]) copy_str(set.fontset, sizeof(set.fontset), (CONST_STRPTR)args[A_FONTSET]);
    if (args[A_SIZE]) set.fontsize = *(LONG *)args[A_SIZE];
    if (args[A_NOAUTOREFRESH]) set.autorefresh = FALSE;
    if (args[A_NOSYNC]) set.syncscroll = FALSE;
    if (args[A_NOHIGHLIGHT]) set.highlight = FALSE;
    if (args[A_LINENUMBERS]) set.linenumbers = TRUE;
    FreeArgs(rda);
    return TRUE;
}

static struct Library *open_class(CONST_STRPTR name, ULONG ver)
{
    struct Library *base = OpenLibrary((STRPTR)name, ver);
    if (!base) message((CONST_STRPTR)S(MSG_CLASS_MISSING), name);
    splash_step();
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
    message((CONST_STRPTR)S(MSG_HTML_MISSING), name);
    return NULL;
}

int main(void)
{
    struct MsgPort *appport = NULL;
    ULONG sigmask = 0, result, appsig = 0;
    UWORD code;
    BOOL done = FALSE;
    int rc = RETURN_FAIL;

    locale_open();                      /* first: also the start messages are translated */
    settings_default(&set);
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (_WBenchMsg) {
        if (IconBase) wb_options(_WBenchMsg);
    } else if (!shell_options()) goto out;

    /* steps of the progress bar: the classes below, html.gadget, the
     * toolbar buttons and the window                                */
    if (set.splash) splash_open((CONST_STRPTR)iconname, (CONST_STRPTR)APPNAME,
                                (CONST_STRPTR)VERSION_TEXT, 8 + 1 + gui_tool_count() + 1);
    splash_status((CONST_STRPTR)S(MSG_SPLASH_CLASSES));
    if (!(AslBase = open_class((CONST_STRPTR)"asl.library", 39)) ||
        !(WindowBase = open_class((CONST_STRPTR)"window.class", 44)) ||
        !(LayoutBase = open_class((CONST_STRPTR)"gadgets/layout.gadget", 44)) ||
        !(ButtonBase = open_class((CONST_STRPTR)"gadgets/button.gadget", 44)) ||
        !(ScrollerBase = open_class((CONST_STRPTR)"gadgets/scroller.gadget", 44)) ||
        !(SpeedBarBase = open_class((CONST_STRPTR)"gadgets/speedbar.gadget", 44)) ||
        !(BitMapBase = open_class((CONST_STRPTR)"images/bitmap.image", 44)) ||
        !(TextFieldBase = open_class((CONST_STRPTR)"gadgets/texteditor.gadget", 45)) ||
        (splash_status((CONST_STRPTR)S(MSG_SPLASH_PREVIEW)), !(HTMLBase = open_html(set.ttf))))
        goto out;
    splash_step();

    setup_conversion();

    appport = CreateMsgPort();
    if (!gui_open(HTML_GetClass(), appport, appport ? &apphook : NULL, &set)) {
        message((CONST_STRPTR)S(MSG_NO_WINDOW), NULL);
        goto out;
    }
    splash_close();
    /* the program icon stands for the iconified window */
    if (IconBase) {
        struct DiskObject *dob = GetDiskObject((STRPTR)iconname);
        if (dob) {
            dob->do_CurrentX = dob->do_CurrentY = NO_ICON_POSITION;
            gui_set_icon(dob);
        }
    }
    update_title();

    if (startfile[0]) load_document((CONST_STRPTR)startfile);
    else update_preview();

    if (appport) appsig = 1UL << appport->mp_SigBit;
    while (!done) {
        ULONG sig;
        /* after iconifying the window has a new port: ask every time */
        GetAttr(WINDOW_SigMask, gui.winobj, &sigmask);
        sig = Wait(sigmask | appsig | find_sigmask() | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((result = DoMethod(gui.winobj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                done = check_save();
                break;
            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                case GID_TOOLBAR:
                case GID_TOOLBAR2: done = command(code); break;
                case GID_OVERFLOW: {
                    ULONG cmd = gui_overflow_cmd(code);
                    if (cmd) done = command(cmd);
                    break;
                }
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
            case WMHI_ICONIFY:
                find_cleanup(FALSE);
                gui_iconify();
                break;
            case WMHI_UNICONIFY:
                if (!gui_uniconify()) done = TRUE;
                else sync_reset();
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
        if (!done) find_handle();
        if (dropped[0] && !done) {
            char name[PATHLEN];
            strcpy(name, dropped);
            dropped[0] = 0;
            /* dropped on the AppIcon: open the window first */
            if (!gui_uniconify()) break;
            open_document((CONST_STRPTR)name);
        }
    }
    rc = RETURN_OK;

out:
    splash_close();
    find_cleanup(TRUE);
    gui_close();
    sync_free();
    prefs_cleanup();
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
    locale_close();                     /* last: menus and gadgets used the strings */
    (void)version;
    return rc;
}
