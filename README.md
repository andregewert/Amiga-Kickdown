# Amiga-MDTools

*[Deutsche Version](README.de.md)*

Markdown tools for AmigaOS 3.2, written in C:

* **MDEdit** – a ReAction based Markdown editor with live HTML preview
* **mdtohtml** – a command line converter from Markdown to HTML

Both share the same converter core (`src/mdconv.c`) built on
[md4c](https://github.com/mity/md4c), a fast CommonMark compliant parser written in C
(included as git submodule). They replace the earlier ARexx/MUI editor and the
Free Pascal based `mdtohtml` from [PubAmiga](https://github.com/andregewert/PubAmiga).

## Requirements

* AmigaOS 3.2 (ReAction classes V44+, `texteditor.gadget`, `speedbar.gadget`, `bitmap.image`)
* [html.gadget](https://github.com/andregewert/Amiga-HTML-Gadget) in `SYS:Classes/Gadgets/` or next to MDEdit
  (optionally `htmlttf.gadget`)
* [AISS](http://masonicons.info/) for the toolbar images (`TBIMAGES:` assign). Missing images are
  replaced by text buttons.

`mdtohtml` only needs dos.library and runs on any AmigaOS.

## MDEdit

```
MDEdit [FILE] <name.md> [TEMPLATE <file>] [CHARSET <name>] [DIALECT GitHub|CommonMark] [TTF] [NOAUTOREFRESH] [NOSYNC]
```

* Editor (`texteditor.gadget`, fixed width font) on the left, HTML preview (`html.gadget`) on the
  right, the weight bar between them adjusts the split.
* The preview follows the text half a second after you stop typing (*Preview/Auto refresh*,
  can be switched off), *Preview/Refresh* (Amiga-R) updates it at once. The scroll position is kept.
* Editor and preview scroll together (*Preview/Synchronize scrolling*, both directions). Headings
  are the fixed points, positions between them are interpolated.
* Relative image paths are resolved against the document's drawer.
* Links: `#anchors` scroll the preview, links to `.md` files open them in the editor, other
  links are shown in the status line.
* Headings get GitHub style anchors (`## Two Words` → `#two-words`), so tables of contents work.
* Open, Save, Save as, Export HTML; asks before discarding changes or replacing files.
* Cut/Copy/Paste/Undo/Redo, select all; text selected in the preview can be copied, too.
* Status line with the cursor position, mouse wheel scrolls the pane under the pointer.
* AppWindow: drop a Markdown icon on the window to open it.

From the Workbench the same options are read from the tool types (`TEMPLATE=`, `CHARSET=`,
`DIALECT=`, `TTF`, `NOAUTOREFRESH`, `NOSYNC`); a relative `TEMPLATE` is relative to the icon's drawer.
Set MDEdit as default tool of your `.md` icons to open them by double click.

## mdtohtml

```
mdtohtml [FROM|FILE] <file.md> [TO|OUTFILE <file.html>] [TEMPLATE <file>]
         [CHARSET|ENCODING <name>] [TITLE <text>] [DIALECT|MODE GitHub|CommonMark]
```

* Without `FROM` the Markdown text is read from standard input, without `TO` the page is
  written to standard output (e.g. `mdtohtml <readme.md >readme.html`).
* `DIALECT`: `GitHub` (default; tables, ~~strikethrough~~, task lists, autolinks, footnotes) or
  `CommonMark`.
* `CHARSET` is only written into the page, the text itself is not converted. Default: `UTF-8` if
  the input is valid UTF-8 with non-ASCII characters, otherwise `ISO-8859-1`.
* `TITLE`: default is the first `#` heading, otherwise the file name.
* Raw HTML in the Markdown text is passed through.

Note: the Free Pascal version used Unix style options (`-f`, `-o`, `-t` ...). This version uses
the usual AmigaDOS template; `FILE`, `OUTFILE`, `ENCODING` and `MODE` are kept as aliases.

### Templates

A template is an HTML file with the placeholders `$title$`, `$encoding$` (or `$charset$`) and
`$body$` (case is ignored), see `test/template.html`. Without a template a minimal HTML 4 frame
is used. MDEdit uses the template for the preview and the export.

## Icons

The archive comes with classic icons; complete sets in the **GlowIcons** and **NewIcons**
style are in `Icons/` (double click `UseGlowIcons`, `UseNewIcons` or `UseClassic`).
`tools/icons.py` draws them with the icon writer of html.gadget (`html_gadget/tools/mkicons.py`).

![Icon styles: classic, GlowIcons, NewIcons (normal and selected)](icons/preview.png)

## Building

Requires [bebbo's amiga-gcc](https://codeberg.org/bebbo/amiga-gcc) in `/opt/amiga` (with NDK 3.2).

```
git clone --recursive <repository>     # or: git submodule update --init
make                # bin/MDEdit, bin/mdtohtml
make check          # converter and scroll sync tests on the host (with AddressSanitizer)
make check-update   # accept intended output changes as new reference
make dist           # Aminet archive dist/MDTools.lha (+ MDTools.readme)
make icons          # sample icons of all styles in icons/, preview in icons/preview.png
```

All Amiga sources and test files are **ISO-8859-1** encoded; `make` refuses UTF-8 (`make charcheck`).
md4c and the html.gadget headers come as git submodules (`md4c/`, `html_gadget/`), both pinned
to a release tag.

## License

MIT, see [LICENSE](LICENSE). md4c is MIT licensed as well (`md4c/LICENSE.md`).
