# The launcher's and the wizard's look

How the launcher and the wizard look, as decisions. The colours are in `components/graphite/palette.hpp`,
and the tests in `apps/components_tests/graphite/` hold each to what it must clear.

- **The Qt programs wear palantir's dark palette**, `Palette::DARK` at palantir's `67955d19f`: Ayu Graphite's
  semantic layer with palantir's moves, a ground a rung under Ayu Graphite's `bg`, a muted ink lifted to read
  4.5:1 on a field, a lime accent. Ayu Graphite's `error`, `warning` and `success` stand beside it, as
  palantir's palette names none. What derives from the table is derived in code, palantir's way: the border
  ladder, the selection's tint and the default button's states. The tint is solved in encoded sRGB, where
  `QPainter` blends it, rather than in linear light, where palantir's renderer does. Refused: a target of
  the ayu-graphite repository writing the table, whose semantic layer is not this look; a build reaching
  into palantir's sources.
- **Always dark, on every system**, by the user's decision: the launcher and the wizard choose the look
  (`Platform::Look::Graphite`), and the editor keeps upstream's, which on Windows follows the system's dark
  mode; its tables and scene colours were made for upstream's palettes. Fusion under the palette, and the
  colour scheme asked for as dark (`QStyleHints::setColorScheme`), which Windows answers with a dark title
  bar and a platform whose theme takes no request ignores.
- **The error ink is lifted at its own hue until it reads**: Ayu Graphite's `#ff8b8c` reads 4.26:1 on a
  field's `#454545`, so it is raised in Oklab lightness, its hue kept and its chroma where sRGB holds it, to
  the least colour that clears 4.5:1, as palantir lifts its muted ink over Ayu Graphite's.
