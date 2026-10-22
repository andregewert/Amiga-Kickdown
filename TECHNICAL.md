# Kickdown – technical notes

Notes that concern several modules: how the parts work together, and
findings about the OS 3.2 classes that more than one place depends on.
What belongs to one module is in that module's head comment (`@file`) or
at its functions. `CLAUDE.md` lists the modules and says which section to
read before which change.

## Class versions and fallbacks

`main()` (`kickdown.c`) opens the classes in this order and stops with a
requester if one is missing: asl.library 39, window.class, layout.gadget,
button.gadget, scroller.gadget, speedbar.gadget and bitmap.image 44,
texteditor.gadget 45. The other classes are opened when they are first
needed (label.image, chooser.gadget, the classes of the settings, find and
print windows; fuelgauge.gadget 44 only for printing).

Some features depend on newer versions and are switched off without them:

| Feature | Needs | Checked in |
|---|---|---|
| Syntax highlighting | texteditor.gadget V47 (`GA_TEXTEDITOR_HighlighterHook`) | `highlight_hook()` (`highlight.c`) |
| Formatting buttons shown later in a second row (toolbar with texts) | layout.gadget V47 (`LAYOUT_AddChild` with OM_SET) | `gui_show_format()` (`gui.c`) |
| Printing, PostScript and PDF | html.gadget/htmlttf.gadget 1.2 (`HTMLM_Export`, `HTMLM_PrintBegin`) | `gadget_can_print()` (`printwin.c`) |
| GitHub look (table lines, code backgrounds, quote bar), `FITIMAGES` | html.gadget 1.2 (CSS, `HTML_FitImages`) | not checked, older versions ignore it |
| Program icon in the splash and About window | icon.library V44 (`GetDiskObjectNew`) | `splash.c` (`settings.c` falls back to `GetDiskObject()`) |

html.gadget (htmlttf.gadget with `TTF`) is opened in any version, first
as `gadgets/<name>` (the class path), then as `PROGDIR:<name>`
(`open_html()`).

## Start, splash window and toolbar

The splash window (`splash.c`) draws with Intuition and graphics only, so
it opens before any class. `main()` computes the number of steps of its
progress bar: the 8 classes above, html.gadget, one per toolbar button
made at the start (`gui_tool_count()`), and the window. Every
`open_class()` and every button made in `make_buttons()` (`gui.c`) calls
`splash_step()`; a new class or button must keep the count right.

The toolbar (`gui.c`) is made once at the start, so the toolbar settings
apply from the next start:

- `TOOLBAR=TEXT` loads no AISS images at all, which is the fastest start;
  missing images also fall back to text buttons.
- With texts (`BOTH`, `TEXT`) the formatting buttons are a second
  speedbar below the first (`gui.rows`); with images only they are part of
  the one bar.
- The formatting buttons (`FORMATBUTTONS`, off by default) and their
  images are only made when they are shown, at the start or later with
  *Format/Show formatting buttons* (`make_format_buttons()`).
- Buttons that do not fit are listed in the overflow chooser at the right
  end (`GID_OVERFLOW`).

## Preview refresh, anchors and scroll sync

- The main loop counts IntuiTicks: after a change of the text it waits
  `REFRESH_DELAY` ticks (5, about half a second) without typing, then
  converts and sets the preview (`tick()` in `kickdown.c`), keeping the
  scroll position. A change of the window size converts again and resets
  the sync, since both views break lines anew.
- `mdconv.c` gives every heading a GitHub style `id` (repeated anchors get
  `-1`, `-2` ...). The same anchors serve three places: `#anchor` links in
  the preview (`follow_link()`), tables of contents in exported HTML, and
  the fixed points of the scroll sync (`sync.c`, via `HTML_Anchor`).
- Relative image paths in the preview are resolved against the drawer of
  the document.

## Settings and their precedence

Settings (`settings.c`) are tool types of the program icon, read also for
a Shell start (`GetProgramName()`). Then, in `kickdown.c`, the Shell
arguments (`shell_options()`) or the tool types of the project icon a
Workbench start was given (`wb_options()`) override them. A relative
`TEMPLATE` tool type is relative to the drawer of the icon it is in.
Saving writes only Kickdown's own tool types back into the program icon;
see the head of `settings.c` for how foreign entries and the NewIcons
block are kept. Page range and copies of the print window are not
settings.

The steps for a new setting are listed in `CLAUDE.md`.

## Printing and TurboPrint

All output goes through the preview gadget (html_gadget 1.2,
`printwin.c`): PostScript and PDF with `HTMLM_Export`, the printer with
`HTMLM_PrintBegin`/`HTMLM_PrintRender` and printer.device
`PRD_DUMPRPORTTAGS` with `DRPA_SourceHook`.

- **Printer, resolution:** only the default printer (unit 0). The density
  is the one of the printer settings; Kickdown offers no choice. Before
  the print Kickdown asks the driver for its resolution once with a
  classic `PRD_DUMPRPORT`, `SPECIAL_NOPRINT` and 1000 x 1000 mils
  (`printer_dpi()`), because `ped_X/YDotsInch` may still hold the values of
  the last print. The whole sheet is sent in exactly the driver's dots; the
  driver adds its hardware margins to Kickdown's margins.
- **Rendering:** htmlttf.gadget renders the page at the printer's
  resolution, at most `MAXRENDERDPI` (600), the source hook enlarges
  above that; html.gadget uses its bitmap fonts in the size that fits.
- **Driver name:** the print dialog shows the driver of the default
  printer from `ENV:Sys/Printer.prefs` (chunk PTXT) and does not open
  printer.device for it.
- **TurboPrint** (`TPMATCHWORD` in `pd_OldStk[2]`) does not know
  `PRD_DUMPRPORTTAGS`, and its `PRD_TPEXTDUMPRPORT` with RGB24 damaged
  memory here (broken checkbox images, only chip RAM left), as did queries
  with `SPECIAL_NOPRINT` and writing into `PrinterData`. Even
  `OpenDevice()`/`CloseDevice()` of TurboPrint's printer.device from
  Kickdown damaged memory (units above 0 were opened then, which it does
  not know as a device from OS 3.1 times). Therefore:
  - `turboprint_running()` recognizes TurboPrint without opening it:
    printer.device in the device list, `PrinterData` read under
    `Forbid()`. With TurboPrint, printer.device is never opened.
  - With TurboPrint there is no graphics printing: the print dialog
    disables the kind "Printer", and without a saved `PRINT_MODE` it
    starts with PostScript level 2 to `PS:` (TurboPrint's PostScript
    handler, whose Ghostscript renders better than a graphics dump).
  - TurboPrint's printer.device is resident: after a memory damage, test
    again only after a cold start.
- **Progress:** each page goes out with `SendIO()` while a modal progress
  window shows the pages and can stop the print; PostScript/PDF export
  reports its progress through a hook the same way.
