/*
 * hostset - host test of the settings in tool types (settings.c)
 *
 * Writes settings into a fake icon with comments and a NewIcons block,
 * prints the tool types and reads them back. Exit code 1 on a failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define KICKDOWN_H

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef void *APTR;
typedef long BPTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
#define TRUE 1
#define FALSE 0
#define MEMF_ANY 0
#define MEMF_CLEAR 0x10000
#define ACCESS_READ (-2)
#define WBTOOL 3

struct Library { UWORD lib_Version; };
struct DiskObject { STRPTR *do_ToolTypes; };
static struct Library iconbase = { 44 };
static struct Library *IconBase = &iconbase;

static APTR AllocVec(ULONG size, ULONG flags) { return flags & MEMF_CLEAR ? calloc(1, size) : malloc(size); }
static void FreeVec(APTR p) { free(p); }
static BPTR Lock(STRPTR n, LONG m) { (void)n; (void)m; return 1; }
static void UnLock(BPTR l) { (void)l; }
static BOOL NameFromLock(BPTR l, STRPTR buf, LONG len) { (void)l; snprintf((char *)buf, len, "Work:Kickdown"); return TRUE; }
static STRPTR FilePart(STRPTR p)
{
    char *s = strrchr((char *)p, '/'), *c = strrchr((char *)p, ':');
    if (s && (!c || s > c)) return (STRPTR)s + 1;
    return c ? (STRPTR)c + 1 : p;
}
static STRPTR PathPart(STRPTR p)
{
    STRPTR f = FilePart(p);
    return f > p && f[-1] == '/' ? f - 1 : f;
}
static BOOL AddPart(STRPTR dir, CONST_STRPTR file, ULONG size)
{
    size_t n = strlen((char *)dir);
    if (n && dir[n - 1] != ':' && dir[n - 1] != '/') strncat((char *)dir, "/", size - n - 1);
    strncat((char *)dir, (const char *)file, size - strlen((char *)dir) - 1);
    return TRUE;
}
static LONG StrToLong(CONST_STRPTR s, LONG *v)
{
    char *e;
    *v = strtol((const char *)s, &e, 10);
    return e == (char *)s ? -1 : (LONG)(e - (char *)s);
}

/* FindToolType: "KEY" or "KEY=value", case is ignored; "(KEY)" is not KEY */
static STRPTR FindToolType(CONST_STRPTR *tt, STRPTR key)
{
    size_t n = strlen((char *)key);
    for (; tt && *tt; tt++) {
        if (!strncasecmp((const char *)*tt, (const char *)key, n)) {
            if ((*tt)[n] == '=') return (STRPTR)*tt + n + 1;
            if (!(*tt)[n]) return (STRPTR)*tt + n;
        }
    }
    return NULL;
}

/* the fake icon */
static char *icon[64];
static int nicon;
static struct DiskObject *GetDiskObject(STRPTR name)
{
    struct DiskObject *d = calloc(1, sizeof(*d));
    (void)name;
    d->do_ToolTypes = calloc(nicon + 1, sizeof(STRPTR));
    memcpy(d->do_ToolTypes, icon, nicon * sizeof(STRPTR));
    return d;
}
static struct DiskObject *GetDiskObjectNew(STRPTR n) { return GetDiskObject(n); }
static struct DiskObject *GetDefDiskObject(LONG t) { (void)t; return GetDiskObject(NULL); }
static void FreeDiskObject(struct DiskObject *d) { free(d->do_ToolTypes); free(d); }
static BOOL PutDiskObject(STRPTR name, struct DiskObject *d)
{
    static char store[64][600];
    static char tmp[64][600];
    int i, n;
    (void)name;
    /* the new list may point into the old icon: copy via tmp */
    for (n = 0; d->do_ToolTypes[n]; n++) strcpy(tmp[n], (const char *)d->do_ToolTypes[n]);
    for (i = 0; i < n; i++) {
        strcpy(store[i], tmp[i]);
        icon[i] = store[i];
    }
    nicon = n;
    return TRUE;
}

#include "../src/settings.c"

static int failed;

static void show(const char *title)
{
    int i;
    printf("%s\n", title);
    for (i = 0; i < nicon; i++) printf("  %s\n", icon[i]);
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failed = 1;
}

