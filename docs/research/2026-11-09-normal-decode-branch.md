# The normal word's decode, branched or selected — 2026-11-09

`unpackTangent` decodes a vertex's tangent and its normal from one word, three corners a hit, in every stage that
resolves one. The word nought is "no direction", which a select or a branch answers with nought.

- **On a low-end AMD integrated GPU** (AMD's Windows driver), the tuples with maps: a branch on the absent word held
  `direct` and `landing` at 144 registers where the select held 160, and `bounce` at 128 for 144 without the dark alpha.
  The normal's select is what costs the registers; the tangent's does not, and decoding the three corners in a rolled
  loop changed nothing.
- **On a high-end NVIDIA laptop GPU** the branched normal cost the guild's `landing` a third more (0.52 → 0.70 ms),
  3.5% of its frame; the normal selected and the tangent branched ran as fast as the select everywhere.

So the normal is selected: the registers it costs one AMD driver are a smaller loss than the branch's on NVIDIA.
