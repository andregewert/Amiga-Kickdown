# Amiga-MDTools – Hinweise für Claude

MDEdit (ReAction-Markdown-Editor mit HTML-Vorschau) und mdtohtml (CLI) für AmigaOS 3.2,
cross-kompiliert mit bebbos amiga-gcc. Überblick, Optionen und Build in `README.de.md`.

## Aufbau

- `src/mdconv.c`: gemeinsamer Konverter (reines ANSI-C, baut auch auf dem Host). Ruft md4c
  auf, vergibt Überschriften-IDs, setzt das Template zusammen.
- `src/mdtohtml.c`: CLI (ReadArgs, dos.library). `src/fileio.c`: Datei-I/O für beide Programme.
- `src/mdedit.c`: Programmlogik des Editors, `src/gui.c`: Fenster, Menü, Speedbar,
  `src/sync.c`: Scroll-Synchronisation (Überschriften als Fixpunkte, Umbruch des Editors wird
  geschätzt und an `GA_TEXTEDITOR_Prop_Entries` kalibriert), `src/highlight.c`: Syntax-Hook,
  `src/settings.c`: Einstellungen in den Tooltypes des Programm-Icons, `src/prefswin.c`:
  Einstellungsfenster (listbrowser + page.gadget). Gruppen dort komplett per Tagliste bauen:
  `CHILD_Label` gilt nur für das Kind in derselben Tagliste, `LAYOUT_AddChild` per OM_SET erst
  ab V47. Gadgets auf verdeckten Seiten mit `SetPageGadgetAttrs()` ändern.
- `src/splash.c`: Startfenster, nur Intuition/graphics (öffnet vor den ReAction-Klassen);
  `splash_step()` an jeder Ladestufe, die Schrittzahl steht in `main()`.
- Formatierung (Toolbar, Menü *Format*): `src/mdformat.c` ändert den Text (reines ANSI-C,
  Host-Test `test/hostfmt.c`), `src/format.c` verbindet es mit texteditor.gadget (Positionen
  x/y = Zeichen im Absatz/Absatznummer, Absatz = Zeile der Datei). Die Reihenfolge von
  `CMD_BOLD`…`CMD_QUOTE` (mdedit.h) entspricht `FMT_...` (mdformat.h).
- Neue Einstellung: Feld in `struct Settings`, Standard in `settings_default()`, Tooltype in
  `settings_from_tooltypes()` und `settings_save_icon()`, Gadget in `prefswin.c`, Übernahme in
  `apply_settings()` (mdedit.c), Test in `test/hostset.c`.
- Syntax-Hook (`GA_TEXTEDITOR_HighlighterHook`, V47): läuft beim Tippen im input.device-Kontext,
  also kein DOS, kein malloc, wenig Stack. `HighlightSetFormat(obj, pos, end, style)`: `end`
  exklusiv, Stile werden verodert, eine Farbe (`TBSTYLE_SETCOLOR | n << 8`, n = Eintrag n-1 von
  `GA_TEXTEDITOR_ColorMap`) ersetzt die vorige. Der Rückgabewert des Hooks ist der Status der
  Zeile (offene Code-Umzäunung); ändert er sich, ruft die Klasse den Hook für die Folgezeilen
  auf. Das wurde im Maschinencode der OS3.2-Klasse nachgesehen, die Autodocs sagen es nicht.
- `md4c/`: Git-Submodule, auf ein Release-Tag gepinnt. Nicht verändern; Anpassungen gehören
  nach `mdconv.c`. `src/entity_stub.c` ersetzt md4cs `entity.c` (Entities bleiben wörtlich
  stehen, `MD_HTML_FLAG_VERBATIM_ENTITIES`).
- `html_gadget/`: Git-Submodule (Amiga-HTML-Gadget); benutzt werden die Header aus
  `html_gadget/include` und das Icon-Werkzeug `html_gadget/tools/mkicons.py`. Zur Zeit auf
  einem Commit nach `v1.0` (mkicons.py gibt es erst seitdem); sobald html_gadget 1.1
  getaggt ist, auf das Tag setzen. Braucht MDEdit neue Attribute, das Submodule auf
  ein neueres Tag/Commit setzen (`git -C html_gadget checkout <tag>`, dann `git add html_gadget`).
  Nicht im Submodule ändern; Gadget-Änderungen gehören ins Projekt `~/Dokumente/html_gadget`.

