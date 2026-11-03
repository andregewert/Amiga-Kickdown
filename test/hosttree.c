/*
 * hosttree - host test of mdtree (make check)
 *
 *   hosttree <empty drawer to work in>
 *
 * The mdfs_* functions with POSIX calls, the helpers of mdtree and whole
 * runs over a small tree.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mdtree.h"

static int failures, checks;
static char messages[8192];

#define CHECK(cond) do { \
        checks++; \
        if (!(cond)) { \
            printf("FAIL line %d: %s\n", __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* the file system */

void *mdfs_open_dir(const char *path)
{
    return opendir(path);
}

int mdfs_next(void *dir, char *name, int *is_dir)
{
    struct dirent *e;

    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        strncpy(name, e->d_name, MDFS_NAMEMAX - 1);
        name[MDFS_NAMEMAX - 1] = 0;
        *is_dir = e->d_type == DT_DIR;
        return 1;
    }
    return 0;
}

void mdfs_close_dir(void *dir)
{
    closedir(dir);
}

int mdfs_exists(const char *path)
{
    struct stat st;

    if (stat(path, &st)) return MDFS_NONE;
    return S_ISDIR(st.st_mode) ? MDFS_DIR : MDFS_FILE;
}

int mdfs_make_dir(const char *path)
{
    return !mkdir(path, 0777) || mdfs_exists(path) == MDFS_DIR;
}

char *mdfs_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long size;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if ((data = malloc(size + 1)) && fread(data, 1, size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        data[size] = 0;
        if (len) *len = size;
    }
    return data;
}

void mdfs_free(char *data)
{
    free(data);
}

int mdfs_write(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f) return 0;
    ok = fwrite(data, 1, len, f) == len;
    return !fclose(f) && ok;
}

int mdfs_copy(const char *from, const char *to)
{
    size_t len = 0;
    char *data = mdfs_read(from, &len);
    int ok;

    if (!data) return 0;
    ok = mdfs_write(to, data, len);
    free(data);
    return ok;
}

int mdfs_is_inside(const char *inner, const char *outer)
{
    char i[PATH_MAX], o[PATH_MAX];
    size_t n;

    if (!realpath(inner, i) || !realpath(outer, o)) return 0;
    n = strlen(o);
    return !strncmp(i, o, n) && (i[n] == 0 || i[n] == '/');
}

void mdfs_print(const char *text)
{
    strncat(messages, text, sizeof(messages) - strlen(messages) - 1);
}

void mdfs_fault(const char *path)
{
    mdfs_print("fault: ");
    mdfs_print(path);
    mdfs_print("\n");
}

int mdfs_break(void)
{
    return 0;
}

/* helpers of the test */

static void put(const char *path, const char *text)
{
    if (!mdfs_write(path, text, strlen(text))) {
        perror(path);
        exit(1);
    }
}

static int file_has(const char *path, const char *text)
{
    char *data = mdfs_read(path, NULL);
    int found = data && strstr(data, text);

    free(data);
    return found;
}

static int rewritten(const char *url, const char *expected)
{
    char out[64] = "";

    return mdtree_rewrite_link(url, out, sizeof(out)) && !strcmp(out, expected);
}

static int target(const char *page, const char *url, const char *expected, const char *exp_anchor)
{
    char path[64], anchor[64];

    return mdtree_link_target(page, url, path, sizeof(path), anchor, sizeof(anchor)) ==
               MDTREE_LINK_LOCAL &&
           !strcmp(path, expected) && !strcmp(anchor, exp_anchor);
}

static int target_kind(const char *page, const char *url)
{
    char path[64], anchor[64];

    return mdtree_link_target(page, url, path, sizeof(path), anchor, sizeof(anchor));
}

static int parts(const char *url, const char *exp_path, const char *exp_anchor)
{
    char path[64], anchor[64];

    return mdconv_link_parts(url, path, sizeof(path), anchor, sizeof(anchor)) &&
           !strcmp(path, exp_path) && !strcmp(anchor, exp_anchor);
}

