/*
 * hostconv - host test of mdconv (make check)
 *
 *   hostconv <file.md> [template.html] [dialect]
 *
 * Prints the HTML page that mdtohtml and MDEdit would produce.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdconv.h"

static char *slurp(const char *name, size_t *len)
{
    FILE *f = fopen(name, "rb");
    char *data;
    long size;

    if (!f) {
        perror(name);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size + 1);
    if (!data || fread(data, 1, size, f) != (size_t)size) {
        perror(name);
        exit(1);
    }
    fclose(f);
    data[size] = 0;
    if (len) *len = size;
    return data;
}

int main(int argc, char **argv)
{
    struct MDConvOptions opt;
    char *md, *tmpl = NULL, *html;
    size_t len, htmllen;

    if (argc < 2) {
        fprintf(stderr, "usage: %s file.md [template.html] [dialect]\n", argv[0]);
        return 1;
    }
    memset(&opt, 0, sizeof(opt));
    opt.flags = mdconv_default_flags();
    if (argc > 3 && !mdconv_dialect(argv[3], &opt.flags)) {
        fprintf(stderr, "unknown dialect %s\n", argv[3]);
        return 1;
    }
    md = slurp(argv[1], &len);
    if (argc > 2) opt.tmpl = tmpl = slurp(argv[2], NULL);
    opt.fallback_title = "Untitled";

    if (!(html = mdconv_html(md, len, &opt, &htmllen))) {
        fprintf(stderr, "conversion failed\n");
        return 1;
    }
    fwrite(html, 1, htmllen, stdout);
    mdconv_free(html);
    free(md);
    free(tmpl);
    return 0;
}
