# Style Guide für ReAction-Anwendungen (AmigaOS 3.2)

Vorgaben für Programme mit ReAction-Oberfläche, entstanden bei MDEdit (Amiga-MDTools). Gedacht als
Arbeitsgrundlage für KI-unterstützte Projekte: Die Regeln sind verbindlich, sofern das Projekt
nichts anderes festlegt. Die Begründungen stehen dabei, damit Ausnahmen bewusst entschieden werden.

Zielsystem: AmigaOS 3.2 (ReAction-Klassen V47), Cross-Build mit bebbos amiga-gcc und NDK 3.2.
Ältere Klassenversionen werden unterstützt, wo es ohne großen Aufwand geht. Was V47 voraussetzt,
muss einen Rückfall haben oder gekennzeichnet sein.

---

## 1. Grundsätze

1. **Der Anwender behält die Kontrolle.** Was Zeit kostet oder Geschmackssache ist, wird
   einstellbar: Toolbar-Darstellung, Startfenster, Knopfrahmen, eingeblendete Leisten.
2. **Sofort wirksam, wo es sauber geht.** Lässt sich eine Änderung nur beim nächsten Start
   umsetzen, sagt das der Einstellungsdialog (Hinweiszeile) und die Statuszeile.
3. **Systemfarben und -schriften respektieren.** Farben werden aus den Bildschirmstiften
   abgeleitet, nicht fest verdrahtet. Schriften kommen vom Bildschirm.
4. **Nichts ist nur über einen Weg erreichbar.** Jeder Toolbar-Befehl steht auch im Menü, die
   wichtigen haben ein Tastenkürzel.
5. **Moderne Muster, Amiga-typisch umgesetzt.** Beispiele sind flache Toolbars,
   Overflow-Listen, ausgegraute Knöpfe und ein Splash-Fenster, alles mit den Bordmitteln von
   OS 3.2 und ohne Fremdklassen.

## 2. Texte, Sprache, Zeichensatz

- **Kein sichtbarer Text im Code.** Alle Texte stehen in einer Katalogbeschreibung (`.cd`,
  Englisch eingebaut) und in Übersetzungen (`.ct`, mindestens Deutsch). Der Code holt sie über
  `locale.library` (`OpenCatalog` mit `OC_BuiltInLanguage "english"`) und eine Funktion `S(id)`.
- **Katalog-Nummern** ergeben sich aus der Reihenfolge. Neue Texte kommen ans Ende der Datei.
  Werden Texte entfernt oder umsortiert, steigt die Katalogversion im Code und in allen `.ct`.
- **Platzhalter** (`%s`, `%ld`, …) bleiben in Übersetzungen gleich, das Werkzeug prüft das.
- **Kataloge im CatComp-Format von OS 3.2** (am Original-Katalog der CD geprüft): Die Länge
  zählt das Null-Byte nur mit, wenn das Auffüllen auf 4 Bytes keines hinzufügen würde.
- **Tastenkürzel** (`_` im Text) sind innerhalb eines Fensters eindeutig. Das gilt auch gegenüber
  Knöpfen, die immer sichtbar sind, z. B. Speichern/Benutzen/Abbrechen im Einstellungsfenster.
  Das ist in jeder Sprache einzeln zu prüfen.
- **Beschriftungen in Formularen** enden mit einem Leerzeichen, als Abstand zum Gadget
  (label.image hat keinen eigenen Rand).
- **Zeichensatz:** Amiga-Quellen, Kataloge, Installer und Aminet-Readme sind ISO-8859-1. Nur
  Markdown-Dokumentation für GitHub ist UTF-8. Ein `make`-Ziel prüft auf versehentliches UTF-8.
- **Namen richtig schreiben**, auch in C-Quellen und Versionsstrings: „André Gewert“ (é als
  0xE9), nie „Andre“.
- **Datumsangaben:** `TT.MM.JJJJ`, Tag und Monat zweistellig (`02.10.2026`), auch in `$VER`.
- **Auslassungspunkte** als „...“ schreiben, weil ISO-8859-1 kein „…“ hat. Das © (0xA9) ist
  erlaubt.
- **Installer** mit Sprachauswahl, der die Kataloge nach `LOCALE:Catalogs/<Sprache>/` kopiert.

## 3. Fenster

### 3.1 Hauptfenster
- `window.class` mit Iconify-Gadget (`WINDOW_IconifyGadget`, braucht einen AppPort). Das
  Programm-Icon dient als AppIcon. Die Signalmaske wird in jeder Schleifenrunde neu gelesen, weil
  sich der Port nach dem Iconify ändert.
