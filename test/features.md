# Markdown-Beispiel für MDEdit

Dieses Dokument zeigt, was **MDEdit** und `mdtohtml` darstellen.
Umlaute (äöü ÄÖÜ ß) bleiben in ISO-8859-1 erhalten, Entities wie &copy; und &#228; auch.

## Textauszeichnung

*kursiv*, **fett**, ***beides***, ~~durchgestrichen~~ und `Code`.
Ein Zeilenumbruch mit zwei Leerzeichen  
am Zeilenende.

## Listen

* Punkt eins
* Punkt zwei
  1. verschachtelt
  2. nummeriert
* Punkt drei

- [x] erledigt
- [ ] offen

## Zitat und Code

> Ein Zitat
> über zwei Zeilen.

```c
int main(void)
{
    return 0;
}
```

    eingerückter Code

## Tabelle

| Links | Mitte | Rechts |
|:------|:-----:|-------:|
| a     | b     | c      |
| 1     | 2     | 3      |

## Links und Bilder

[Aminet](http://aminet.net), <https://www.ubergeek.de>, www.amiga.org,
[Anker](#tabelle) und ein Bild: ![Boing](boing.gif)

Fußnote[^1].

[^1]: Der Text der Fußnote.

---

<p align="center">Eingebettetes <b>HTML</b> wird durchgereicht.</p>