static void test_helpers(void)
{
    char out[16], small[4], anchor[4];

    CHECK(parts("a.md", "a.md", ""));
    CHECK(parts("a.md#x", "a.md", "x"));
    CHECK(parts("d/a.md#x%20y", "d/a.md", "x y"));
    CHECK(parts("./a.md", "a.md", ""));
    CHECK(parts("../a.md", "/a.md", ""));
    CHECK(parts("../../a.md", "//a.md", ""));
    CHECK(parts("d/../a.md", "d//a.md", ""));
    CHECK(parts("..", "/", ""));
    CHECK(parts("a%20b.md?q", "a b.md", ""));
    CHECK(parts("#x", "", "x"));
    CHECK(parts("", "", ""));
    CHECK(parts("a..b/.c", "a..b/.c", ""));
    CHECK(!mdconv_link_parts("long-name.md", small, sizeof(small), anchor, sizeof(anchor)));
    CHECK(!mdconv_link_parts("a#long-anchor", small, sizeof(small), anchor, sizeof(anchor)));

    CHECK(mdtree_is_markdown("a.md"));
    CHECK(mdtree_is_markdown("A.MD"));
    CHECK(mdtree_is_markdown("a.Markdown"));
    CHECK(!mdtree_is_markdown("a.md.info"));
    CHECK(!mdtree_is_markdown(".md"));
    CHECK(!mdtree_is_markdown("md"));

    CHECK(mdtree_is_skipped("a.md.info", 0));
    CHECK(mdtree_is_skipped("A.INFO", 0));
    CHECK(mdtree_is_skipped(".tinydev", 1));
    CHECK(!mdtree_is_skipped(".hidden", 0));
    CHECK(!mdtree_is_skipped("img", 1));

    CHECK(mdtree_html_name("a.md", out, sizeof(out)) && !strcmp(out, "a.html"));
    CHECK(mdtree_html_name("b.markdown", out, sizeof(out)) && !strcmp(out, "b.html"));
    CHECK(!mdtree_html_name("much-too-long-for-it.md", out, sizeof(out)));

    CHECK(rewritten("a.md", "a.html"));
    CHECK(rewritten("../d/a.MD#x", "../d/a.html#x"));
    CHECK(rewritten("a.markdown", "a.html"));
    CHECK(rewritten("file:a.md", "file:a.html"));
    CHECK(!mdtree_rewrite_link("a.png", out, sizeof(out)));
    CHECK(!mdtree_rewrite_link("#a.md", out, sizeof(out)));
    CHECK(!mdtree_rewrite_link("http://x.org/a.md", out, sizeof(out)));
    CHECK(!mdtree_rewrite_link("mailto:a@b.md", out, sizeof(out)));
    CHECK(!mdtree_rewrite_link("Help:a.md", out, sizeof(out)));

    CHECK(target("index.html", "a.html", "a.html", ""));
    CHECK(target("d/p.html", "a.html#x", "d/a.html", "x"));
    CHECK(target("d/p.html", "#x", "d/p.html", "x"));
    CHECK(target("d/e/p.html", "../../a.html", "a.html", ""));
    CHECK(target("d/e/p.html", "./.././f//a.html", "d/f/a.html", ""));
    CHECK(target("p.html", "file:d/a.html", "d/a.html", ""));
    CHECK(target("p.html", "a%20b.html#%FC", "a b.html", "\xfc"));
    CHECK(target("p.html", "a&amp;b.html", "a&b.html", ""));
    CHECK(target("p.html", "a.html?q=1", "a.html", ""));
    CHECK(target("p.html", "d/", "d", ""));
    CHECK(target_kind("p.html", "http://x.org/") == MDTREE_LINK_EXTERNAL);
    CHECK(target_kind("p.html", "mailto:a@b.c") == MDTREE_LINK_EXTERNAL);
    CHECK(target_kind("p.html", "../a.html") == MDTREE_LINK_BAD);
    CHECK(target_kind("d/p.html", "../../a.html") == MDTREE_LINK_BAD);
    CHECK(target_kind("p.html", "/a.html") == MDTREE_LINK_BAD);
    CHECK(target_kind("p.html", "a%00b.html") == MDTREE_LINK_BAD);
    CHECK(target_kind("p.html", "a-very-long-name-that-does-not-fit-the-test-s-buffer-at-all-really.html")
          == MDTREE_LINK_BAD);

    CHECK(mdtree_has_anchor("<h2 id=\"x\">", "x"));
    CHECK(mdtree_has_anchor("<a name='x'>", "x"));
    CHECK(mdtree_has_anchor("<a NAME=x>", "x"));
    CHECK(mdtree_has_anchor("<a\nid=\"x\">", "x"));
    CHECK(!mdtree_has_anchor("<h2 id=\"xy\">", "x"));
    CHECK(!mdtree_has_anchor("<h2 id=\"yx\">", "x"));
    CHECK(!mdtree_has_anchor("<a href=\"x\">", "x"));
    CHECK(!mdtree_has_anchor("<a grid=\"x\">", "x"));
    CHECK(!mdtree_has_anchor("the text x", "x"));
}

