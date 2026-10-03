/*
 * hostfmt - host test of the formatting commands (src/mdformat.c)
 *
 * Every case gives the text before and after; "|" in the inline results
 * marks the part to be marked ("[...]") or the cursor ("|").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdformat.h"

static int fails;

static void inl(int kind, const char *sel, const char *want)
{
    size_t m0, m1;
    char got[256], *r = mdfmt_inline(kind, sel, strlen(sel), &m0, &m1);
    if (!r) { printf("FAIL out of memory\n"); fails++; return; }
    if (m0 == m1) snprintf(got, sizeof(got), "%.*s|%s", (int)m0, r, r + m0);
    else snprintf(got, sizeof(got), "%.*s[%.*s]%s", (int)m0, r, (int)(m1 - m0), r + m0, r + m1);
    if (strcmp(got, want)) {
        printf("FAIL inline %d \"%s\": \"%s\", expected \"%s\"\n", kind, sel, got, want);
        fails++;
    }
    free(r);
}

static void lines(int kind, const char *text, const char *want)
{
    char *r = mdfmt_lines(kind, text, strlen(text));
    if (!r || strcmp(r, want)) {
        printf("FAIL lines %d \"%s\": \"%s\", expected \"%s\"\n", kind, text, r ? r : "(null)", want);
        fails++;
    }
    free(r);
}

int main(void)
{
    inl(FMT_BOLD, "", "**|**");
    inl(FMT_BOLD, "word", "**[word]**");
    inl(FMT_BOLD, "**word**", "[word]");
    inl(FMT_BOLD, "__word__", "[word]");
    inl(FMT_ITALIC, "word", "*[word]*");
    inl(FMT_ITALIC, "*word*", "[word]");
    inl(FMT_ITALIC, "**word**", "*[**word**]*");
    inl(FMT_ITALIC, "***word***", "[**word**]");
    inl(FMT_BOLD, "*word*", "**[*word*]**");
    inl(FMT_BOLD, "1. Test\n2. Test 2", "[1. **Test**\n2. **Test 2**]");
    inl(FMT_BOLD, "- one \n\n> - [ ] two", "[- **one** \n\n> - [ ] **two**]");
    inl(FMT_BOLD, "- **one**\n- two", "[- **one**\n- **two**]");
    inl(FMT_BOLD, "- **one**\n- **two**", "[- one\n- two]");
    inl(FMT_ITALIC, "## Title\ntext", "[## *Title*\n*text*]");
    inl(FMT_ITALIC, "## *Title*\n*text*", "[## Title\ntext]");
    inl(FMT_UNDERLINE, "", "<u>|</u>");
    inl(FMT_UNDERLINE, "word", "<u>[word]</u>");
    inl(FMT_UNDERLINE, "<u>word</u>", "[word]");
    inl(FMT_UNDERLINE, "<U>word</U>", "[word]");
    inl(FMT_UNDERLINE, "- one\n- two", "[- <u>one</u>\n- <u>two</u>]");
    inl(FMT_UNDERLINE, "- <u>one</u>\n- <u>two</u>", "[- one\n- two]");
    inl(FMT_CODE, "x = 1", "`[x = 1]`");
    inl(FMT_CODE, "`x`", "[x]");
    inl(FMT_CODE, "a\nb", "```\n[a\nb]\n```");
    inl(FMT_CODE, "```\na\nb\n```", "[a\nb]");
    inl(FMT_LINK, "", "[|]()");
    inl(FMT_LINK, "Amiga", "[Amiga](|)");
    inl(FMT_LINK, "https://aminet.net", "[|](https://aminet.net)");
    inl(FMT_IMAGE, "", "![|]()");
    inl(FMT_IMAGE, "Boing", "![Boing](|)");

    lines(FMT_HEADING, "", "# ");
    lines(FMT_HEADING, "Title", "# Title");
    lines(FMT_HEADING, "# Title", "## Title");
    lines(FMT_HEADING, "## Title", "### Title");
    lines(FMT_HEADING, "### Title", "Title");
    lines(FMT_HEADING, "- item", "# item");
    lines(FMT_HEADING, "#hashtag", "# #hashtag");
    lines(FMT_BULLET, "", "- ");
    lines(FMT_BULLET, "one\ntwo", "- one\n- two");
    lines(FMT_BULLET, "- one\n* two", "one\ntwo");
    lines(FMT_BULLET, "one\n\ntwo", "- one\n\n- two");
    lines(FMT_BULLET, "1. one\n- [ ] two", "- one\n- two");
    lines(FMT_BULLET, "  nested", "  - nested");
    lines(FMT_NUMBERED, "one\ntwo\n\nthree", "1. one\n2. two\n\n3. three");
    lines(FMT_NUMBERED, "1. one\n2) two", "one\ntwo");
    lines(FMT_NUMBERED, "- one", "1. one");
    lines(FMT_TASK, "", "- [ ] ");
    lines(FMT_TASK, "do it\n- done", "- [ ] do it\n- [ ] done");
    lines(FMT_TASK, "- [ ] do it\n- [x] done", "do it\ndone");
    lines(FMT_QUOTE, "", "> ");
    lines(FMT_QUOTE, "one\n\ntwo", "> one\n>\n> two");
    lines(FMT_QUOTE, "> one\n>\n> two", "one\n\ntwo");
    lines(FMT_QUOTE, "> > deep", "> deep");
    lines(FMT_QUOTE, "> quoted\nnot", "> > quoted\n> not");

    if (!fails) printf("ok   formatting commands\n");
    return fails != 0;
}
