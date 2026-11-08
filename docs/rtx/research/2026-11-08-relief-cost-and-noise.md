# What marching a relief costs and how it moves the noise — 2026-11-08

A high-end NVIDIA laptop GPU, release builds, the PBR replacer profile with Tamrielic PBR Dwemer Ruins and Landscape
Remastered 2.0 added. `--relief` is the A/B: on marches an `_nh` map's relief, off shifts it once as the rasterizer
does.

## Cost

`bench --seconds=10` at four places, 1920x1080 from 1280x720, a warm-up leg and then on, off, off, on, back to back.
The march alone, before the relief shadows. Median and p99 in ms.

| place | frame, on | frame, off | trace, on | trace, off |
|---|---|---|---|---|
| `seyda-neen-wall-noon` | 6.31–6.33 / 7.31–8.06 | 6.30–6.33 / 6.92–7.09 | 0.383–0.395 / 0.80–0.93 | 0.326–0.332 / 0.65 |
| `vivec` | 7.02 / 8.01–8.10 | 6.96–7.00 / 7.81–8.08 | 0.524–0.529 / 0.57 | 0.517–0.522 / 0.56–0.99 |
| `balmora` | 5.80–5.84 / 6.34–6.64 | 5.75–5.80 / 6.60–6.62 | 0.497–0.500 / 0.83–0.84 | 0.490–0.492 / 0.83–0.91 |
| `addamasartus` | 5.78–5.86 / 6.49–6.56 | 5.81–5.83 / 6.41–6.61 | 0.440–0.448 / 0.79 | 0.415–0.416 / 0.75–0.76 |

No other zone moved past its legs' spread.

## Noise

`noise --ab=relief`, each leg's figures on against off.

| place, leg | noise mean | p99 | bias | fireflies in 1000 |
|---|---|---|---|---|
| march and shadows, motion to the triangle: `seyda-neen-wall-noon`, strafe | 19.37 / 18.21 | 92.53 / 88.44 | 16.38 / 13.31 | 24.05 / 11.24 |
| the same, walk | 8.58 / 8.09 | 56.37 / 53.34 | 19.73 / 17.91 | 14.39 / 8.16 |
| the march alone: strafe | 18.21 / 18.22 | 89.09 / 88.53 | 15.23 / 13.32 | 22.31 / 11.33 |
| the march alone: walk | 8.10 / 8.10 | 54.28 / 53.40 | 18.35 / 17.91 | 12.28 / 8.17 |
| march and shadows, motion to the texel the march shows: strafe | 18.37 / 18.22 | 90.72 / 88.53 | 14.27 / 13.32 | 13.88 / 11.33 |
| the same, walk | 7.89 / 8.10 | 53.75 / 53.40 | 17.65 / 17.91 | 7.86 / 8.17 |
| the same, `balmora-mages-guild`, strafe | 10.71 / 10.74 | 65.66 / 65.56 | 19.40 / 19.41 | 10.10 / 9.97 |
| the same, `vivec`, strafe | 4.37 / 4.42 | 28.25 / 28.53 | 7.40 / 7.42 | 2.89 / 2.91 |

The fireflies the march added came from a history fetched by the triangle's motion, which the texel the march shows
does not follow; measured to that texel, most of them went.

## With the shadows, the land and the landing's shift

The same bench at four places once the relief shadows, the stack's march and the visible point's motion were in, the
march's arrays held in registers (indexed by a count the hit decided, they spilled 32 bytes a lane in the direct and
landing stages). Median ms, on against off.

| place | frame | trace | direct | landing |
|---|---|---|---|---|
| `seyda-neen-wall-noon`, a bounce's landing marched | 6.64–6.66 / 6.37–6.41 | 0.40–0.42 / 0.34–0.35 | 0.73 / 0.64 | 0.96–0.97 / 0.83–0.84 |
| `seyda-neen-wall-noon`, a landing shifted | 6.53–6.61 / 6.35–6.36 | 0.40–0.41 / 0.34 | 0.72–0.73 / 0.63–0.64 | 0.91 / 0.83 |
| `dagon-fel`, a landing shifted | 5.92–5.99 / 5.69–5.78 | 0.61–0.62 / 0.56 | 0.46 / 0.43–0.45 | 0.40 / 0.38 |
| `one-cell-walk`, a landing shifted | 4.96–5.11 / 4.97–5.00 | 0.91 / 0.89–0.90 | 0.32–0.33 / 0.33 | 0.27 / 0.26 |
| `vivec`, a landing shifted | 7.08–7.12 / 7.09–7.11 | 0.54 / 0.53 | 0.44 / 0.43–0.44 | 0.73–0.74 / 0.73 |

