/*
 * hostsync - host test of the scroll synchronisation (sync.c)
 *
 *   hostsync <file.md>
 *
 * Gadgets are replaced by stubs: the editor has a given number of rows,
 * the preview puts heading n at y = 100 * n. Prints the fixed points and
 * the mapping in both directions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdconv.h"
#include "hoststubs.h"

#include "../src/sync.c"

int main(int argc, char **argv)
{
    struct MDConvOptions opt;
    char *md, *html;
    FILE *f;
    long size;
    ULONG i, row;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) return 1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    md = calloc(1, size + 1);
    if (fread(md, 1, size, f) != (size_t)size) return 1;
    fclose(f);

    memset(&opt, 0, sizeof(opt));
    opt.flags = mdconv_default_flags();
    html = mdconv_html(md, size, &opt, NULL);

    stub_html = html;
    stub_heading = find_ids(html, NULL, 0);
    stub_entries = argc > 2 ? atoi(argv[2]) : 0;   /* 0: one row per line */
    sync_rebuild((CONST_STRPTR)md, html);
    printf("lines %lu, editor rows %lu, headings %lu, ids %lu\n", (unsigned long)nlines,
           (unsigned long)cum[nlines], (unsigned long)find_headings(md, NULL, 0),
           (unsigned long)find_ids(html, NULL, 0));
    for (i = 0; i < nknots; i++)
        printf("knot line %ld.%03ld y %ld\n", (long)(knots[i].line / FRAC),
               (long)(knots[i].line % FRAC * 1000 / FRAC), (long)knots[i].y);
    for (row = 0; row <= cum[nlines]; row += 5) {
        LONG line = row_to_line(row), y = line_to_y(line);
        printf("row %3lu -> line %3ld.%03ld -> y %5ld -> line %3ld.%03ld -> row %3lu\n",
               (unsigned long)row, (long)(line / FRAC), (long)(line % FRAC * 1000 / FRAC), (long)y,
               (long)(y_to_line(y) / FRAC), (long)(y_to_line(y) % FRAC * 1000 / FRAC),
               (unsigned long)line_to_row(y_to_line(y)));
    }
    sync_free();
    mdconv_free(html);
    free(md);
    return 0;
}
