# The launcher's and the wizard's look

How the launcher and the wizard look, as decisions. The colours are in `components/graphite/palette.hpp`, and the
tests in `apps/components_tests/graphite/` hold each to what it must clear.

- **palantir's dark palette** (`Palette::DARK` at `67955d19f`): Ayu Graphite's semantic layer with palantir's moves,
  and Ayu Graphite's `error`, `warning` and `success` beside it. The border ladder, the selection's tint and the
  default button's states are derived in code, the tint in encoded sRGB, where `QPainter` blends it. Refused: a
  build reaching into palantir's sources.
- **The accent is Morrowind's leather**, by the user's decision: the main menu art's mean, `#764316`, lifted at its
  hue to `#bc8458`, where a focused edge reads 3:1 on a field.
- **Always dark, on every system**, by the user's decision (`Platform::Look::Graphite`); the editor keeps upstream's.
  The colour scheme is asked for as dark, which Windows answers with a dark title bar.
- **The system's family at 16 px** (12 points), by the user's decision, set in points as `QWizard` grows its title
  from the point size.
- **A button, a field, a combo box and a spin box are one row's height**, 8 px above and below the text, so a button
  beside a field lines up with it.
- **The error ink is lifted at its hue until it reads 4.5:1** on a field, in Oklab lightness; an invalid name is
  that ink, not `Qt::red`'s 2.40:1.
- **Fusion, the palette and a proxy style, no style sheet**, as Qt Creator draws its dark theme (`Graphite::Style`):
  code that sets a palette keeps working. Refused: a style sheet with variables, which overrides what code sets and
  turns check marks and arrows into image files.
- **A focus is an edge, never a dotted rectangle**: a field's whenever it holds the focus, a button's only when the
  keyboard moved it there (CSS's `:focus-visible`).
- **Scroll bars without arrows**, an 8 px pill on the far side of a 4 px gap, never transient.
- **Menus and tooltips are square**, as a rounded top-level window is translucent and draws black corners without a
  compositor.
- **Lucide's line icons** (0.460.0, ISC) on every system, recoloured to the palette's text through
  `Misc::ScalableIcon`: the toolbar, the fallback theme's glyphs, the wizard's icons, the contains-data mark and a
  message box's icons. A dialog's buttons carry no stock icon.
- **A field's text starts 8 px in**, and a spin box's arrows are one 20 px column, each half lit under the pointer.
- **A combo box's list is a menu**: the floating panel's ground, 4 px above and below, the soft edge as separator.
- **A selected row is a fill and the accent's 3 px pill at its start**, as Fluent 2 marks one; the launcher's page
  in use carries the pill at its foot. Refused: the tint, a muddy wash; the accent's full fill.
- **A mnemonic is underlined only while Alt is held**; layouts sit on a 12 px edge and 8 px between widgets.
- **Every page is read in the look by a test** (`GraphiteGalleryTest`): each `.ui` file loaded and its controls held
  to one row's height, so a page upstream adds is seen.
