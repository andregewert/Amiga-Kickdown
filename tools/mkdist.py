#!/usr/bin/env python3
# mkdist.py - builds the Aminet archive Kickdown.lha from the built tree
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# Creates dist/Kickdown/ with the programs, an example, the documentation
# (README*.md converted to ISO-8859-1), the sources and icons (classic
# icons next to the files, complete GlowIcons and NewIcons sets in Icons/,
# see icons.py, and the Masonicons drawn by hand in icons/Masonicons/), then
# packs it with lha. The common parts come from
# amiga-devkit/tools/distlib.py. Run "make dist".

import os, shutil, struct, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icons import ROOT, WBDRAWER, WBTOOL, WBPROJECT  # noqa: E402  (registers the motifs)
from distlib import copy, copytree, copy_devkit, latin1_copy, package_icons, pack  # noqa: E402

NAME = 'Kickdown'
DRAWER_ICON = NAME + '-Drawer'      # Icons/<Style>/: icon of the package drawer
DIST = os.path.join(ROOT, 'dist')
PKG = os.path.join(DIST, NAME)
MASON = 'Masonicons'                # icons/Masonicons/: a set drawn by Martin "Mason" Merz


def mason_icons():
    """ Icons/Masonicons/ from icons/Masonicons/ (made by hand, laid out
        like the generated sets, AUTHOR names the artist) and the script
        Icons/UseMasonicons, written like the scripts of package_icons(),
        but leaving AUTHOR where it is                                  """
    copytree(os.path.join(ROOT, 'icons', MASON), os.path.join(PKG, 'Icons', MASON))
    script = os.path.join(PKG, 'Icons', 'Use' + MASON)
    with open(script, 'w', encoding='latin-1', newline='\n') as f:
        f.write('; gives the files of %s the %s icons\n'
                '; (double click, IconX runs it in this drawer)\n'
                'Copy %s/~(%s.info|AUTHOR|AUTHOR.info) / ALL CLONE QUIET\n'
                'Copy %s/%s.info //%s.info CLONE QUIET\n'
                'Echo "%s icons copied. Close and reopen the drawers to see them."\n'
                % (NAME, MASON, MASON, DRAWER_ICON, MASON, DRAWER_ICON, NAME, MASON))
    # the set has no script icon: the one of Example.md, run by IconX
    with open(os.path.join(ROOT, 'icons', MASON, 'Example.md.info'), 'rb') as f:
        icon = f.read()
    with open(script + '.info', 'wb') as f:
        f.write(with_default_tool(icon, 'C:IconX'))


