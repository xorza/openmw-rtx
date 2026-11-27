# Open issues

- **The crash matrix's `fast-fail` case writes no dump on Windows**: on the Windows machine's AMD integrated GPU, at
  `889fc79425` with an accumulator change on top that touches neither, `crash.matrix` fails `fast-fail` with "the
  summary names none of the exceptions this mode raises: Crash: the game crashed, and no dump of it could be written";
  every other case passes.
- **`ResidencyDeviceConformance.pricesAStructureByItsTriangles` fails on AMD's Windows driver**: on the Windows
  machine's AMD integrated GPU, at `889fc79425` with an accumulator change on top that touches neither, the price of a
  structure of 20,000 triangles leaves 140 bytes over its alignment (`many.mBytes % many.mAlignment`).
- **The Windows machine's tour refuses an Argonian normal map read as a colour**: at `889fc79425`, `textures-readable`
  fails at two places with "a texture "textures/tx_b_n_argonian_f_n.dds": its format is BC5 (ATI2, linear), which holds
  no colour, where a colour is read", and the same of `tx_b_n_argonian_m_n.dds`.
- **Two runs on the Windows machine's AMD integrated GPU no longer draw one picture**: `omw repeat --pairs=1` at
  `889fc79425` finds the trace differing on all 360 frames of `one-cell-walk`, from frame 1 and in every channel,
  `g-surface` and `g-albedo` among them; the gates of 16 and 17 November there found the pair identical.