/* the tree of test_tree(): one page of everything */
static void make_source(void)
{
    static const char picture[] = "PIC\0TURE";

    mdfs_make_dir("src");
    mdfs_make_dir("src/3-project");
    mdfs_make_dir("src/3-project/deep");
    mdfs_make_dir("src/img");
    mdfs_make_dir("src/.tinydev");
    put("src/index.md",
        "# Home \xfc\n\n"
        "[edit](2-editing.md) [find](3-project/1-find.md#options) [own](#home-\xfc)\n"
        "[project](3-project/index.md) [web](http://x.org/a.md) [mail](mailto:a@b.md)\n"
        "`[code](not-a-link.md)`\n\n"
        "    <a href=\"in-a-block.md\">\n\n"
        "![pic](img/a.png) [raw](raw.html#mark)\n");
    put("src/2-editing.md", "No heading here.\n\n[back](index.md)\n");
    put("src/3-project/index.md", "# Project\n\n[find](1-find.md)\n");
    put("src/3-project/1-find.md",
        "# Find\n\n## Options\n\n[up](../index.md) [edit](../2-editing.md) [self](#options)\n");
    put("src/3-project/deep/page.md", "# Deep\n");
    put("src/raw.html", "<a name=\"mark\">raw</a> [not markdown](x.md)\n");
    if (!mdfs_write("src/img/a.png", picture, sizeof(picture))) exit(1);
    put("src/img/a.png.info", "icon");
    put("src/index.md.info", "icon");
    put("src/.tinydev/project.json", "{}");
}

static int run(const char *to)
{
    struct MDConvOptions opt;

    memset(&opt, 0, sizeof(opt));
    opt.flags = mdconv_default_flags();
    opt.tmpl = "<title>$title$</title><img src=\"$root$img/logo.png\">$body$";
    messages[0] = 0;
    return mdtree_convert("src", to, &opt);
}

