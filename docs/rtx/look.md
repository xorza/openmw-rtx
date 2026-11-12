# The launcher's and the wizard's look

How the launcher and the wizard look, as decisions. The colours are in `components/graphite/palette.hpp`,
and the tests in `apps/components_tests/graphite/` hold each to what it must clear.

- **The Qt programs wear palantir's dark palette**, `Palette::DARK` at palantir's `67955d19f`: Ayu Graphite's
  semantic layer with palantir's moves, a ground a rung under Ayu Graphite's `bg`, a muted ink lifted to read
  4.5:1 on a field. Ayu Graphite's `error`, `warning` and `success` stand beside it, as
  palantir's palette names none. What derives from the table is derived in code, palantir's way: the border
  ladder, the selection's tint and the default button's states. The tint is solved in encoded sRGB, where
  `QPainter` blends it, rather than in linear light, where palantir's renderer does. Refused: a target of
  the ayu-graphite repository writing the table, whose semantic layer is not this look; a build reaching
  into palantir's sources.
- **The accent is Morrowind's leather**, by the user's decision, in place of palantir's lime: the mean of the main
  menu's art (`menu_morrowind.dds`), `#764316`, lifted at its own hue until a focused edge reads 3:1 on a field,
  `#bc8458`, as the error ink is lifted. Its dark ink reads 5.77:1 on it.
- **Always dark, on every system**, by the user's decision: the launcher and the wizard choose the look
  (`Platform::Look::Graphite`), and the editor keeps upstream's, which on Windows follows the system's dark
  mode; its tables and scene colours were made for upstream's palettes. Fusion under the palette, and the
  colour scheme asked for as dark (`QStyleHints::setColorScheme`), which Windows answers with a dark title
  bar and a platform whose theme takes no request ignores.
- **The system's family at 16 px**, palantir's title step, by the user's decision: Qt's own default on Linux is 9
  points, 12 px, and palantir's 14 px body still read small. Logical pixels, which a display's scale multiplies.
- **A button, a field, a combo box and a spin box are one row's height**: the text with palantir's row padding,
  8 px above and below, inside the hairline, by the user's decision, where palantir pads a button 4 px; so a
  button beside a field lines up with it.
- **The error ink is lifted at its own hue until it reads**: Ayu Graphite's `#ff8b8c` reads 4.26:1 on a
  field's `#454545`, so it is raised in Oklab lightness, its hue kept and its chroma where sRGB holds it, to
  the least colour that clears 4.5:1, as palantir lifts its muted ink over Ayu Graphite's.
- **Fusion, the palette and a proxy style; no style sheet**, as Qt Creator draws its dark theme
  (`Graphite::Style`). The palette carries every colour, so code that sets a widget's palette keeps working; the
  style draws palantir's recipes for buttons, fields, check boxes, tabs, rows, scroll bars, progress bars,
  menus and frames, and Fusion the rest. Refused: a style sheet with variables, as OBS Studio 30.2 themes: a
  sheet that sets a property overrides what code sets in a widget's palette, a complex widget restyled by the
  box model takes all its subcontrols with it, and check marks and arrows become image files, a second copy of
  the colours; Fusion and the palette alone, whose gradients, bevels and 2 px corners are the look replaced.
- **A focus is an edge, never a dotted rectangle**: a field's edge turns the accent's whenever it holds the
  focus, a button's and a check box's only when the keyboard moved it there, as palantir rings a control on a
  keyboard focus alone (CSS's `:focus-visible`).
- **Scroll bars without arrows**: palantir's 8 px pill on the far side of a 4 px gap, laid out as the common
  style lays out its transient bars, without making them transient, which would overlay and hide them.
- **Menus and tooltips are square**: palantir rounds its floating panels, but a rounded top-level window is a
  translucent one, which a desk without a compositor draws black around its corners.
- **The launcher's glyphs are Lucide's line icons** (0.460.0, ISC, `files/licenses/Lucide.txt`), on every system
  rather than the system's theme, which draws its icons for its own scheme: the toolbar's pages as templates
  `Misc::ScalableIcon` recolours to the palette's text, and the fallback theme's five glyphs, which
  `QIcon::fromTheme` draws as they are, in a copy of `mText` a test holds to the table. Refused: the fallback
  glyphs left black, unreadable on the ground; Oxygen's PNGs beside line icons, two looks on one toolbar.
- **A dialog's buttons are their words**: no stock icons beside them, the platform's look and not this one.
- **An invalid name is the error ink**, where the launcher wrote it in `Qt::red`, 2.40:1 on a field.
- **A field's text starts 8 px in**, palantir's field inset, in a line edit, a spin box and a combo box alike; a
  combo box and a spin box grow by what the inset takes from Fusion's field, so the text Fusion sized them for
  still fits.
- **A spin box's arrows are one column of 20 px**, the up arrow above the down, each half lit under the pointer,
  soft hairlines beside the field and between the two.
- **A combo box's list is a menu**: the menus' palette is the floating panel's ground, which the list fills each
  row with, and the list leaves the menu's panel showing; a menu pads 4 px above and below, its separator the
  soft edge held off its sides.