- AppWindow: Auf das Fenster gezogene Dateien werden geöffnet.
- Größe und Position sind einstellbar (0 bzw. -1 = automatisch). Ein Knopf „Aktuell“ übernimmt
  die Werte des offenen Fensters. Gespeicherte Positionen werden in den Bildschirm geschoben.
- Statuszeile unten, rechts daneben die Cursorposition.
- Hilfe-Bubbles über `WINDOW_HintInfo` und `WINDOW_GadgetHelp`.

### 3.2 Dialoge (Rückfragen, Meldungen)
- **Zentriert über dem Hauptfenster** (`WINDOW_RefWindow` + `WPOS_CENTERWINDOW`), nicht dort,
  wo `EasyRequest()` sie hinsetzt. `EasyRequest()` bleibt nur als Rückfall.
- **Modal:** Das Hauptfenster zeigt den Wartezeiger, seine Eingaben werden verworfen
  (`WM_HANDLEINPUT` leerlaufen lassen).
- **Knöpfe so breit wie ihr Text** (`BUTTON_TextPadding`, `CHILD_WeightedWidth 0`):
  - Die positive Antwort steht links, die negativen rechts, dazwischen eine leere Gruppe.
  - Ein einzelner Knopf eines zentrierten Infodialogs wird zentriert.
  - Return wählt den ersten Knopf, Esc und das Schließgadget den letzten.
- **Der Text steht in einem hellen, eingedrückten Feld:**
  - Füllung hellgrau, auf halbem Weg zwischen Fensterhintergrund und SHINEPEN, also nicht
    Weiß. Der Stift wird mit `ObtainBestPen` geholt und wieder freigegeben.
  - Rahmen `BVS_BUTTON` mit `LAYOUT_BevelState IDS_SELECTED`. Nicht `BVS_FIELD`, der wirkt
    nicht eingedrückt.
- Dateinamen können `_` enthalten, deshalb `LABEL_Underscore 0` für Meldungstexte.

### 3.3 Werkzeugfenster (nicht modal, z. B. Suchen/Ersetzen)
- Das Fenster bleibt neben dem Hauptfenster offen. Dessen Signale werden in der Hauptschleife
  mitbedient.
- **Fokus aufs erste Eingabefeld:** `ActivateLayoutGadget()` mit der Wurzel-Layout-Gruppe als
  erstem Argument. Weil `ActivateWindow()` asynchron ist, wird es bei `WMHI_ACTIVE` wiederholt.
- **Statuszeile im Fenster** als schreibgeschützter `button.gadget` mit `BVS_NONE` und
  `BUTTON_Transparent`, damit das Muster des Fensterhintergrunds durchscheint.
- Aktionen links, „Schließen“ rechts, wie bei den Dialogen.

### 3.4 Verhalten bei Größenänderung
- Zeilen eines Formulars behalten ihre Höhe (`CHILD_WeightedHeight 0`) und bleiben oben
  zusammen, in einer eigenen Gruppe.
- Eine leere Gruppe am Ende nimmt den restlichen Platz. Knopfzeilen bleiben am unteren Rand.
- Eingabefelder dürfen in die Breite wachsen, Checkboxen und Knöpfe nicht.

### 3.5 Startfenster (Splash)
- Es erscheint, wenn der Start spürbar dauert (Klassen, Gadgets, Bilder), und ist abschaltbar
  (Einstellung + Tooltype `NOSPLASH`).
- **Nur Intuition und graphics.library**, damit es vor den ReAction-Klassen erscheinen kann.
  Rahmenlos, mittig, nicht aktiviert (kein Fokusklau).
- **Inhalt:**
  - links das Programm-Icon (`LayoutIconA`/`DrawIconState`, icon.library V44, sonst
    weglassen),
  - rechts der Name groß in einer echten (nicht skalierten) Größe der Bildschirmschrift oder von
    Helvetica (24/18/15 Pixel, sonst fett), darunter Untertitel, Version und Copyright,
  - unten Statuszeile und Fortschrittsbalken (FILLPEN, eingedrückt).
- Hintergrund hellgrau mit doppelter 3D-Kante (außen erhaben, innen eingedrückt), Titel in einem
  dunklen Akzentblau.
- Es schließt sich, sobald das Hauptfenster offen ist, und vor jeder Fehlermeldung.

### 3.6 Über-Fenster (About)
- Es übernimmt den Kopf des Startfensters mit demselben Zeichencode. Darunter folgen eine
  eingeprägte Linie, die Details (Website, E-Mail, Lizenz, Versionen der verwendeten
  Bibliotheken/Gadgets) und ein zentrierter OK-Knopf.