`noise --ab=relief` over ground, once the stack's march was in: at `one-cell-walk`, `dagon-fel` and `island-crossing`
the noise mean, the p99, the bias and the fireflies stood within 0.06 of the march's absence on both legs.

Vanilla's `shot --views=all --map` against the base: the trace and the scene the same on every frame of every place;
only the composed frame differed, by the card's arithmetic under the wavelet.

## The march's step cap

`RELIEF_STEPS` at 16, 32 and 64, legs in the order 32, 16, 64, 64, 16, 32, `bench --seconds=10`. Median ms.

| place | frame, 16 | frame, 32 | frame, 64 | trace, 16 / 32 / 64 | direct, 16 / 32 / 64 |
|---|---|---|---|---|---|
| `seyda-neen-wall-noon` | 6.44–6.47 | 6.52–6.53 | 6.68–6.69 | 0.38 / 0.40 / 0.43–0.45 | 0.67–0.68 / 0.71–0.72 / 0.80–0.82 |
| `dagon-fel` | 5.95 | 5.89–6.00 | 5.98–6.01 | 0.62–0.64 / 0.64–0.65 / 0.63–0.66 | 0.46 / 0.46 / 0.47 |
| `one-cell-walk` | 5.02–5.12 | 5.04–5.09 | 5.07 | 0.92–1.00 / 0.99–1.00 / 0.92–0.99 | 0.33 / 0.33 / 0.32–0.33 |

The cap only reaches a march whose crossing passes it, which the close wall's grazing texels do and the ground at a
walk's distance does not.

## On a low-end AMD integrated GPU

A handheld's integrated GPU under RADV, its clock held at 1600 MHz, release build, the same profile, 1920x1080 from
1280x720, the shadows, the stack's march and the landing's shift in. `bench --seconds=10`, a warm-up leg and then on,
off, off, on. Median ms, on against off.

| place | frame | trace | direct | landing |
|---|---|---|---|---|
| `seyda-neen-wall-noon` | 186.1–187.1 / 177.9–178.1 | 17.37–17.40 / 14.63–14.65 | 33.63 / 27.78–27.79 | 41.84–42.25 / 41.16–41.25 |
| `dagon-fel` | 104.7–105.6 / 102.1 | 12.46–12.60 / 11.03–11.04 | 8.85–8.99 / 7.90–7.91 | 5.61–5.80 / 5.53–5.55 |
| `vivec` | 200.2–200.4 / 195.0–195.2 | 27.59–27.62 / 24.53–24.57 | 13.83–13.84 / 12.12–12.14 | 40.69–40.70 / 40.34–40.56 |

The march costs this card a larger share than the NVIDIA one: 5% of the close wall's frame, 3% at `dagon-fel` and
`vivec`, most of it in the trace and the direct light, which march and shadow every relief texel the eye meets.

The trace's kernels on a Windows AMD driver (`noKernelSpillsPastItsCeiling`): the relief tuples spill what the tuples
without it spill, and the sea's walk holds 224 registers without a dark map's alpha against 219, inside the same four
waves.

`noise --relief=true --versus=relief=false --strafe=150` at the close wall on the handheld gave the march 30.3
fireflies in a thousand against 9.1 and a frame bias of 21.2 against 12.4. The same command under that card's budget on
the NVIDIA card (`--memory-budget=6004`) gave the same, and with the legs swapped gave the shift 23.1 and 20.3 against the
march's 11.0 and 13.8: the first leg's reference shows the strafe at another place (`.notes/ISSUES.md`). Each switch
measured as the second leg, the march stands at 11.0 fireflies and a bias of 13.8 against the shift's 9.2 and 12.8.