int main(void)
{
    static char *initial[] = { "(TEMPLATE=Template.html)", "(DIALECT=GitHub)", "(TTF)",
                               "(NOSYNC)", "DONOTTOUCH=1", " ",
                               "*** DON'T EDIT THE FOLLOWING LINES!! ***", "IM1=abc", "IM2=def" };
    struct Settings s, r;
    int i;

    nicon = sizeof(initial) / sizeof(initial[0]);
    memcpy(icon, initial, sizeof(initial));
    show("before:");

    settings_default(&s);
    s.ttf = TRUE;
    s.linenumbers = TRUE;
    s.fontsize = 14;
    strcpy(s.template, "Work:Kickdown/My page.html");
    s.colours[C_HEADING] = 0x123456;
    s.winwidth = 800;
    s.winheight = 560;
    s.winleft = 0;                  /* 0 is a position, not "unset" */
    s.wintop = 24;
    s.tbmode = TBMODE_TEXT;
    s.tbframes = TRUE;
    s.fmtbuttons = FALSE;
    s.splash = FALSE;
    s.fitimages = TRUE;
    s.prmode = PRMODE_PDF;
    s.pslevel = 1;
    s.prdest = PRDEST_DEVICE;
    strcpy(s.prdevice, "PAR:");
    s.paper = PAPER_LETTER;
    s.margins[0] = 15; s.margins[1] = 17; s.margins[2] = 15; s.margins[3] = 25;
    s.prserif = TRUE;
    s.prsize = 11;
    s.prpagenumbers = FALSE;
    s.prdpi = 600;
    check(settings_save_icon(&s, (CONST_STRPTR)"PROGDIR:Kickdown"), "save");
    show("after saving (ttf, line numbers, size 14, template, heading colour):");

    check(!strcmp(icon[nicon - 2], "IM1=abc") && !strcmp(icon[nicon - 1], "IM2=def") &&
          !strcmp(icon[nicon - 4], " "), "NewIcons block stays at the end");
    for (i = 0; i < nicon && strcmp(icon[i], "DONOTTOUCH=1"); i++) ;
    check(i < nicon, "foreign tool type kept");
    check(!strcmp(icon[2], "TTF") && !strcmp(icon[3], "(NOSYNC)") && !strcmp(icon[1], "(DIALECT=GitHub)"),
          "own entries replaced in place, defaults stay disabled");

    settings_default(&r);
    settings_load_icon(&r, (CONST_STRPTR)"PROGDIR:Kickdown");
    check(!memcmp(&r, &s, sizeof(r)), "read back gives the same settings");

    s.ttf = FALSE;
    s.fontsize = 0;
    s.colours[C_HEADING] = default_colours[C_HEADING];
    s.syncscroll = FALSE;
    s.tbmode = TBMODE_BOTH;
    s.tbframes = FALSE;
    check(settings_save_icon(&s, (CONST_STRPTR)"PROGDIR:Kickdown"), "save again");
    show("after switching ttf off, size 0, default heading colour, nosync:");
    check(!strcmp(icon[2], "(TTF)") && !strcmp(icon[3], "NOSYNC"), "TTF disabled, NOSYNC active");
    for (i = 0; i < nicon && strncmp(icon[i], "(SIZE", 5); i++) ;
    check(i < nicon && !strcmp(icon[i], "(SIZE=14)"), "SIZE disabled with its old value");
    for (i = 0; i < nicon && strncmp(icon[i], "TOOLBAR=", 8); i++) ;
    check(i < nicon && !strcmp(icon[i], "TOOLBAR=BOTH"), "TOOLBAR=BOTH");
    for (i = 0; i < nicon && strcmp(icon[i], "(TOOLBARFRAMES)"); i++) ;
    check(i < nicon, "TOOLBARFRAMES disabled");
    for (i = 0; i < nicon && strcmp(icon[i], "NOFORMATBUTTONS"); i++) ;
    check(i < nicon, "NOFORMATBUTTONS kept");
    for (i = 0; i < nicon && strcmp(icon[i], "NOSPLASH"); i++) ;
    check(i < nicon, "NOSPLASH kept");
    for (i = 0; i < nicon && strcmp(icon[i], "MARGINS=15,17,15,25"); i++) ;
    check(i < nicon, "MARGINS kept");
    for (i = 0; i < nicon && strcmp(icon[i], "PAPER=LETTER"); i++) ;
    check(i < nicon, "PAPER kept");
    for (i = 0; i < nicon && strcmp(icon[i], "FITIMAGES"); i++) ;
    check(i < nicon, "FITIMAGES kept");
    for (i = 0; i < nicon && strcmp(icon[i], "PS_LEVEL=1"); i++) ;
    check(i < nicon, "PS_LEVEL=1 kept");
    settings_default(&r);
    settings_load_icon(&r, (CONST_STRPTR)"PROGDIR:Kickdown");
    check(!memcmp(&r, &s, sizeof(r)), "read back gives the same settings");
    return failed;
}
