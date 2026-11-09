# A sphere-mapped sheet's span taken off the corners the hit carried

Measured 2026-11-09. `sheetAt` fetched and decoded the triangle's three vertex normals a second time, inside
the surface's resolve, to measure how far their reflections spread. `resolve` now takes that span
(`sheetSpanOf`) off the corner normals it already rebuilds from `Hit::mShading` and its turns, and hands it
to the sheet.

## The cause

On a low-end AMD integrated GPU under AMD's Windows driver, with the vertex normal packed into a word
(`4ec617cb00`), the bounce kernel of the full tuple without the dark alpha's read held 144 registers against
its ceiling of 128. Each kernel below was compiled with one part of the decode left out:

| what was left out | bounce, no dark alpha | landing | visibility |
|---|---|---|---|
| nothing | 144 | 160 | 128 |
| every normal decode, a hit's and the sheet's | 128 | 144 | 128 |
| the sheet's decode alone | 128 | 144 | 128 |
| the hit's decode done one corner at a time | 144 | 160 | 128 |
| the stage record read again after the bounce ray | 144 | — | — |

The sheet's decode alone holds the bounce and the landing a step past where they would stand.

## The shapes tried

| shape | bounce, no dark alpha | landing | visibility |
|---|---|---|---|
| the corners handed to the sheet | 144 | 144 | 145 |
| the tips rebuilt at the sheet from the hit | 128 | 144 | 145 |
| the span taken where the corners stand | 128 | 144 | 145 |
| the same with no branch on the sheet | 128 | 144 | 145 |
| the sheet's decode one corner at a time | 144 | 160 | 128 |

Every shape that carries anything past the corners lifts the visibility kernel from 128 to 145, its old
ceiling.

## Frame times, AMD

`bench --views=seyda-neen-ship,balmora-mages-guild` on vanilla, the debug harness, two rounds of each,
before and after in turn. GPU zone medians in ms; the shaders are the same in either flavour.

| place | zone | before | after |
|---|---|---|---|
| ship | frame | 111.47, 111.33 | 110.69, 110.55 |
| ship | landing | 17.16, 17.15 | 16.23, 16.20 |
| ship | bounce | 19.15, 19.13 | 19.54, 19.50 |
| ship | trace | 11.63, 11.59 | 11.65, 11.64 |
| ship | direct | 13.38, 13.35 | 13.16, 13.17 |
| guild | frame | 82.17, 81.89 | 81.21, 81.23 |
| guild | landing | 15.59, 15.58 | 14.78, 14.81 |
| guild | bounce | 12.86, 12.87 | 12.82, 12.82 |
| guild | trace | 6.39, 6.36 | 6.44, 6.44 |

## Registers and shared memory on the other drivers

- **RADV**, the handheld's integrated GPU, read off the compiled code's highest vector register: the full
  tuple's `visibility` 160 → 144, `landing` 144 → 128, and the bounce without the dark alpha's read 128 →
  112.
- **NVIDIA**, a high-end laptop GPU: registers unchanged; `water`'s shared memory 3456 and 4224 bytes → 2944,
  and `panes` without the dark alpha's read 6272 → 5760.

## Pictures

`shot --views=all --upscale=off` against the shaders before, on a high-end NVIDIA laptop GPU: no picture
moved, on vanilla or on the PBR replacers; one vanilla and three PBR pictures differ by 1 of 255 on under
0.01% of their pixels, within the denoiser's noise on that card.