## Übersetzungen (locale.library)

- Kein sichtbarer Text fest im Code: jeder Text steht in `catalogs/MDEdit.cd` (Englisch, eingebaut)
  und in `catalogs/deutsch.ct`, im Code `S(MSG_...)` (`src/locale.c`). `src/strings.h` erzeugt
  `make` aus der `.cd` (`tools/catcomp.py`); die Datei ist eingecheckt, nicht von Hand ändern.
- Neue Texte ans Ende der `.cd` oder an passender Stelle einfügen, die Nummern ergeben sich aus
  der Reihenfolge. Werden Texte entfernt oder umsortiert, `CATALOG_VERSION` in `locale.c` und die
  Version in allen `.ct` erhöhen, sonst lädt ein alter Katalog falsche Texte.
- Platzhalter (`%s`, `%lu` …) müssen in Übersetzungen gleich bleiben, `catcomp.py` prüft das.
- Tastenkürzel (`_`) innerhalb eines Fensters eindeutig halten, auch gegenüber den immer
  sichtbaren Knöpfen (Einstellungen: Save/Use/Cancel); Labels in Formularen enden mit einem
  Leerzeichen (Abstand zum Gadget).
- Kataloge werden im Format der OS3.2-Kataloge (CatComp) geschrieben; das wurde an
  `installer.catalog` der OS3.2-CD geprüft.

## Zeichenkodierung: ISO-8859-1

- Amiga-Quellen, Kataloge und Testdateien (`src/`, `catalogs/`, `test/`, `package/`, `Makefile`,
  `LICENSE`) sind
  **ISO-8859-1**. Ausnahmen (UTF-8): `README*.md`, `CLAUDE.md` und `test/utf8.*` (testet die
  Zeichensatzerkennung).
- Namen richtig schreiben, auch in C-Quellen: „André Gewert“ (é als ISO-8859-1 0xE9), nicht
  „Andre“. Sonst in C-Kommentaren möglichst ASCII. Dateien mit Umlauten mit Python schreiben
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
  Gadget-Attrappen aus `test/hoststubs.h` und den leeren Headern in `test/hostinc/` ein) und
  `test/hosthl` (Highlighting, Vergleich mit `test/<name>.hl`) und `test/hostset` (Schreiben und
  Lesen der Tooltypes: fremde Einträge und NewIcons-Block müssen erhalten bleiben) und
  `test/hostfmt` (Formatierungsbefehle).
  Gewollte Änderungen mit `make check-update` übernehmen und den Diff prüfen. Neue
  `test/*.md` werden automatisch mitgetestet.
- Vor einem Commit: `make` und `make check`.
- GUI-Tests auf dem Amiga (Amiberry) macht der Benutzer selbst; Amiberry nicht mit seiner
  Konfiguration oder seinem Festplatten-Image starten. Ungetestete Annahmen über
  ReAction-Klassen im Commit/Bericht kennzeichnen.

## Icons und Aminet-Paket

- `tools/icons.py` registriert die Motive von MDEdit und Markdown-Dokumenten in
  `html_gadget/tools/mkicons.py` (Stile, Dateiformat, Vorschau kommen von dort). Die Dateien in
  `icons/` sind Beispiele und werden mit `make icons` erzeugt, nie von Hand bearbeitet.
- `tools/mkdist.py` (`make dist`) baut `dist/MDTools.lha` samt Icons, Doku (README*.md nach
  ISO-8859-1) und Quellen (inkl. md4c und html.gadget-Headern, `md4c/VERSION`). Die Quellen im
  Archiv müssen ohne Git bauen.
- `package/MDTools.readme` ist die Aminet-Readme: ISO-8859-1, Zeilen höchstens 78 Zeichen.
  Für Anwender sichtbare Änderungen englisch (`History`) und deutsch (`Versionsgeschichte`)
  unter der Version aus `VERSION_TEXT` eintragen und `Version:` anpassen.

## Versionen

- `$VER` in `src/mdedit.c` (`VERSION_TEXT`) und `src/mdtohtml.c` bei Änderungen erhöhen.
- Datumsangaben immer `TT.MM.JJJJ`, Tag und Monat zweistellig, z. B. `02.10.2026`.