- Website und Mailadresse des Autors stehen immer drin.
- Modal und zentriert wie die Dialoge. Ein einfacher Requester bleibt als Rückfall.

## 4. Einstellungen

### 4.1 Speicherort
- **Tooltypes des Programm-Icons** (`PROGDIR:<Programm>`), auch beim Start aus der Shell.
  Shell-Argumente und Tooltypes eines Projekt-Icons gehen vor.
- Beim Schreiben bleiben fremde Tooltypes und der NewIcons-Block (`*** DON'T EDIT …`) an
  ihrer Stelle. Neue Einträge kommen vor den NewIcons-Block.
- Ein Wert, der zum Standard wird, bleibt als deaktivierter Eintrag `(KEY=Wert)` stehen. So zeigt
  das Icon weiter, was es einzustellen gibt.
- Schalter heißen so, dass der Standard ohne Eintrag gilt (`NOSPLASH`, `NOSYNC`,
  `NOFORMATBUTTONS`). Auswahlwerte sind Wörter (`TOOLBAR=IMAGES|BOTH|TEXT`), Farben `RRGGBB`.
- Jede neue Einstellung bekommt: ein Feld in der Struktur, einen Standardwert, Lesen und
  Schreiben der Tooltypes, ein Gadget, die Übernahme zur Laufzeit und einen Host-Test.

### 4.2 Einstellungsfenster
- **Links eine Kategorienliste** (`listbrowser`), **rechts die Seite** (`page.gadget`). Jede
  Seite ist eine Gruppe mit `BVS_GROUP`-Rahmen und dem Kategorienamen als Titel.
- **Unten** „Speichern“ und „Benutzen“ links, „Abbrechen“ rechts. Speichern schreibt ins Icon,
  Benutzen gilt für die Sitzung.
- **Abstände in „virtuellen Pixeln“** (layout.gadget skaliert sie), als Konstanten:
  - innen im Seitenrahmen 8 seitlich und 6 oben/unten,
  - zwischen den Zeilen 4,
  - zwischen Fensterrand, Liste, Rahmen und Knöpfen 6.
- **Gadgets behalten ihre Höhe**, eine leere Gruppe am Ende der Seite nimmt den Rest auf.
- **Hinweiszeilen** (label.image) für Einstellungen, die erst beim nächsten Start wirken, direkt
  unter diesen Einstellungen. Was sofort wirkt, steht darunter.
- Abhängige Gadgets werden ausgegraut, wenn sie nicht gelten (z. B. Schriftwahl nur beim
  TrueType-Renderer).
- Einstellungen, die es auch im Menü gibt (Häkchen-Einträge), halten Menü und Dialog synchron.

## 5. Menüs

- Aufbau aus einer Tabelle (Typ, Text-ID, Kürzel, Flags, Befehl). Die Texte kommen zur
  Laufzeit aus dem Katalog, die Kürzel sind in allen Sprachen gleich.
- Kürzel nach Amiga-Gewohnheit: N/O/S/A/Q, X/C/V, Z/Y, F/G (Suchen/Weitersuchen), ? für Über.
  Neue Kürzel nicht doppelt vergeben.
- Schalter als `CHECKIT | MENUTOGGLE`. Ihr Zustand wird beim Aufbau aus den Einstellungen gesetzt.

## 6. Toolbar (speedbar.gadget)

### 6.1 Bilder
- **AISS** über `TBIMAGES:` mit drei Varianten: `<name>`, `<name>_s` (gedrückt), `<name>_g`
  (ausgegraut).
- **Alternativen:** Ein Eintrag kann mehrere Namen haben (`"a|b"`), der erste vorhandene wird
  geladen. AISS 4 hat mehr Motive als AISS Classic. Nur Motive nehmen, die es in beiden gibt
  oder für die ein Ersatz eingetragen ist. Die Liste der Classic-Motive steht in der
  AISS-Classic-Dokumentation.
- **Keine `list_`-Motive** in der Toolbar, sie sind kleiner als die übrigen.
- Gruppen durch Abstand trennen (`SBNA_Spacing 8`), z. B. Datei | Bearbeiten | Suchen |
  Formatieren | Vorschau | Programm.

### 6.2 Aussehen und Zustände
- **Flach:**
  - Kein Rahmen um die Leiste (`SPEEDBAR_BevelStyle BVS_NONE`).
  - Knöpfe ohne Rahmen (`SPEEDBAR_ButtonBevelStyle BVS_NONE`, V47), beim Drücken erscheint das
    `_s`-Bild (`SBH_IMAGE`).
  - Rahmen um die Knöpfe gibt es als Einstellung.
