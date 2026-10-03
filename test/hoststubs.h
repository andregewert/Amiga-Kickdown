/*
 * hoststubs.h - minimal Amiga types and gadget stubs for hostsync
 */
#ifndef HOSTSTUBS_H
#define HOSTSTUBS_H
#define KICKDOWN_H                /* sync.c must not see the real one */

#include <stdlib.h>
#include <string.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef short BOOL;
typedef void *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef void Object;
struct Gadget;
struct Window;
#define TRUE 1
#define FALSE 0
#define NULL_TAG 0
#define TAG_DONE 0UL
#define MEMF_ANY 0
#define MEMF_CLEAR 0x10000

/* attributes used by sync.c */
enum { GA_TEXTEDITOR_Prop_First = 1, GA_TEXTEDITOR_Prop_Entries, GA_TEXTEDITOR_Prop_Visible,
       HTML_Top, HTML_Total, HTML_Visible, HTML_Anchor, SCROLLER_Top };

struct GUI { Object *editor, *html, *escroll; struct Window *win; };
static struct GUI gui = { (Object *)1, (Object *)2, (Object *)3, NULL };

static ULONG stub_entries;          /* editor rows, 0: as many as lines */
static ULONG stub_top, stub_heading;
static const char *stub_html;

static APTR AllocVec(ULONG size, ULONG flags)
{
    return flags & MEMF_CLEAR ? calloc(1, size) : malloc(size);
}
static void FreeVec(APTR p) { free(p); }

/* anchors: the n-th heading id is at y = 100 * (n + 1) */
static void SetAttrs(Object *o, ULONG tag, ULONG data, ULONG end)
{
    const char *p = stub_html;
    ULONG n = 0;
    (void)o; (void)end;
    if (tag != HTML_Anchor) return;
    while ((p = strstr(p, " id=\""))) {
        p += 5;
        n++;
        if (!strncmp(p, (const char *)data, strlen((const char *)data)) &&
            p[strlen((const char *)data)] == '"') {
            stub_top = 100 * n;
            return;
        }
    }
}
static void SetGadgetAttrs(struct Gadget *g, struct Window *w, void *r, ULONG tag, ULONG data, ULONG end)
{
    (void)g; (void)w; (void)r; (void)end;
    if (tag == HTML_Top) stub_top = data;
}
static ULONG GetAttr(ULONG tag, Object *o, ULONG *store)
{
    (void)o;
    switch (tag) {
    case GA_TEXTEDITOR_Prop_Entries: *store = stub_entries; break;
    case HTML_Top:   *store = stub_top; break;
    case HTML_Total: *store = 100 * (stub_heading + 2); break;
    default: *store = 0;
    }
    return 1;
}
#endif
