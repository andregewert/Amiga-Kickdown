#!/usr/bin/env python3
# mkdist.py - builds the Aminet archive MDTools.lha from the built tree
#
# Copyright (c) 2026 Andre Gewert <agewert@ubergeek.de>, MIT License
#
# Creates dist/MDTools/ with the programs, an example, the documentation
# (README*.md converted to ISO-8859-1), the sources and icons (classic
# icons next to the files, complete GlowIcons and NewIcons sets in Icons/,
# see icons.py), then packs it with lha. Run "make dist".

import os, shutil, subprocess, glob, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icons import ROOT, STYLES, WBDRAWER, WBTOOL, WBPROJECT, write_icon  # noqa: E402

NAME = 'MDTools'
DIST = os.path.join(ROOT, 'dist')
PKG = os.path.join(DIST, NAME)


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def copytree(src, dst, ignore=()):
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(*ignore), dirs_exist_ok=True)


def to_latin1_text(md):
    table = {'„': '"', '“': '"', '”': '"', '–': '-', '—': '-',
             '…': '...', '→': '->', '≤': '<=', '≥': '>=', '’': "'"}
    return ''.join(table.get(ch, ch) for ch in md).encode('latin-1', 'replace')


def main():
    b = lambda *p: os.path.join(ROOT, *p)
    shutil.rmtree(DIST, ignore_errors=True)
    os.makedirs(PKG)

    copy(b('package', NAME + '.readme'), os.path.join(PKG, NAME + '.readme'))
    copy(b('LICENSE'), os.path.join(PKG, 'LICENSE'))
    copy(b('bin', 'MDEdit'), os.path.join(PKG, 'MDEdit'))
    copy(b('bin', 'mdtohtml'), os.path.join(PKG, 'C', 'mdtohtml'))
    copy(b('test', 'features.md'), os.path.join(PKG, 'Example.md'))
    copy(b('html_gadget', 'demo', 'boing.gif'), os.path.join(PKG, 'boing.gif'))
    copy(b('test', 'template.html'), os.path.join(PKG, 'Template.html'))

    docs = os.path.join(PKG, 'Docs')
    os.makedirs(docs)
    for src, dst in (('README.md', 'ReadMe.md'), ('README.de.md', 'LiesMich.md')):
        with open(b(src), encoding='utf-8') as f:
            text = to_latin1_text(f.read())
        with open(os.path.join(docs, dst), 'wb') as f:
            f.write(text)

    # sources in the same layout as the repository, so "make" works there
    src = os.path.join(PKG, 'Source')
    for d in ('src', 'test', 'tools', 'package'):
        copytree(b(d), os.path.join(src, d), ignore=('__pycache__', 'hostconv', 'hostsync', 'hosthl', 'hostset'))
    for f in ('Makefile', 'README.md', 'README.de.md', 'LICENSE', 'CLAUDE.md'):
        copy(b(f), os.path.join(src, f))
    copytree(b('md4c', 'src'), os.path.join(src, 'md4c', 'src'), ignore=('*.pc.in', '*.cmake', 'CMakeLists.txt'))
    copy(b('md4c', 'LICENSE.md'), os.path.join(src, 'md4c', 'LICENSE.md'))
    ver = subprocess.run(['make', '-s', '--no-print-directory', 'md4cversion'], cwd=ROOT,
                         capture_output=True, text=True, check=True).stdout.strip()
    with open(os.path.join(src, 'md4c', 'VERSION'), 'w') as f:
        f.write(ver + '\n')
    copytree(b('html_gadget', 'include'), os.path.join(src, 'html_gadget', 'include'))
    copy(b('html_gadget', 'tools', 'mkicons.py'), os.path.join(src, 'html_gadget', 'tools', 'mkicons.py'))
    copy(b('html_gadget', 'LICENSE'), os.path.join(src, 'html_gadget', 'LICENSE'))

    # icons: classic ones next to the files, every style also in Icons/<Style>/
    mv = 'SYS:Utilities/MultiView'
    icons = [('MDEdit', 'mdedit', WBTOOL,
              dict(stack=65536, tooltypes=('(TEMPLATE=Template.html)', '(DIALECT=GitHub)',
                                           '(CHARSET=ISO-8859-1)', '(TTF)', '(FONTSET=Vera)',
                                           '(SIZE=12)',
                                           '(NOAUTOREFRESH)', '(NOSYNC)', '(NOHIGHLIGHT)',
                                           '(LINENUMBERS)'))),
             ('Example.md', 'markdown', WBPROJECT, dict(default_tool='MDEdit')),
             ('Template.html', 'readme', WBPROJECT, dict(default_tool=mv)),
             (NAME + '.readme', 'readme', WBPROJECT, dict(default_tool=mv)),
             ('LICENSE', 'license', WBPROJECT, dict(default_tool=mv)),
             ('Docs/ReadMe.md', 'markdown', WBPROJECT, dict(default_tool=mv)),
             ('Docs/LiesMich.md', 'markdown', WBPROJECT, dict(default_tool=mv))]
    for d in ('C', 'Docs', 'Icons', 'Source'):
        icons.append((d, 'drawer', WBDRAWER, {}))
    write_icon(PKG, 'Classic', 'drawer', WBDRAWER)
    for name, role, kind, kw in icons:
        write_icon(os.path.join(PKG, name), 'Classic', role, kind, **kw)
    for style in STYLES:
        sets = os.path.join(PKG, 'Icons', style)
        write_icon(os.path.join(sets, NAME), style, 'drawer', WBDRAWER)
        for name, role, kind, kw in icons:
            write_icon(os.path.join(sets, name), style, role, kind, **kw)
        script = os.path.join(PKG, 'Icons', 'Use' + style)
        with open(script, 'w', encoding='latin-1', newline='\n') as f:
            f.write('; gives the files of %s the %s icons\n'
                    '; (double click, IconX runs it in this drawer)\n'
                    'Copy %s/~(%s.info) / ALL CLONE QUIET\n'
                    'Copy %s/%s.info // CLONE QUIET\n'
                    'Echo "%s icons copied. Close and reopen the drawers to see them."\n'
                    % (NAME, style, style, NAME, style, NAME, style))
        write_icon(script, style, 'tiles', WBPROJECT, default_tool='C:IconX')

    # archive
    arc = os.path.join(DIST, NAME + '.lha')
    files = [NAME + '.info']
    for dirpath, dirnames, filenames in os.walk(PKG):
        dirnames.sort()
        for fn in sorted(filenames):
            files.append(os.path.relpath(os.path.join(dirpath, fn), DIST))
    # an LhA that can create archives: jlha (Debian: jlha-utils) or lha for UNIX
    tool = shutil.which('jlha') or 'lha'
    subprocess.run([tool, 'ao5q', arc] + files, cwd=DIST, check=True)
    shutil.copy2(os.path.join(PKG, NAME + '.readme'), os.path.join(DIST, NAME + '.readme'))
    print('created', arc, os.path.getsize(arc), 'bytes')


if __name__ == '__main__':
    main()
