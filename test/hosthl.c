/*
 * hosthl - host test of the syntax highlighting (highlight.c)
 *
 *   hosthl <file.md>
 *
 * Prints every line and below it the formatting of each character:
 *   colour row: H heading, C code, Q quote, M marker, L link, U URL,
 *               T HTML, . none
 *   style row:  b bold, i italic, B bold italic, _ underline only, . none
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MDEDIT_H                    /* highlight.c must not see the real one */

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef void *APTR;
typedef unsigned char *STRPTR;
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0UL

#define TBSTYLE_UNDERLINE   0x0001
#define TBSTYLE_BOLD        0x0002
#define TBSTYLE_ITALIC      0x0004
#define TBSTYLE_SETCOLOR    0x0008

struct HighlightMessage { ULONG Version; STRPTR Text; ULONG StatusOfPrevBlock; };
struct Hook { struct { void *a, *b; } h_MinNode; APTR h_Entry, h_SubEntry, h_Data; };
struct Library { UWORD lib_Version; };
struct ColorMap;
struct Screen { struct { struct ColorMap *ColorMap; } ViewPort; };
struct DrawInfo { UWORD *dri_Pens; };
enum { BACKGROUNDPEN, TEXTPEN, OBP_Precision, PRECISION_IMAGE };

static struct Library tfbase = { 47 };
static struct Library *TextFieldBase = &tfbase;
static ULONG HookEntry(void) { return 0; }
static struct Screen screen;
static struct DrawInfo *GetScreenDrawInfo(struct Screen *s) { (void)s; return NULL; }
static void FreeScreenDrawInfo(struct Screen *s, struct DrawInfo *d) { (void)s; (void)d; }
/* pens 10, 11, ... in the order of colours[] */
static LONG nextpen = 10;
static LONG ObtainBestPen(struct ColorMap *c, ULONG r, ULONG g, ULONG b, ULONG t, ULONG v, ULONG e)
{ (void)c; (void)r; (void)g; (void)b; (void)t; (void)v; (void)e; return nextpen++; }
static void ReleasePen(struct ColorMap *c, LONG p) { (void)c; (void)p; }

static ULONG fcol[4096], fsty[4096];
static ULONG linelen;

static void HighlightSetFormat(APTR o, ULONG pos, ULONG end, ULONG style)
{
    ULONG i;
    if (!o || end > linelen || pos >= end) {
        fprintf(stderr, "bad range %lu..%lu (length %lu)\n", pos, end, linelen);
        exit(1);
    }
    for (i = pos; i < end; i++) {
        if (style & TBSTYLE_SETCOLOR) fcol[i] = (style >> 8 & 0xff) - 9;
        fsty[i] |= style & (TBSTYLE_BOLD | TBSTYLE_ITALIC | TBSTYLE_UNDERLINE);
    }
}

#include "../src/highlight.c"

int main(int argc, char **argv)
{
    static char line[4096];
    FILE *f;
    ULONG status = 0, i;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) return 1;
    highlight_colours(&screen);
    while (fgets(line, sizeof(line), f)) {
        struct HighlightMessage msg = { 0, (STRPTR)line, status };
        for (linelen = 0; line[linelen] && line[linelen] != '\n'; linelen++) ;
        memset(fcol, 0, sizeof(fcol));
        memset(fsty, 0, sizeof(fsty));
        status = hook_func(&hook, (APTR)1, &msg);
        printf("%.*s\n", (int)linelen, line);
        for (i = 0; i < linelen; i++) putchar(".HCQMLUT"[fcol[i]]);
        putchar('\n');
        for (i = 0; i < linelen; i++) {
            ULONG s = fsty[i] & (TBSTYLE_BOLD | TBSTYLE_ITALIC);
            putchar(s == (TBSTYLE_BOLD | TBSTYLE_ITALIC) ? 'B' : s == TBSTYLE_BOLD ? 'b' :
                    s == TBSTYLE_ITALIC ? 'i' : fsty[i] & TBSTYLE_UNDERLINE ? '_' : '.');
        }
        printf("\n%s\n", status ? "  [in code fence]" : "");
    }
    fclose(f);
    return 0;
}
