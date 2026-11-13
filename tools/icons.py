#!/usr/bin/env python3
# icons.py - Workbench icons of the Amiga-Kickdown distribution
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# The icon writer (classic 4 colour icons, GlowIcons, NewIcons) and the
# motifs drawer, readme, license, install and tiles come from
# amiga-devkit/tools/amigaicons.py (git submodule). This file adds the
# motifs of Kickdown and of Markdown documents and registers them there.
#
# Run directly (or "make icons") to write sample icons of all styles to
# icons/<Style>/ and a preview picture icons/preview.png, and the preview
# icons/masonicons.png of the hand drawn set in icons/Masonicons/.

import os, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'amiga-devkit', 'tools'))
import amigaicons as gi  # noqa: E402

from amigaicons import STYLES, WBDRAWER, WBTOOL, WBPROJECT, write_icon  # noqa: E402,F401

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


def pic_kickdown():
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


def m_kickdown(s, c):
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


# ------------------------------------------------------- NewIcons motifs ---

def n_kickdown(c, sel):
    """ the monitor with the editor (white lines) and the preview;
        selected: scrolled on, the cursor line yellow                     """
    def screen(c, box, u0, u1, t0, t1, sel):
        mid = (u0 + u1) // 2
        lines = ((2, 6), (4, 4), (6, 6), (8, 3), (10, 5))
        if sel:
            lines = lines[1:] + ((12, 4),)
        for i, (t, n) in enumerate(lines):
            box.face_line(c, t0 + t - (2 if sel else 0), u0 + 1, u0 + n,
                          gi.N_YELLOW if sel and i == 2 else WHITE)
        if not sel:
            box.face_line(c, t0 + 2, mid + 2, mid + 5, gi.N_YELLOW)
        for t in ((2, 4, 6, 8) if sel else (4, 6, 8, 10)):
            box.face_line(c, t0 + t, mid + 2, u1 - 1, gi.N_LBLUE)
    gi.n_monitor(c, screen, sel)


def n_badge(c, x, y):
    """ the Markdown mark as a straight label, 15 x 9 """
    c.rect(x, y, x + 14, y + 8, BLACK)
    for i in range(5):                        # M
        c.put(x + 2, y + 2 + i, WHITE)
        c.put(x + 7, y + 2 + i, WHITE)
    for dx, dy in ((3, 3), (4, 4), (5, 4), (6, 3)):
        c.put(x + dx, y + dy, WHITE)
    c.rect(x + 11, y + 2, x + 11, y + 5, WHITE)     # arrow
    c.rect(x + 9, y + 5, x + 13, y + 5, WHITE)
    c.rect(x + 10, y + 6, x + 12, y + 6, WHITE)


def n_markdown(c, sel):
    gi.n_sheet(c, sel)
    n_badge(c, 19, 28)


gi.register('kickdown', pic_kickdown, m_kickdown, n_kickdown)
gi.register('markdown', pic_markdown, m_markdown, n_markdown)


# ---------------------------------------------------------------- samples ---

ROLES = ('drawer', 'kickdown', 'markdown', 'readme', 'license', 'tiles')

SAMPLES = (('Drawer', 'drawer', WBDRAWER, {}),
           ('Kickdown', 'kickdown', WBTOOL, {'stack': 65536}),
           ('Example.md', 'markdown', WBPROJECT, {'default_tool': 'Kickdown'}),
           ('ReadMe', 'readme', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}),
           ('License', 'license', WBPROJECT, {'default_tool': 'SYS:Utilities/MultiView'}),
           ('Script', 'tiles', WBPROJECT, {'default_tool': 'C:IconX'}))


# ------------------------------------------------- Masonicons preview ---
# The Masonicons (icons/Masonicons/, by Martin "Mason" Merz) are not drawn
# here; their OS 3.5 colour images are read back for the README picture.

MASON_PREVIEW = ('Kickdown-Drawer', 'Kickdown', 'Example.md', 'Template.html',
                 'Kickdown.readme', 'LICENSE', 'Install')


def unpack_rle(data, depth, count):
    """ count values of depth bits from the run length coded data of an
        IMAG chunk (byte n < 128: n+1 values follow, n > 128: the next
        value 257-n times, 128: nothing)                                """
    bits = ''.join(format(b, '08b') for b in data)
    pos, out = 0, []

    def take(n):
        nonlocal pos
        v = int(bits[pos:pos + n], 2)
        pos += n
        return v
    while len(out) < count and pos + 8 <= len(bits):
        n = take(8)
        if n < 128:
            out.extend(take(depth) for _ in range(n + 1))
        elif n > 128:
            out.extend([take(depth)] * (257 - n))
    return out[:count]


def colour_images(path):
    """ the OS 3.5 images (FORM ICON, FACE and IMAG chunks) of a .info file
        as rows of RGB or None (transparent): [normal, selected]         """
    d = open(path, 'rb').read()
    pos = d.find(b'FORM', 78) + 12
    images, palette, w, h = [], None, 0, 0
    while pos + 8 <= len(d):
        cid, n = d[pos:pos + 4], struct.unpack('>I', d[pos + 4:pos + 8])[0]
        c = d[pos + 8:pos + 8 + n]
        if cid == b'FACE':
            w, h = c[0] + 1, c[1] + 1
        elif cid == b'IMAG':
            trans, ncol, flags, ifmt, pfmt, depth = c[:6]
            nimg, npal = struct.unpack('>HH', c[6:10])
            img = c[10:10 + nimg + 1]
            pix = unpack_rle(img, depth, w * h) if ifmt else list(img[:w * h])
            if flags & 2:
                raw = c[11 + nimg:12 + nimg + npal]
                raw = bytes(unpack_rle(raw, 8, (ncol + 1) * 3)) if pfmt else raw
                palette = [tuple(raw[i:i + 3]) for i in range(0, len(raw) - 2, 3)]
            images.append([[None if (flags & 1 and v == trans) else palette[v]
                            for v in pix[y * w:(y + 1) * w]] for y in range(h)])
        pos += 8 + n + (n & 1)
    return images


def mason_preview(path, names=MASON_PREVIEW, scale=3):
    """ a PNG like preview.png with the Masonicons, normal and selected """
    pics = [colour_images(os.path.join(ROOT, 'icons', 'Masonicons', n + '.info')) for n in names]
    gap = 10
    cw = max(len(im[0]) for p in pics for im in p)
    ch = max(len(im) for p in pics for im in p)
    pw = gap + len(pics) * (2 * cw + 8 + gap)
    ph = ch + 2 * gap
    bg = gi.WB_PEN[0]
    out = [[bg] * pw for _ in range(ph)]
    for k, pic in enumerate(pics):
        for sel, img in enumerate(pic[:2]):
            ox = gap + k * (2 * cw + 8 + gap) + sel * (cw + 8) + (cw - len(img[0])) // 2
            oy = gap + ch - len(img)
            for y, row in enumerate(img):
                for x, v in enumerate(row):
                    if v is not None:
                        out[oy + y][ox + x] = v
    raw = b''.join(b'\0' + b''.join(bytes(c) * scale for c in row) for row in out for _ in range(scale))

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' +
                chunk(b'IHDR', struct.pack('>IIBBBBB', pw * scale, ph * scale, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    gi.write_samples(os.path.join(ROOT, 'icons'), SAMPLES, ROLES)
    mason_preview(os.path.join(ROOT, 'icons', 'masonicons.png'))