static void test_tree(void)
{
    size_t len = 0;
    char *data;

    make_source();

    /* no dead links: everything well */
    CHECK(run("out") == MDTREE_OK);
    CHECK(!strcmp(messages, "5 pages, 2 files copied, 0 warnings\n"));

    CHECK(file_has("out/index.html", "<title>Home \xfc</title>"));
    CHECK(file_has("out/index.html", "<a href=\"2-editing.html\">"));
    CHECK(file_has("out/index.html", "<a href=\"3-project/1-find.html#options\">"));
    CHECK(file_has("out/index.html", "<a href=\"3-project/index.html\">"));
    CHECK(file_has("out/index.html", "<a href=\"http://x.org/a.md\">"));
    CHECK(file_has("out/index.html", "<a href=\"mailto:a@b.md\">"));
    CHECK(file_has("out/index.html", "not-a-link.md"));     /* left alone in code */
    CHECK(file_has("out/index.html", "in-a-block.md"));
    CHECK(file_has("out/index.html", "<img src=\"img/a.png\""));
    CHECK(file_has("out/2-editing.html", "<title>2-editing</title>"));
    CHECK(file_has("out/2-editing.html", "<a href=\"index.html\">"));
    CHECK(file_has("out/3-project/1-find.html", "<h2 id=\"options\">"));
    CHECK(file_has("out/3-project/1-find.html", "<a href=\"../index.html\">"));
    CHECK(file_has("out/3-project/1-find.html", "<a href=\"#options\">"));

    /* $root$ is the way up, whatever the page's drawer */
    CHECK(file_has("out/index.html", "<img src=\"img/logo.png\">"));
    CHECK(file_has("out/3-project/1-find.html", "<img src=\"../img/logo.png\">"));
    CHECK(file_has("out/3-project/deep/page.html", "<img src=\"../../img/logo.png\">"));

    /* copied as they are */
    CHECK(file_has("out/raw.html", "[not markdown](x.md)"));
    data = mdfs_read("out/img/a.png", &len);
    CHECK(data && len == 9 && !memcmp(data, "PIC\0TURE", 9));
    free(data);

    /* left out */
    CHECK(mdfs_exists("out/index.md") == MDFS_NONE);
    CHECK(mdfs_exists("out/index.md.info") == MDFS_NONE);
    CHECK(mdfs_exists("out/img/a.png.info") == MDFS_NONE);
    CHECK(mdfs_exists("out/.tinydev") == MDFS_NONE);

    /* dead links and leftovers are warnings, nothing is removed */
    put("src/dead.md",
        "# Dead\n\n[gone](gone.md) [no anchor](index.md#nowhere) [own](#nowhere)\n"
        "[outside](../x.md) [drawer](3-project/) ![pic](img/gone.png)\n");
    put("out/old.html", "old");
    put("out/old.png", "old");
    mdfs_make_dir("out/olddir");
    put("out/olddir/page.html", "old");
    put("out/kept.info", "icon");
    CHECK(run("out") == MDTREE_WARN);
    CHECK(strstr(messages, "src/dead.md: dead link \"gone.md\"\n") != NULL);
    CHECK(strstr(messages, "src/dead.md: dead anchor \"index.md#nowhere\"\n") != NULL);
    CHECK(strstr(messages, "src/dead.md: dead anchor \"#nowhere\"\n") != NULL);
    CHECK(strstr(messages, "src/dead.md: dead link \"../x.md\"\n") != NULL);
    CHECK(strstr(messages, "src/dead.md: dead link \"3-project/\"\n") != NULL);
    CHECK(strstr(messages, "src/dead.md: dead link \"img/gone.png\"\n") != NULL);
    CHECK(strstr(messages, "out/old.html: no source (left in place)\n") != NULL);
    CHECK(strstr(messages, "out/old.png: no source (left in place)\n") != NULL);
    CHECK(strstr(messages, "out/olddir: no source (left in place)\n") != NULL);
    CHECK(strstr(messages, "olddir/page") == NULL);     /* its drawer says enough */
    CHECK(strstr(messages, "kept.info") == NULL);
    CHECK(strstr(messages, "6 pages, 2 files copied, 9 warnings\n") != NULL);
    CHECK(mdfs_exists("out/old.html") == MDFS_FILE);
    CHECK(mdfs_exists("out/olddir/page.html") == MDFS_FILE);
    unlink("src/dead.md");

    /* the page of a.md would overwrite a.html */
    put("src/a.md", "# A\n");
    put("src/a.html", "A");
    CHECK(run("out2") == MDTREE_ERROR);
    CHECK(strstr(messages, "src/a.html: would be overwritten by the page of a.md\n") != NULL);
    unlink("src/a.html");
    put("src/a.markdown", "# A\n");
    CHECK(run("out3") == MDTREE_ERROR);
    CHECK(strstr(messages, "gives the same page as a.markdown\n") != NULL ||
          strstr(messages, "would be overwritten") != NULL);
    unlink("src/a.markdown");
    unlink("src/a.md");

    /* wrong drawers */
    CHECK(run("src") == MDTREE_ERROR);
    CHECK(strstr(messages, "the target lies in the source") != NULL);
    CHECK(run("src/3-project") == MDTREE_ERROR);
    CHECK(run("no/such/parent") == MDTREE_ERROR);
    messages[0] = 0;
    {
        struct MDConvOptions opt;
        memset(&opt, 0, sizeof(opt));
        CHECK(mdtree_convert("no-source", "out", &opt) == MDTREE_ERROR);
        CHECK(strstr(messages, "not a drawer") != NULL);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2 || chdir(argv[1])) {
        fprintf(stderr, "usage: hosttree <empty drawer>\n");
        return 2;
    }
    test_helpers();
    test_tree();
    if (failures) {
        printf("--- messages of the last run:\n%s", messages);
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("ok   directory trees (%d checks)\n", checks);
    return 0;
}
