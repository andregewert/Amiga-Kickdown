#!/usr/bin/env python3
# icons.py - Workbench icons of the Amiga-MDTools distribution
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# The icon writer (classic 4 colour icons, GlowIcons, NewIcons) comes from
# html_gadget/tools/mkicons.py (git submodule). This file adds the motifs of
# MDEdit and of Markdown documents and registers them there.
#
# Run directly (or "make icons") to write sample icons of all styles to
# icons/<Style>/ and a preview picture icons/preview.png.

import os, sys, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location(
    'gadgeticons', os.path.join(ROOT, 'html_gadget', 'tools', 'mkicons.py'))
gi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gi)

from_gi = ('STYLES', 'WBDRAWER', 'WBTOOL', 'WBPROJECT', 'write_icon')
STYLES, WBDRAWER, WBTOOL, WBPROJECT, write_icon = (getattr(gi, n) for n in from_gi)

BLACK, WHITE, TEXT = gi.BLACK, gi.WHITE, gi.TEXT
BLUE, GREY, PAPER = gi.BLUE, gi.GREY, gi.PAPER
INK = ((90, 96, 112), (40, 44, 56))          # the Markdown mark


# ------------------------------------------------------ classic pictures ---
# pens: 0 grey, 1 black, 2 white, 3 blue

def c_markmark(c, x, y):
    """ the Markdown mark, 13 x 7: black box, white "M" and arrow """
    c.rect(x, y, x + 12, y + 6, 1)
    for i in range(5):                        # M
        c.rect(x + 2, y + 1 + i, x + 2, y + 1 + i, 2)
        c.rect(x + 6, y + 1 + i, x + 6, y + 1 + i, 2)
    c.rect(x + 3, y + 2, x + 3, y + 2, 2)
    c.rect(x + 4, y + 3, x + 4, y + 3, 2)
    c.rect(x + 5, y + 2, x + 5, y + 2, 2)
    c.rect(x + 9, y + 1, x + 9, y + 4, 2)     # arrow
    c.rect(x + 8, y + 4, x + 10, y + 4, 2)
    c.rect(x + 9, y + 5, x + 9, y + 5, 2)


def pic_mdedit():
    c = gi.Canvas(40, 24)
    c.rect(1, 1, 38, 22, 2)
    c.frame(1, 1, 38, 22, 1, 1)
    c.rect(2, 2, 37, 4, 3)                    # title bar
    c.rect(20, 5, 20, 21, 1)                  # split
    for y in (8, 11, 14, 17):                 # editor: Markdown source
        c.rect(4, y, 17 if y != 14 else 12, y, 1)
    c.rect(4, 8, 5, 8, 3)                     # "#"
    c.rect(23, 7, 33, 8, 3)                   # preview: heading
    for y in (11, 14, 17):
        c.rect(23, y, 35 if y != 17 else 30, y, 1)
    return c


def pic_markdown():
    c = gi.CLASSIC['readme']()
    c.rect(5, 14, 22, 21, 2)                  # clear the lower lines
    c_markmark(c, 8, 14)
    return c


gi.CLASSIC['mdedit'] = pic_mdedit
gi.CLASSIC['markdown'] = pic_markdown


# --------------------------------------------- GlowIcons / NewIcons motifs ---

def mark(s, c, x, y):
    """ the Markdown mark, 17 x 10 """
    c.rect(x, y, x + 16, y + 9, s.line(INK[1]))
    c.rect(x + 1, y + 1, x + 15, y + 8, s.ramp(*INK)(x, y, x + 16, y + 9))
    for i in range(6):                        # M
        c.put(x + 3, y + 2 + i, WHITE)
        c.put(x + 8, y + 2 + i, WHITE)
    for dx, dy in ((4, 3), (5, 4), (6, 4), (7, 3)):
        c.put(x + dx, y + dy, WHITE)
    c.rect(x + 12, y + 2, x + 12, y + 5, WHITE)     # arrow
    c.rect(x + 10, y + 5, x + 14, y + 5, WHITE)
    c.rect(x + 11, y + 6, x + 13, y + 6, WHITE)
    c.put(x + 12, y + 7, WHITE)


def m_mdedit(s, c):
    frame = s.line(GREY[1])
    c.rect(1, 3, 40, 35, frame)                      # window
    c.rect(2, 4, 39, 8, s.ramp(*BLUE)(2, 4, 39, 8))  # title bar
    c.rect(3, 5, 5, 7, WHITE)
    c.rect(2, 9, 20, 34, s.ramp(WHITE, PAPER[1])(2, 9, 20, 34))     # editor
    c.rect(21, 9, 21, 34, frame)
    c.rect(22, 9, 39, 34, s.ramp(WHITE, PAPER[1])(22, 9, 39, 34))   # preview
    for y, x1 in ((15, 17), (18, 14), (21, 17), (24, 12), (27, 16), (30, 15)):
        c.line(4, y, x1, y, TEXT)
    c.line(4, 12, 5, 12, BLUE[1])                    # "# Heading"
    c.line(7, 12, 15, 12, TEXT)
    c.rect(24, 11, 34, 13, BLUE[1])                  # rendered heading
    for y, x1 in ((17, 37), (20, 35)):
        c.line(24, y, x1, y, TEXT)
    mark(s, c, 23, 24)                               # inside the preview


def m_markdown(s, c):
    gi.m_page(s, c)
    c.rect(9, 20, 33, 34, s.ramp(*PAPER)(9, 20, 33, 34))   # clear the lower lines
    mark(s, c, 12, 23)


gi.MOTIFS['mdedit'] = m_mdedit
gi.MOTIFS['markdown'] = m_markdown


# ---------------------------------------------------------------- samples ---

ROLES = ('drawer', 'mdedit', 'markdown', 'readme', 'license', 'tiles')

SAMPLES = (('Drawer', 'drawer', WBDRAWER, {}),
           ('MDEdit', 'mdedit', WBTOOL, {'stack': 65536}),
           ('Example.md', 'markdown', WBPROJECT, {'default_tool': 'MDEdit'}),
           ('ReadMe', 'readme', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}),
           ('License', 'license', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}),
           ('Script', 'tiles', WBPROJECT, {'default_tool': 'C:IconX'}))


def main():
    out = os.path.join(ROOT, 'icons')
    for style in STYLES:
        for name, role, kind, kw in SAMPLES:
            write_icon(os.path.join(out, style, name), style, role, kind, **kw)
    # the preview shows the roles registered in MOTIFS: only ours, in order
    motifs = gi.MOTIFS
    gi.MOTIFS = {r: motifs[r] for r in ROLES}
    gi.preview(os.path.join(out, 'preview.png'))
    gi.MOTIFS = motifs
    print('icons written to', out)


if __name__ == '__main__':
    main()