def with_default_tool(icon, tool):
    """ the .info file icon (a project icon with a default tool) with tool
        as its default tool; the parts after it (tool types, the image
        chunks of OS 3.5) only move                                     """
    gadget_render, select_render = struct.unpack('>II', icon[22:30])
    has_tool, drawer_data = struct.unpack('>I', icon[50:54])[0], struct.unpack('>I', icon[66:70])[0]
    assert has_tool, 'icon without default tool'
    pos = 78 + (56 if drawer_data else 0)
    for image in (gadget_render, select_render):      # struct Image + planes
        if image:
            w, h, depth, data = struct.unpack('>hhhI', icon[pos + 4:pos + 14])
            pos += 20 + (((w + 15) // 16) * 2 * h * depth if data else 0)
    old = struct.unpack('>I', icon[pos:pos + 4])[0]
    new = tool.encode('latin-1') + b'\0'
    return icon[:pos] + struct.pack('>I', len(new)) + new + icon[pos + 4 + old:]


def main():
    b = lambda *p: os.path.join(ROOT, *p)
    shutil.rmtree(DIST, ignore_errors=True)
    os.makedirs(PKG)

    copy(b('package', NAME + '.readme'), os.path.join(PKG, NAME + '.readme'))
    copy(b('package', 'Install'), os.path.join(PKG, 'Install'))
    # translations; next to Kickdown they work without installing (PROGDIR:Catalogs)
    languages = sorted(os.listdir(b('bin', 'Catalogs')))
    for lang in languages:
        copy(b('bin', 'Catalogs', lang, 'Kickdown.catalog'), os.path.join(PKG, 'Catalogs', lang, 'Kickdown.catalog'))
    copy(b('LICENSE'), os.path.join(PKG, 'LICENSE'))
    copy(b('bin', 'Kickdown'), os.path.join(PKG, 'Kickdown'))
    copy(b('bin', 'mdtohtml'), os.path.join(PKG, 'C', 'mdtohtml'))
    copy(b('test', 'features.md'), os.path.join(PKG, 'Example.md'))
    copy(b('html_gadget', 'demo', 'boing.gif'), os.path.join(PKG, 'boing.gif'))
    copy(b('test', 'template.html'), os.path.join(PKG, 'Template.html'))

    docs = os.path.join(PKG, 'Docs')
    # under their own names: they link to each other
    for name in ('README.md', 'README.de.md'):
        latin1_copy(b(name), os.path.join(docs, name))
    # what the READMEs show or link to, at the same relative paths
    copy(b('screenshot-amiga.png'), os.path.join(docs, 'screenshot-amiga.png'))
    for png in ('preview.png', 'masonicons.png'):
        copy(b('icons', png), os.path.join(docs, 'icons', png))
    copy(b('LICENSE'), os.path.join(docs, 'LICENSE'))

    # sources in the same layout as the repository, so "make" works there
    src = os.path.join(PKG, 'Source')
    for d in ('src', 'test', 'tools', 'package', 'catalogs'):
        copytree(b(d), os.path.join(src, d), ignore=('__pycache__', 'hostconv', 'hostsync', 'hosthl', 'hostset', 'hostfmt', 'hosttree'))
    for f in ('Makefile', 'README.md', 'README.de.md', 'TECHNICAL.md', 'LICENSE', 'CLAUDE.md', 'screenshot-amiga.png'):
        copy(b(f), os.path.join(src, f))
    for png in ('preview.png', 'masonicons.png'):
        copy(b('icons', png), os.path.join(src, 'icons', png))
    copytree(b('md4c', 'src'), os.path.join(src, 'md4c', 'src'), ignore=('*.pc.in', '*.cmake', 'CMakeLists.txt'))
    copy(b('md4c', 'LICENSE.md'), os.path.join(src, 'md4c', 'LICENSE.md'))
    ver = subprocess.run(['make', '-s', '--no-print-directory', 'md4cversion'], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.strip()
    with open(os.path.join(src, 'md4c', 'VERSION'), 'w') as f:
        f.write(ver + '\n')
    copytree(b('html_gadget', 'include'), os.path.join(src, 'html_gadget', 'include'))
    copy(b('html_gadget', 'LICENSE'), os.path.join(src, 'html_gadget', 'LICENSE'))
    copy_devkit(os.path.join(src, 'amiga-devkit'))

    # icons: classic ones next to the files, every style also in Icons/<Style>/
    mv = 'SYS:Utilities/MultiView'
    icons = [('Install', 'install', WBPROJECT,
              dict(default_tool='Installer', tooltypes=('APPNAME=Kickdown', 'MINUSER=AVERAGE'))),
             ('Kickdown', 'kickdown', WBTOOL,
              dict(stack=65536, tooltypes=('(TEMPLATE=Template.html)', '(DIALECT=GitHub)',
                                           '(CHARSET=ISO-8859-1)', '(TTF)', '(FONTSET=Vera)',
                                           '(SIZE=12)',
                                           '(NOAUTOREFRESH)', '(NOSYNC)', '(NOHIGHLIGHT)',
                                           '(LINENUMBERS)'))),
             ('Example.md', 'markdown', WBPROJECT, dict(default_tool='Kickdown')),
             ('Template.html', 'readme', WBPROJECT, dict(default_tool=mv)),
             (NAME + '.readme', 'readme', WBPROJECT, dict(default_tool=mv)),
             ('LICENSE', 'license', WBPROJECT, dict(default_tool=mv)),
             ('Docs/README.md', 'markdown', WBPROJECT, dict(default_tool=mv)),
             ('Docs/README.de.md', 'markdown', WBPROJECT, dict(default_tool=mv))]
    for d in ['C', 'Docs', 'Icons', 'Source', 'Catalogs'] + ['Catalogs/' + l for l in languages]:
        icons.append((d, 'drawer', WBDRAWER, {}))
    # The icon of the package drawer has a name of its own in Icons/<Style>/:
    # the drawer is called like the program, whose icon is Kickdown.info.
    package_icons(PKG, NAME, icons, DRAWER_ICON)
    mason_icons()

    pack(DIST, NAME)


if __name__ == '__main__':
    main()