- **Knöpfe, die nichts bewirken würden, sind ausgegraut:** Speichern bei unverändertem
  Dokument, Rückgängig/Wiederholen ohne Schritte, Ausschneiden/Kopieren ohne Markierung.
  - Umgesetzt wird das über den Tausch gegen das `_g`-Bild plus `SBNA_Disabled`. Das Gitter der
    Speedbar zeichnet bevel.image bei `BVS_NONE` nicht.
  - Der Zustand wird im Tick abgefragt. Die Leiste wird nur bei einer echten Änderung neu
    gezeichnet (Liste lösen, ändern, anhängen, `RefreshGList`).
- **Hilfe-Bubbles** zu jedem Knopf, mit dem Markdown/Kürzel in Klammern, wenn hilfreich.

### 6.3 Darstellung: Bilder, Bilder und Text, nur Text
- speedbar.gadget 47.x speichert `SBNA_Text`, **zeichnet ihn aber nie** (im Maschinencode von
  47.8 und 47.10 geprüft). Texte sind deshalb `label.image`-Objekte, die als `SBNA_Image`
  übergeben werden.
- **Bilder und Text:** das Bild mit dem Text darunter, in der kleinsten echten Größe der
  Bildschirmschrift (z. B. Helvetica 9).
- **Nur Text:** Es wird kein Bild geladen, das Programm startet schneller.
- Ein fehlendes Bild führt in jedem Modus zum Text.
- Mit Texten wird die Leiste zu breit, deshalb bekommen die Formatierungsknöpfe eine zweite
  Leiste. Für die Toolbar gibt es kurze Beschriftungen (eigene Katalogtexte).
- Ausgegrauter Text verwendet einen Stift zwischen Text- und Hintergrundfarbe.

### 6.4 Zu schmale Fenster
- Die Speedbar blendet Knöpfe, die nicht passen, einfach aus und hat kein Overflow-Menü.
- **Overflow:** Rechts neben der Leiste steht ein Drop-down-`chooser` ohne Titel, nur der Pfeil
  (Breite 20, Höhe einer Textzeile).
  - Seine Liste enthält die abgeschnittenen Befehle, ausgegraute auch dort ausgegraut.
  - Zwischen den Einträgen verschiedener Leisten steht eine Trennlinie (`CNA_Separator`).
  - Ermittelt wird das über `SPEEDBAR_Visible` im Tick. Die Liste wird nur bei Änderung neu
    gebaut. Passt alles, ist der Pfeil ausgegraut.

### 6.5 Ausblendbare Knopfgruppen
- Optionale Gruppen (z. B. die Formatierungsknöpfe) sind per Einstellung und Menü-Häkchen sofort
  ausblendbar:
  - Bei einer Leiste werden die Knoten aus der Liste genommen bzw. an ihrer Stelle wieder
    eingefügt.
  - Bei zwei Leisten wird die zweite aus dem Layout entfernt bzw. neu angelegt; das braucht V47,
    sonst „beim nächsten Start“.

## 7. Farben und Schrift

- Farben werden mit `ObtainBestPen` geholt und mit `ReleasePen` wieder freigegeben. Bei zu wenig
  Farben wird auf einen sichtbaren Systemstift zurückgefallen, nie auf den Hintergrund.
- **Hellgrau** für Flächen, die sich abheben sollen: Mitte zwischen BACKGROUNDPEN und SHINEPEN.
- **Syntax-/Akzentfarben** gefällig und gut lesbar auf dem Workbench-Grau, mindestens 4.5:1
  Kontrast zu schwarzem Text, z. B.:

  | Rolle | Farbton |
  |---|---|
  | Überschriften | Marine `#0A2A8A` |
  | Code | Dunkelgrün `#0A521E` |
  | Zitate | Schiefer `#3A4A66` |
  | Markierungen | Dunkelrot `#8C1810` |
  | Links | Blau `#0038C0` |
  | URLs | Petrol `#005266` |
  | HTML | Pflaume `#7A1F6E` |

- Farben sind einstellbar (Getcolor-Gadgets) mit Knopf „Standardfarben“.
- Schriften kommen vom Bildschirm. Größere oder kleinere Varianten werden nur in echten (nicht
  skalierten) Größen verwendet (`AvailFonts`, `FPF_DESIGNED`).

## 8. ReAction: Technische Regeln und Fallstricke

