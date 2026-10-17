#!/usr/bin/env python3
# mkdist.py - builds the Aminet archive Kickdown.lha from the built tree
#
# Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
#
# Creates dist/Kickdown/ with the programs, an example, the documentation
# (README*.md converted to ISO-8859-1), the sources and icons (classic
# icons next to the files, complete GlowIcons and NewIcons sets in Icons/,
# see icons.py), then packs it with lha. The common parts come from
# amiga-devkit/tools/distlib.py. Run "make dist".

import os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icons import ROOT, WBDRAWER, WBTOOL, WBPROJECT  # noqa: E402  (registers the motifs)
from distlib import copy, copytree, copy_devkit, latin1_copy, package_icons, pack  # noqa: E402

NAME = 'Kickdown'
DRAWER_ICON = NAME + '-Drawer'      # Icons/<Style>/: icon of the package drawer
DIST = os.path.join(ROOT, 'dist')
PKG = os.path.join(DIST, NAME)


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
    copy(b('icons', 'preview.png'), os.path.join(docs, 'icons', 'preview.png'))
    copy(b('LICENSE'), os.path.join(docs, 'LICENSE'))

    # sources in the same layout as the repository, so "make" works there
    src = os.path.join(PKG, 'Source')
    for d in ('src', 'test', 'tools', 'package', 'catalogs'):
        copytree(b(d), os.path.join(src, d), ignore=('__pycache__', 'hostconv', 'hostsync', 'hosthl', 'hostset', 'hostfmt'))
    for f in ('Makefile', 'README.md', 'README.de.md', 'LICENSE', 'CLAUDE.md', 'screenshot-amiga.png'):
        copy(b(f), os.path.join(src, f))
    copy(b('icons', 'preview.png'), os.path.join(src, 'icons', 'preview.png'))
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

    pack(DIST, NAME)


if __name__ == '__main__':
    main()
