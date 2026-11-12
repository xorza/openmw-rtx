# The look, polished

A second pass over the launcher's and the wizard's look (`docs/rtx/look.md`), from every page of both rendered
in it: each `.ui` file loaded through `QUiLoader` under `Graphite::apply` on the offscreen platform, every tab
of a tab widget, and a sampler of what the files do not show (a default and a disabled button, fields, an open
combo box's list, a list with its scroll bar, a tree with its header, a message box, a menu). Temporary: when
the work lands, each decision moves into `docs/rtx/look.md` and this file is deleted.

Sources: palantir at `67955d19f` (its recipes, as for the first pass); Fluent 2's list and navigation items
(<https://fluent2.microsoft.design/components/web/react/core/list/usage>), a selected item a fill with an
accent indicator; Qt 6.11's `QStyle::SH_UnderlineShortcut` and the `PM_Layout*` metrics.

## What the pages show

- **Text sits on a field's edge.** A line edit, a spin box and a combo box start their text two or three
  pixels in (`1.00`, `800`, `JPG`, `A field`), where palantir pads a field 8 px.
- **A spin box's arrows are cramped**: two 10 × 6 chevrons stacked in a row's height, nearly touching, with
  nothing that says they are buttons until the pointer is on one.
- **A combo box's open list is the window's ground** (`#141414`), not a menu's (`#383838`): the palette given to
  its view in `polish` does not reach the popup's container, which paints the ground.
- **A list's rows are as tall as their text**, where every other row of the look has 8 px above and below; a
  dozen rows of plugins read as a wall.
- **A selected row is an olive wash** (the accent at 58 of 255 over the ground), legible at 4.5:1 but muddy, and
  the only place the accent is a tint rather than a mark.
- **A menu's first and last rows touch its edges**: Fusion's menu margins are nought, where palantir pads a menu
  4 px.
- **Mnemonic underlines show at rest** (`N̲ew Content List`, `B̲ack`, `N̲ext`), as on Linux they always do.
- **Layouts keep Fusion's spacing**, 6 between widgets and 9 at a page's edge, off palantir's 4 px grid.
- **Three kinds of icon remain from other looks**: the Data Directories list's "contains data" mark
  (`openmw-plugin.png`, Oxygen, in colour), the wizard's four Tango icons (installer, folder, locale, the
  dollar on Buy the game), and a message box's question, warning and error icons (Fusion's own, blue).
- **A rule** (`QFrame::HLine`, the Testing tab's, the wizard's method page) is drawn in Fusion's light grey, a
  step louder than every other edge.
- What reads well and stays: the tab chips and their cap, the check boxes and radios, the buttons and the
  default button, the pill scroll bar, the progress bar, the toolbar's line icons, the 16 px text and one row's
  height.

## Proposals

1. **Fields pad their text 8 px**, palantir's field inset: `SE_LineEditContents` and the spin box's and combo
   box's edit fields inset by `space.sm`, so text, a placeholder and a combo's label start where palantir's do.
2. **A spin box's arrows as one column of two buttons**: 8 × 4 chevrons, each half lit under the pointer as now,
   a hairline between them, and the column 20 px wide. Refused: `−` and `+` beside each other (Fluent's), which
   takes a field's width twice over in the Settings page's narrow columns.
3. **A combo box's list is a menu**: the popup's container painted as `PE_PanelMenu`, `#383838` behind the
   middle edge with 4 px above and below, its rows the menu's rows, as palantir's combo box drops its menu.
4. **A list's rows take the row padding**: `CT_ItemViewItem` 4 px above and below, palantir's chip inset, so a
   row is the text and 8 px.
5. **A selected row is a fill and a mark, not a tint**: `#454545` (the hover rung) with the accent's 3 px pill at
   its left edge, as Fluent 2 marks a selected list or navigation item; under the pointer `#525252`. The tint
   stays for selected text inside a field, where palantir uses it. Refused: the olive wash for rows, the one
   place the accent fills a surface; the accent's full fill, under which the row's text must turn dark.
6. **A menu pads 4 px**, `PM_MenuVMargin`, palantir's menu padding, its separator the soft edge inset as its
   rows are.
7. **Mnemonics underline only while Alt is held** (`SH_UnderlineShortcut` 0), as Windows and macOS draw them.
8. **Layouts on palantir's grid**: 12 px at a page's or a group's edge (`space.md`), 8 between widgets
   (`space.sm`).
9. **A rule is the soft edge**, `QFrame::HLine` and `VLine` drawn as `borderSoft`, as palantir's separator.
10. **The remaining icons from Lucide, in the look's ink**:
    - the wizard's four (`files/wizard/icons`) as `Misc::ScalableIcon` templates, which the wizard already
      loads them through, so no code moves: `disc` (installer), `folder`, `languages`, `shopping-cart`;
    - the "contains data" mark as `package-check` through `ScalableIcon`;
    - a message box's icons drawn by the style (`standardIcon` for `SP_MessageBox*`): `circle-help` and
      `info` in the text's ink, `triangle-alert` in the warning's, `circle-x` in the error ink.
11. **The checked toolbar page carries the accent**: its icon and label in the text, its chip `#454545` as now,
    and the accent's 3 px pill under the chip, as a tab's cap marks the tab in use. Refused: the icon itself in
    the accent, a lime glyph beside white ones.

## Plan

Each batch: the recipe with exact-pixel tests on the offscreen platform, as the first pass's, and the gallery of
every page rendered before and after.

2. List rows' padding and the selected row's fill and mark, the toolbar page's pill (4, 5, 11).
3. Mnemonics, layout spacing, rules (7, 8, 9).
4. Icons: the wizard's, the contains-data mark, a message box's (10). Upstream change for the mark, recorded.
5. A gallery of every page, kept: `apps/components_tests/graphite/` renders each `.ui` file of the launcher and
   the wizard and fails on a page that does not load, so a page added upstream is seen in the look.

## Changed

- **The accent is Morrowind's leather, not palantir's lime**, by the user's request during batch 1: the main
  menu's art, `#764316`, lifted at its own hue to `#bc8458`, which every floor still holds.

