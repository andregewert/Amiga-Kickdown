# Amiga-MDTools – Hinweise für Claude

MDEdit (ReAction-Markdown-Editor mit HTML-Vorschau) und mdtohtml (CLI) für AmigaOS 3.2,
cross-kompiliert mit bebbos amiga-gcc. Überblick, Optionen und Build in `README.de.md`.

## Aufbau

- `src/mdconv.c`: gemeinsamer Konverter (reines ANSI-C, baut auch auf dem Host). Ruft md4c
  auf, vergibt Überschriften-IDs, setzt das Template zusammen.
- `src/mdtohtml.c`: CLI (ReadArgs, dos.library). `src/fileio.c`: Datei-I/O für beide Programme.
- `src/mdedit.c`: Programmlogik des Editors, `src/gui.c`: Fenster, Menü, Speedbar,
  `src/sync.c`: Scroll-Synchronisation (Überschriften als Fixpunkte, Umbruch des Editors wird
  geschätzt und an `GA_TEXTEDITOR_Prop_Entries` kalibriert).
- `md4c/`: Git-Submodule, auf ein Release-Tag gepinnt. Nicht verändern; Anpassungen gehören
  nach `mdconv.c`. `src/entity_stub.c` ersetzt md4cs `entity.c` (Entities bleiben wörtlich
  stehen, `MD_HTML_FLAG_VERBATIM_ENTITIES`).
- `include/`: Kopien der öffentlichen html.gadget-Header aus `~/Dokumente/html_gadget/include`.
  Bei neuen html.gadget-Attributen von dort neu kopieren, nicht hier ändern.

## Zeichenkodierung: ISO-8859-1

- Amiga-Quellen und Testdateien (`src/`, `include/`, `test/`, `Makefile`, `LICENSE`) sind
  **ISO-8859-1**. Ausnahmen (UTF-8): `README*.md`, `CLAUDE.md` und `test/utf8.*` (testet die
  Zeichensatzerkennung).
- In C-Kommentaren und -Strings ASCII verwenden. Dateien mit Umlauten mit Python schreiben
  (`encoding='latin-1'`) oder mit `iconv -f UTF-8 -t ISO-8859-1` umwandeln.
- `make charcheck` (Teil von `make`) findet UTF-8-Sequenzen.

## Bauen und Testen

- `make`: Cross-Build nach `bin/`. md4c wird mit `-DMD4C_USE_ASCII` übersetzt (byteweise,
  Latin-1 und UTF-8 laufen unverändert durch).
- Stack: Beide Programme setzen `__stack` und linken mit `-Wl,-u,___stkinit`, damit libnix beim
  Start auf einen größeren Stack wechselt. Von der Workbench gibt ein Projekt-Icon oft nur 4 KB,
  zu wenig für ReAction, ASL und die Datatypes, die html.gadget auf unserem Stack aufruft.
  Große Puffer (Pfade) trotzdem nicht unnötig auf den Stack legen.
- Bibliotheksbasen explizit mit `= NULL` initialisieren (sonst COMMON-Symbole, die die
  Auto-Open-Stubs aus libstubs.a nachziehen). Die Basis von texteditor.gadget heißt
  `TextFieldBase`.
- `make check`: Konverter auf dem Host (ASan/UBSan), vergleicht `test/<name>.md` mit
  `test/<name>.expected`. Zusätzlich läuft `test/hostsync` (bindet `src/sync.c` mit den
  Gadget-Attrappen aus `test/hoststubs.h` und den leeren Headern in `test/hostinc/` ein).
  Gewollte Änderungen mit `make check-update` übernehmen und den Diff prüfen. Neue
  `test/*.md` werden automatisch mitgetestet.
- Vor einem Commit: `make` und `make check`.
- GUI-Tests auf dem Amiga (Amiberry) macht der Benutzer selbst; Amiberry nicht mit seiner
  Konfiguration oder seinem Festplatten-Image starten. Ungetestete Annahmen über
  ReAction-Klassen im Commit/Bericht kennzeichnen.

## Versionen

- `$VER` in `src/mdedit.c` (`VERSION_TEXT`) und `src/mdtohtml.c` bei Änderungen erhöhen.
- Datumsangaben immer `TT.MM.JJJJ`, Tag und Monat zweistellig, z. B. `02.10.2026`.
