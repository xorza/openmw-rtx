# Ayu Graphite for the launcher and the wizard

The launcher and the wizard are always dark, and dressed as palantir's stock dark theme dresses its
widgets: flat fills on a neutral ladder, hairline edges, small radii, one lime accent for focus,
checks and progress. The editor (`openmw-cs`) keeps upstream's look. Temporary: when the work lands,
each decision below moves into `docs/rtx/build.md` (or a page of its own beside it) and this file is
deleted.

Sources:

- palantir at `67955d19f` (`~/Projects/Darkroom/palantir`): `src/theme/palette.rs` (`Palette::DARK`,
  the border ladder), `src/theme/metrics.rs` (`Metrics::DEFAULT`), the widget recipes beside them
  (`button.rs`, `text_edit.rs`, `toggle.rs`, `tabs.rs`, `scrollbar.rs`, `progress_bar.rs`,
  `tooltip.rs`, `menu/`, `focus_ring.rs`, `separator.rs`) and `src/theme/tests/floors.rs`, cited as
  "palantir". Its `DARK` is the semantic layer of Ayu Graphite with palantir's own moves: a ground one
  rung under Ayu Graphite's `bg`, `text_muted` lifted to clear 4.5:1 on a field, and a lime accent.
- `~/Projects/ayu-graphite` (`ayu-graphite.toml`, `kde/build.py`): the hues palantir's palette has
  no role for, `error`, `warning` and `success`.
