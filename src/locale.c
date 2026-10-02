/*
 * MDEdit - translations through locale.library
 *
 * The built-in strings are English (src/strings.h, made from
 * catalogs/MDEdit.cd). OpenCatalog() looks for MDEdit.catalog in
 * PROGDIR:Catalogs/<language>/ and LOCALE:Catalogs/<language>/ for the
 * languages the user prefers; for English (or without a catalog or
 * locale.library) the built-in strings are used.
 *
 * The strings stay valid until locale_close(), menus and gadgets may
 * keep pointers to them.
 *
 * Copyright (c) 2026 André Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <libraries/locale.h>

#include <proto/exec.h>
#include <proto/locale.h>

#define MDEDIT_STRINGS
#include "strings.h"
#include "mdedit.h"

/* catalog version: raise it together with the version in the .ct files
 * when strings are removed or reordered                                */
#define CATALOG_VERSION 1

/* initialised explicitly, see mdedit.c */
struct LocaleBase *LocaleBase = NULL;
static struct Catalog *catalog;

void locale_open(void)
{
    if (!(LocaleBase = (struct LocaleBase *)OpenLibrary((STRPTR)"locale.library", 38))) return;
    catalog = OpenCatalog(NULL, (STRPTR)"MDEdit.catalog",
                          OC_BuiltInLanguage, (ULONG)"english",
                          OC_Version,         CATALOG_VERSION,
                          TAG_DONE);
}

void locale_close(void)
{
    if (LocaleBase) {
        CloseCatalog(catalog);              /* NULL is fine */
        CloseLibrary((struct Library *)LocaleBase);
    }
    catalog = NULL;
    LocaleBase = NULL;
}

/* the string 'id' in the user's language */
const char *S(LONG id)
{
    const char *builtin = id >= 0 && id < MSG_COUNT ? msg_builtin[id] : "";
    if (!catalog) return builtin;
    return (const char *)GetCatalogStr(catalog, id, (STRPTR)builtin);
}