- **Taglisten vollständig bauen:** `CHILD_Label` gilt nur für das Kind in derselben Tagliste.
  `LAYOUT_AddChild` per `OM_SET` gibt es erst ab V47, und dann nur über `SetGadgetAttrs()`.
  Danach `RethinkLayout()`.
- **`LAYOUT_RemoveChild` gibt das Objekt frei.** Wird es wieder gebraucht, wird es neu angelegt.
- **Ein neu hinzugefügtes Kind zeichnet sich sofort**, noch bevor das Layout es platziert hat,
  also bei 0/0 über dem Fenstertitel. Vorher `GA_Left/Top/Width/Height` auf die Zielposition
  setzen.
- **`CHILD_CacheDomain FALSE`** für Kinder, deren Mindestgröße sich ändert (z. B. eine Speedbar,
  deren Knöpfe kommen und gehen).
- **Versteckte Seiten** eines `page.gadget` nur mit `SetPageGadgetAttrs()` ändern.
- **Speedbar-Knoten** dürfen nur geändert werden, während die Liste gelöst ist
  (`SPEEDBAR_Buttons, ~0`). Nach dem Anhängen zeichnet sie sich nicht selbst neu, also
  `RefreshGList()`. Die Speedbar gibt weder Knoten noch Bilder frei.
- **chooser:** Die Nummer des gewählten Eintrags zählt Trennlinien mit. Knoten mit
  `CNA_UserData` versehen statt über die Position zuzuordnen.
- **Stiftwerte** sind echte Bildschirmstifte (z. B. beim Highlighter-Hook von
  texteditor.gadget). Eine eigene Farbtabelle der Klasse nicht ungeprüft überschreiben.
- **Klassen** mit Mindestversion öffnen (meist 44). Fehlt eine, gibt es einen Requester mit
  ihrem Namen, und das Programm beendet sich sauber.
- **Bibliotheksbasen** explizit mit `= NULL` initialisieren, sonst ziehen COMMON-Symbole die
  Auto-Open-Stubs nach.
- **Stack:** `__stack` setzen und mit `-Wl,-u,___stkinit` linken. Workbench-Projekt-Icons geben
  oft nur 4 KB, zu wenig für ReAction, ASL und Datatypes. Große Puffer nicht auf den Stack legen.
- **Hooks im input.device-Kontext** (z. B. Highlighter): kein DOS, kein `malloc`, wenig Stack.
- **Ergebnisse von ARexx-Befehlen der Gadgets** nur freigeben, wenn sicher ist, dass sie
  allokiert sind. Im Zweifel Befehle ohne Ergebnis oder andere Methoden verwenden.

## 9. Arbeitsweise und Qualitätssicherung

- **Wenn die Autodocs schweigen:** das Verhalten der OS-3.2-Klassen im Maschinencode nachsehen
  (Klassen von der OS-3.2-CD oder, nur lesend, aus dem Festplatten-Image des Anwenders). Das
  Ergebnis kommt in Code-Kommentar und Projektnotizen.
- **Ungetestete Annahmen** über ReAction-Klassen im Bericht und in der Commit-Nachricht
  kennzeichnen.
- **GUI-Tests macht der Anwender** im Emulator. Den Emulator nie selbst mit der Konfiguration
  oder dem Festplatten-Image des Anwenders starten. Screenshots des Anwenders auswerten.
- **Logik vom GUI trennen:** Textverarbeitung, Konverter, Einstellungen und Formatierung als
  reines ANSI-C, das auch auf dem Host baut. Host-Tests mit ASan/UBSan laufen über
  `make check`, Referenzausgaben über `make check-update`.
- **Vor jedem Commit** `make` (inkl. Zeichensatzprüfung) und `make check`.
- **Commit und Push nur auf Aufforderung.** Commit-Nachrichten englisch, mit Begründung, was und
  warum.
- **Versionen:** `$VER` bei Änderungen erhöhen. Die Aminet-Readme (ISO-8859-1, höchstens 78
  Zeichen pro Zeile) bekommt die Versionsgeschichte englisch und deutsch.
- **Dokumentation:** README englisch und deutsch, gleiche Inhalte. Neue Funktionen,
  Einstellungen und Tooltypes werden dort ergänzt. Projektnotizen für die KI (z. B.
  `CLAUDE.md`) halten Aufbau, Konventionen und gewonnene Erkenntnisse fest.
- **Code-Stil:** wie der umgebende Code. Kommentare englisch und knapp, sie erklären das Warum.
  In C-Kommentaren möglichst ASCII.
