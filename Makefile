# Amiga-MDTools - MDEdit (ReAction Markdown editor) and mdtohtml
# Cross build with bebbo's amiga-gcc (m68k-amigaos-gcc) and NDK 3.2.

PREFIX  ?= /opt/amiga
CC      := $(PREFIX)/bin/m68k-amigaos-gcc
STRIP   := $(PREFIX)/bin/m68k-amigaos-strip
CPU     ?= -m68000
B       := build
O       := bin

# md4c (git submodule) works byte by byte: Latin-1 and UTF-8 pass unchanged
MD4C    := md4c/src
MD4CVER := $(shell git -C md4c describe --tags 2>/dev/null | sed 's/^release-//;s/^v//')
MDDEFS  := -DMD4C_USE_ASCII -I$(MD4C) '-DMD4C_VERSION_STR="$(MD4CVER)"'

# public headers of html.gadget (git submodule)
HTMLINC := html_gadget/include

CFLAGS  := $(CPU) -Os -noixemul -fno-common -Wall -Wextra -Wno-unused-parameter \
           -Wno-pointer-sign -I$(HTMLINC) -Isrc $(MDDEFS)
MD4CFLAGS := $(CPU) -Os -noixemul -fno-common -DMD4C_USE_ASCII

CONVOBJ := $(B)/mdconv.o $(B)/md4c.o $(B)/md4c-html.o $(B)/entity_stub.o $(B)/fileio.o
EDITOBJ := $(B)/mdedit.o $(B)/gui.o $(B)/sync.o $(CONVOBJ)
TOOLOBJ := $(B)/mdtohtml.o $(CONVOBJ)

all: charcheck $(O)/MDEdit $(O)/mdtohtml

$(MD4C)/md4c.c $(HTMLINC)/gadgets/html.h:
	@echo "*** submodules are missing: git submodule update --init"; exit 1

$(B)/md4c.o: $(MD4C)/md4c.c $(MD4C)/md4c.h
	@mkdir -p $(B)
	$(CC) $(MD4CFLAGS) -c $< -o $@

$(B)/md4c-html.o: $(MD4C)/md4c-html.c $(MD4C)/md4c-html.h $(MD4C)/md4c.h
	@mkdir -p $(B)
	$(CC) $(MD4CFLAGS) -c $< -o $@

$(B)/%.o: src/%.c src/mdedit.h src/mdconv.h src/fileio.h $(MD4C)/md4c.c $(HTMLINC)/gadgets/html.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/MDEdit: $(EDITOBJ)
	@mkdir -p $(O)
	$(CC) $(CPU) -noixemul -Wl,-u,___stkinit -o $@.debug $(EDITOBJ)
	$(STRIP) -o $@ $@.debug

$(O)/mdtohtml: $(TOOLOBJ)
	@mkdir -p $(O)
	$(CC) $(CPU) -noixemul -Wl,-u,___stkinit -o $@.debug $(TOOLOBJ)
	$(STRIP) -o $@ $@.debug

# Amiga sources and texts must be ISO-8859-1: refuse UTF-8 sequences.
# Not checked: README*.md, CLAUDE.md, test/utf8.* (UTF-8) and the submodules.
LATIN1  := $(filter-out test/utf8.%,$(wildcard src/*.c src/*.h test/*.md test/*.html test/*.expected \
           package/* LICENSE Makefile))
charcheck:
	@if LC_ALL=C grep -lP '[\xC2-\xF4][\x80-\xBF]' $(LATIN1); then \
		echo "*** The files above contain UTF-8, please convert them to ISO-8859-1"; exit 1; fi

# host test of the conversion (Linux/macOS): test/<name>.md must give
# test/<name>.expected
test/hostconv: test/hostconv.c src/mdconv.c src/mdconv.h src/entity_stub.c $(MD4C)/md4c.c $(MD4C)/md4c-html.c
	cc -g -Wall -fsanitize=address,undefined -Isrc $(MDDEFS) -o $@ \
		test/hostconv.c src/mdconv.c src/entity_stub.c $(MD4C)/md4c.c $(MD4C)/md4c-html.c

# host test of the scroll synchronisation (src/sync.c with gadget stubs)
test/hostsync: test/hostsync.c test/hoststubs.h src/sync.c src/mdconv.c src/mdconv.h src/entity_stub.c $(MD4C)/md4c.c $(MD4C)/md4c-html.c
	cc -g -Wall -fsanitize=address,undefined -Isrc -Itest/hostinc $(MDDEFS) -o $@ \
		test/hostsync.c src/mdconv.c src/entity_stub.c $(MD4C)/md4c.c $(MD4C)/md4c-html.c

CHECKS  := $(wildcard test/*.md)

check: test/hostconv test/hostsync
	@mkdir -p $(B)/test; fail=0; \
	for f in $(CHECKS); do n=$$(basename $$f .md); \
		if ./test/hostconv $$f test/template.html > $(B)/test/$$n.out && \
		   diff -u test/$$n.expected $(B)/test/$$n.out; then echo "ok   $$f"; \
		else echo "FAIL $$f"; fail=1; fi; \
		if ./test/hostsync $$f > $(B)/test/$$n.sync && ./test/hostsync $$f 500 > /dev/null; \
		then echo "ok   $$f (sync, $$(head -1 $(B)/test/$$n.sync))"; \
		else echo "FAIL $$f (sync)"; fail=1; fi; \
	done; exit $$fail

# accept the current output as reference after an intended change
check-update: test/hostconv
	@for f in $(CHECKS); do n=$$(basename $$f .md); \
		./test/hostconv $$f test/template.html > test/$$n.expected && echo "updated test/$$n.expected"; \
	done

# Aminet style archive dist/Amiga-MDTools.lha; needs an LhA that can
# create archives: jlha (Debian: jlha-utils) or lha for UNIX
LHA     ?= $(shell which jlha 2>/dev/null || echo lha)
dist: all
	rm -rf $(B)/dist dist
	mkdir -p $(B)/dist/Amiga-MDTools dist
	cp $(O)/MDEdit $(O)/mdtohtml $(B)/dist/Amiga-MDTools/
	cp test/template.html $(B)/dist/Amiga-MDTools/template.html
	cp test/features.md $(B)/dist/Amiga-MDTools/Example.md
	cp LICENSE $(B)/dist/Amiga-MDTools/
	cd $(B)/dist && $(LHA) ao5q ../../dist/Amiga-MDTools.lha Amiga-MDTools

clean:
	rm -rf $(B) $(O) dist test/hostconv test/hostsync

.PHONY: all clean check check-update charcheck dist