- Qt 6.11: [`QProxyStyle`](https://doc.qt.io/qt-6/qproxystyle.html), the
  [style sheet reference](https://doc.qt.io/qt-6/stylesheet-reference.html),
  [`QStyleHints::setColorScheme`](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop).
- Two well-regarded Qt programs that ship a dark look of their own: Qt Creator, a palette from a
  `.creatortheme` file and its drawing changed in a proxy style, no style sheet
  ([forum](https://forum.qt.io/topic/71183/about-colour-theme-and-layout-design-of-qt-creator)); OBS
  Studio 30.2, style sheets with variables substituted before Qt loads them
  ([wiki](https://github-wiki-see.page/m/obsproject/obs-studio/wiki/OBS-Studio-Theme-System)).

## What stands today

- `Platform::Application` (`components/platform/application.cpp`, upstream) is the `QApplication` of
  the launcher, the wizard and the editor. On Windows 11 or 10 with the system dark it switches to the
  `windows` style and loads `files/windows/QWindowsVistaDark/dark_win1x.qss` (630 lines each), and
  follows the system as it changes. Everywhere else it sets nothing.
- On Linux the static Qt has no D-Bus, so neither KDE's theme nor its own `kde` theme loads: the
  `generic` theme, the Fusion style, Qt's light palette (`qt.qpa.theme` log, this session).
- What the two programs are made of (their `.ui` files and code): 55 check boxes, 42 labels, 17 push
  buttons, 15 combo boxes, 10 spin boxes, 7 group boxes, 7 tool buttons, 4 radio buttons, 4 list
  widgets, 3 tab widgets, 3 progress bars, 3 line edits, 2 plain text edits, a tree widget, a
  toolbar, 4 context menus, a progress dialog, message boxes, and the wizard as `QWizard::ModernStyle`.
- What paints its own colour:
  - the launcher's toolbar sheet, `QToolBar { border: 0px; } QToolButton { min-width: 70px }`
    (`maindialog.cpp:28`);
  - an invalid profile name in `Qt::red` (`profilescombobox.cpp:45`, `textinputdialog.cpp:54`): 2.40:1
    on a field of `#454545`, under the 4.5:1 text needs.
- Images:
  - the toolbar's four page icons are 256 px Oxygen PNGs, in colour, which read on dark;
  - the fallback icon theme's 16 px SVGs (`files/launcher/icons/fallback`: new, clear, copy, delete,
    refresh) are drawn in `#000000`, which vanishes on dark;
  - the header logo is gold and reads on dark;
  - the wizard's watermark fades to transparency, not to white, so it fades into a dark page too.

## Decisions

- **The colours are palantir's `Palette::DARK`, held once in a table of the fork's**, with `error`,
  `warning` and `success` from Ayu Graphite's semantic layer, which palantir's palette has no role
  for. Every other colour is derived from the table in code: the border ladder, `text_muted` at
  46, 56 and 89 of 255, as palantir's is, and the selection wash. Refused:
  - a target in the ayu-graphite repo generating the table: that repo's semantic layer is not this
    look, whose ground, muted ink and accent are palantir's moves;
  - reading palantir's Rust source at build time: a build of one repo reaching into another.
- **Fusion, a full palette and a proxy style over Fusion; no style sheet**, as Qt Creator draws its
  dark theme. The palette carries every colour, so code that sets a widget's palette, the red invalid
  name among it, keeps working. The proxy style draws the dozen primitives that make the look, and
  Fusion draws the rest. Refused:
  - a style sheet with variables, as OBS does: a sheet that sets a property overrides what code
    sets in a widget's palette; a complex widget restyled by the box model takes all its
    subcontrols with it; and check marks and arrows become image files, a second copy of the
    colours;
  - Fusion and the palette alone: Fusion's gradients, bevels and 2 px corners are the look this
    replaces.
- **The colour scheme is asked for as dark** (`QStyleHints::setColorScheme(Qt::ColorScheme::Dark)`), so
  Windows draws a dark title bar and anything in Qt that reads the scheme reads dark. On Linux the
  compositor draws the frame.
- **The launcher and the wizard choose it, and the editor keeps upstream's behaviour.** The editor's
  tables, delegates and scene colours were made for upstream's palettes and would need an audit of
  their own. `Platform::Application` takes the look as an argument, so its Windows follow-the-system
  code runs for the editor alone.
- **The system's font, at the system's size.** palantir's 14 px body is its own interface's. Here the
  font is what the desk's settings and scaling chose; palantir's spacing steps set the paddings and
  gaps.
- **Glyph icons drawn in the text ink.** Recommended: the toolbar's four page icons and the five
  fallback glyphs replaced by Lucide's line icons (ISC licence), recoloured at load to `text` from one
  SVG each, so an icon is never a second copy of a colour. A new third-party asset, with its licence
  shipped in `files/licenses/` beside the others. Refused: the fallback glyphs left black, unreadable;
  the Oxygen PNGs kept beside line icons, two styles on one toolbar.
- **The red of an invalid name is the palette's `error`**, through a small header of the fork's that
  the two launcher sites read. Ayu Graphite's `#ff8b8c` reads 4.26:1 on a field's `#454545`, under
  4.5:1, so it is lifted at its own hue until it clears, as palantir lifts `text_muted` over Ayu
  Graphite's; the floor test holds it.

## The look, widget by widget (palantir's recipes)

| Widget | Rest | Hover | Pressed or focused | Disabled | Geometry |
|---|---|---|---|---|---|
| Push button, tool button | `element_mid`, `border_soft` | `element_strong` | `element_strong`, `border_focused` edge | `element`, `text_disabled` | radius 4, padding 12 × 4 |
| Default button (dialog's OK) | `accent`, `on_accent` ink | lighter accent | darker accent | as push button | radius 4 |
| Line edit, spin box, combo box | `element_mid`, `border_soft` | as rest | `border_focused` edge, same width | `element`, `border_soft` | radius 4, padding 8 × 4, hairline |
| Check box, radio | `element_mid`, `border_strong` | `element_strong` | checked: `accent` fill, `on_accent` mark | `element`, `border_soft` | radius 4 (radio round), mark stroke 2 |
| Tab bar | unselected: no fill, `text_muted` | `element_mid` | selected: `window_bg` chip, 2 px `accent` cap | `text_disabled` | top radius 6, band `surround` |
| List, tree, table | `window_bg` | `element_mid` row | selected: the selection wash | | row padding 8 × 4 |
| Scroll bar | track none, thumb `text_muted` at 115 | thumb at 166 | dragged: at 217 | | 8 px pill, gap 4 |
| Progress bar | track `element_mid` | | fill `accent` | | 6 px pill |
| Menu, tooltip | `element`, `border_mid`, shadow | row `element_mid` | | | radius 10 menu, 4 tooltip |
| Group box (no palantir recipe; a card's) | `border_soft` frame | | | | radius 6, title in `text_muted` |
| Focus | no dotted rectangle: a focused control's edge is `border_focused` | | | | 2 px ring on keyboard focus alone |

## Plan

Each batch ends with its tests green and screenshots of every launcher page and every wizard page
beside the last batch's, at 100 % and 200 % scale, on Linux and on Windows (192.168.0.8).

6. **Packaging and review.** The AppImage and the Windows folder carry what the programs load; the
   Steam Deck in game mode, where Qt opens on X11 under gamescope; `./omw gate`.

The work is about three to four days: the proxy style's dozen primitives and their tests are most of
it.

## Changed

- **Menus and tooltips square, not rounded at 10 and 4**: a rounded top-level window is a translucent one,
  which a desk without a compositor draws black around its corners.
- **Screenshots on Linux each batch, on Windows and at 200 % once, in the last batch**: the look is drawn by one
  style on every system, and the Windows machine builds the tree from scratch for each look.

