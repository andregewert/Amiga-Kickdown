/*
 * mdtohtml - converts a Markdown file to HTML
 *
 *   mdtohtml [FROM] <file.md> [TO <file.html>] [TEMPLATE <file>]
 *            [CHARSET <name>] [TITLE <text>] [DIALECT GitHub|CommonMark]
 *
 * Without FROM the Markdown text is read from standard input, without TO
 * the HTML page goes to standard output.
 *
 * Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>
 * Released under the MIT License, see LICENSE.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#include "fileio.h"
#include "mdconv.h"

static const char version[] = "$VER: mdtohtml 1.0 (02.10.2026)";

/* minimum stack, libnix swaps to a bigger one at startup if needed */
unsigned long __stack = 32768;

#define TEMPLATE "FROM=FILE,TO=OUTFILE/K,TEMPLATE/K,CHARSET=ENCODING/K,TITLE/K,DIALECT=MODE/K"
enum { ARG_FROM, ARG_TO, ARG_TEMPLATE, ARG_CHARSET, ARG_TITLE, ARG_DIALECT, ARG_COUNT };

/* file name without path and extension as fallback title */
static void base_name(CONST_STRPTR path, char *out, ULONG size)
{
    char *dot;
    strncpy(out, (const char *)FilePart((STRPTR)path), size - 1);
    out[size - 1] = 0;
    if ((dot = strrchr(out, '.')) && dot != out) *dot = 0;
}

int main(void)
{
    LONG args[ARG_COUNT] = { 0 };
    struct RDArgs *rda;
    struct MDConvOptions opt;
    STRPTR md = NULL, tmpl = NULL;
    char *html = NULL;
    char title[108];
    ULONG mdlen = 0;
    size_t htmllen = 0;
    int rc = RETURN_FAIL;

    if (!(rda = ReadArgs((STRPTR)TEMPLATE, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)"mdtohtml");
        return RETURN_FAIL;
    }

    memset(&opt, 0, sizeof(opt));
    opt.flags = mdconv_default_flags();
    if (args[ARG_DIALECT] && !mdconv_dialect((const char *)args[ARG_DIALECT], &opt.flags)) {
        Printf((STRPTR)"mdtohtml: unknown dialect \"%s\" (GitHub, CommonMark)\n", args[ARG_DIALECT]);
        goto out;
    }
    opt.title = (const char *)args[ARG_TITLE];
    opt.charset = (const char *)args[ARG_CHARSET];

    if (args[ARG_TEMPLATE]) {
        if (!(tmpl = read_file((CONST_STRPTR)args[ARG_TEMPLATE], NULL))) {
            PrintFault(IoErr(), (STRPTR)args[ARG_TEMPLATE]);
            goto out;
        }
        opt.tmpl = (const char *)tmpl;
    }

    if (args[ARG_FROM]) {
        if (!(md = read_file((CONST_STRPTR)args[ARG_FROM], &mdlen))) {
            PrintFault(IoErr(), (STRPTR)args[ARG_FROM]);
            goto out;
        }
        base_name((CONST_STRPTR)args[ARG_FROM], title, sizeof(title));
        opt.fallback_title = title;
    } else if (!(md = read_fh(Input(), &mdlen))) {
        PrintFault(IoErr(), (STRPTR)"mdtohtml");
        goto out;
    }

    if (!(html = mdconv_html((const char *)md, mdlen, &opt, &htmllen))) {
        PrintFault(ERROR_NO_FREE_STORE, (STRPTR)"mdtohtml");
        goto out;
    }

    if (args[ARG_TO]) {
        if (!write_file((CONST_STRPTR)args[ARG_TO], (CONST_STRPTR)html, htmllen)) {
            PrintFault(IoErr(), (STRPTR)args[ARG_TO]);
            rc = RETURN_ERROR;
            goto out;
        }
    } else if (!write_fh(Output(), (CONST_STRPTR)html, htmllen)) {
        PrintFault(IoErr(), (STRPTR)"mdtohtml");
        rc = RETURN_ERROR;
        goto out;
    }
    rc = RETURN_OK;

out:
    if (html) mdconv_free(html);
    if (md) FreeVec(md);
    if (tmpl) FreeVec(tmpl);
    FreeArgs(rda);
    (void)version;
    return rc;
}
