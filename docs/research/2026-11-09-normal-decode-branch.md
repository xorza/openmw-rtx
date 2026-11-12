# The normal word's decode, branched or selected

Measured 2026-11-09. `unpackTangent` decodes a vertex's tangent and, since the normal joined the tangent's word, its
normal (`unpackNormal`): three corners a hit, in every stage that resolves one. The word nought is "no direction",
which a select or a branch answers with nought.

## Registers, on a low-end AMD integrated GPU under AMD's Windows driver

Through `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`, which logs each pipeline's `numUsedVgprs` for a
32-lane wave.

| decode | `direct` | `landing` | `bounce` without the dark alpha's read | `bounce`, the whole tuple |
|---|---|---|---|---|
| selected: the word decoded whole, the presence bit choosing the answer | 160 | 160 | 144 | 144 |
| branched: an absent word answers nought before the decode | 144 | 144 | 128 | 144 |
| the normal selected and the tangent branched | 160 | 160 | 144 | 144 |
| selected, each triangle's three corners decoded in a loop kept rolled | 160 | 160 | 144 | 144 |

The tuples with the maps (`HAS_MAPS`) are the ones that moved; the tuples without them held 110 to 112 in all four.
The normal's select is what holds the registers, and the three decodes side by side are not.

## Frame times, on a high-end NVIDIA laptop GPU

`bench --views=seyda-neen-ship,balmora-mages-guild`, release, two rounds each, alternated, medians in ms.

| decode | ship frame | ship `landing` | guild frame | guild `landing` |
|---|---|---|---|---|
| selected | 5.762, 5.762, 5.741, 5.770 | 0.562, 0.554, 0.555, 0.561 | 4.990, 4.995, 4.988, 4.991 | 0.519, 0.522, 0.520, 0.521 |
| branched | 5.731, 5.752 | 0.555, 0.550 | 5.175, 5.163 | 0.711, 0.687 |
| the normal selected and the tangent branched | 5.731, 5.754 | 0.556, 0.564 | 4.967, 4.975 | 0.518, 0.521 |

The branched normal costs the guild's landing a third more, 3.5% of its frame.
