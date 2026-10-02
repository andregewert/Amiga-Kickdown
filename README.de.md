# Amiga-MDTools

*[English version](README.md)*

Markdown-Werkzeuge für AmigaOS 3.2 in C:

* **MDEdit** – ReAction-basierter Markdown-Editor mit Live-HTML-Vorschau
* **mdtohtml** – Kommandozeilen-Konverter von Markdown nach HTML

Beide nutzen denselben Konvertierungskern (`src/mdconv.c`) auf Basis von
[md4c](https://github.com/mity/md4c), einem schnellen, CommonMark-konformen Parser in C
(als Git-Submodule eingebunden). Sie lösen den ARexx/MUI-Editor und das Free-Pascal-`mdtohtml`
aus [PubAmiga](https://github.com/andregewert/PubAmiga) ab.

## Voraussetzungen

* AmigaOS 3.2 (ReAction-Klassen ab V44, `texteditor.gadget`, `speedbar.gadget`, `bitmap.image`)
* [html.gadget](https://github.com/andregewert/Amiga-HTML-Gadget) in `SYS:Classes/Gadgets/` oder neben MDEdit
  (optional `htmlttf.gadget`)
* [AISS](http://masonicons.info/) für die Toolbar-Bilder (Assign `TBIMAGES:`). Fehlende Bilder
  werden durch Text-Buttons ersetzt.

`mdtohtml` braucht nur die dos.library.

## MDEdit

```
MDEdit [FILE] <name.md> [TEMPLATE <datei>] [CHARSET <name>] [DIALECT GitHub|CommonMark] [TTF] [NOAUTOREFRESH] [NOSYNC] [NOHIGHLIGHT] [LINENUMBERS]
       [FONTSET Vera|DejaVu|Noto] [SIZE n]
```

* Links der Editor (`texteditor.gadget`, Festbreitenschrift), rechts die Vorschau (`html.gadget`),
  dazwischen ein verschiebbarer Balken.
* Die Vorschau folgt eine halbe Sekunde nach der letzten Eingabe (*Preview/Auto refresh*,
  abschaltbar), *Preview/Refresh* (Amiga-R) aktualisiert sofort. Die Scrollposition bleibt erhalten.
* Syntax-Hervorhebung im Editor (*Edit/Syntax highlighting*, braucht texteditor.gadget V47):
  Überschriften, Hervorhebungen, Code (auch Codeblöcke), Zitate, Listenzeichen, Links, URLs,
  Tabellen, HTML-Tags und Entities.
* Zeilennummern im Editor einblendbar (*Edit/Line numbers*).
* Einstellungsfenster (*Project/Settings...*) mit den Kategorien Editor, Preview (Renderer,
  TrueType-Schriften, Aktualisierung, Scrollen), Markdown (Dialekt, Zeichensatz, Template),
  Colours (Farben der Syntax-Hervorhebung) und Window (Startgröße und -position, „Current“ übernimmt
  sie vom offenen Fenster). *Save* schreibt sie in die Tooltypes des
  MDEdit-Icons, *Use* gilt für die laufende Sitzung; andere Tooltypes bleiben unverändert.
* Editor und Vorschau scrollen gemeinsam (*Preview/Synchronize scrolling*, in beide Richtungen).
  Überschriften sind die Fixpunkte, dazwischen wird interpoliert.
* Relative Bildpfade beziehen sich auf die Schublade des Dokuments.
* Links: `#anker` scrollen die Vorschau, Links auf `.md`-Dateien öffnen diese im Editor, andere
  Links erscheinen in der Statuszeile.
* Überschriften erhalten Anker wie auf GitHub (`## Zwei Worte` → `#zwei-worte`), damit
  Inhaltsverzeichnisse funktionieren.
* Öffnen, Speichern, Speichern als, HTML exportieren; Rückfrage vor dem Verwerfen von Änderungen
  und vor dem Überschreiben.
* Ausschneiden/Kopieren/Einfügen/Rückgängig/Wiederholen, alles markieren; Markierungen in der
  Vorschau lassen sich ebenfalls kopieren.
* Statuszeile mit Cursorposition, das Mausrad scrollt den Bereich unter dem Mauszeiger.
* AppWindow: ein auf das Fenster gezogenes Icon wird geöffnet.
* Ikonifizieren (Gadget in der Titelleiste oder *Project/Iconify*): Das MDEdit-Icon erscheint auf
  der Workbench, ein Doppelklick oder ein darauf gezogenes Markdown-Icon öffnet das Fenster wieder.

Die Einstellungen kommen aus den Tooltypes des MDEdit-Icons, auch beim Start aus der Shell;
Shell-Argumente und Tooltypes eines Projekt-Icons gehen vor. Dieselben Optionen werden gelesen (`TEMPLATE=`, `CHARSET=`,
`DIALECT=`, `TTF`, `NOAUTOREFRESH`, `NOSYNC`, `NOHIGHLIGHT`, `LINENUMBERS`,
`FONTSET=`, `SIZE=`, `WIDTH=`, `HEIGHT=`, `LEFT=`, `TOP=`, `COLOR_HEADING=RRGGBB` … `COLOR_HTML=`).

## mdtohtml

```
mdtohtml [FROM|FILE] <datei.md> [TO|OUTFILE <datei.html>] [TEMPLATE <datei>]
         [CHARSET|ENCODING <name>] [TITLE <text>] [DIALECT|MODE GitHub|CommonMark]
```

* Ohne `FROM` wird von der Standardeingabe gelesen, ohne `TO` auf die Standardausgabe geschrieben.
* `DIALECT`: `GitHub` (Standard; Tabellen, Durchstreichen, Aufgabenlisten, Autolinks, Fußnoten)
  oder `CommonMark`.
* `CHARSET` landet nur im `<meta>`-Tag, der Text wird nicht umkodiert. Standard: `UTF-8`, wenn
  die Eingabe gültiges UTF-8 mit Nicht-ASCII-Zeichen ist, sonst `ISO-8859-1`.
* `TITLE`: Standard ist die erste `#`-Überschrift, sonst der Dateiname.

Hinweis: Die Pascal-Version hatte Unix-Optionen (`-f`, `-o`, `-t` …). Diese Version benutzt die
übliche AmigaDOS-Schablone; `FILE`, `OUTFILE`, `ENCODING` und `MODE` bleiben als Aliase erhalten.

### Templates

HTML-Datei mit den Platzhaltern `$title$`, `$encoding$` (oder `$charset$`) und `$body$`, siehe
`test/template.html`. MDEdit verwendet das Template für Vorschau und Export.

## Icons

Das Archiv hat klassische Icons; vollständige Sätze im **GlowIcons**- und **NewIcons**-Stil
liegen in `Icons/` (Doppelklick auf `UseGlowIcons`, `UseNewIcons` oder `UseClassic`).
`tools/icons.py` zeichnet sie mit dem Icon-Werkzeug von html.gadget
(`html_gadget/tools/mkicons.py`).

![Icon-Stile: klassisch, GlowIcons, NewIcons (normal und ausgewählt)](icons/preview.png)

## Bauen

Benötigt [bebbos amiga-gcc](https://codeberg.org/bebbo/amiga-gcc) unter `/opt/amiga` (mit NDK 3.2).

```
git submodule update --init
make                # bin/MDEdit, bin/mdtohtml
make check          # Konverter-, Scroll-Sync- und Highlighting-Tests auf dem Host (ASan)
make check-update   # gewollte Ausgabeänderungen als neue Referenz übernehmen
make dist           # Aminet-Archiv dist/MDTools.lha (+ MDTools.readme)
make icons          # Beispiel-Icons aller Stile in icons/, Vorschau in icons/preview.png
```

Alle Amiga-Quellen und Testdateien sind **ISO-8859-1** kodiert; `make` bricht bei UTF-8 ab.
md4c und die Header von html.gadget sind Git-Submodules (`md4c/`, `html_gadget/`), beide auf ein
Release-Tag gepinnt.

## Lizenz

MIT, siehe [LICENSE](LICENSE). md4c steht ebenfalls unter MIT (`md4c/LICENSE.md`).
