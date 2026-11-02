# The sea's walk as a stage of its own — 2026-11-02

Release builds at `421d2dba38` (base) and the split, `bench` over the default suite. The desk: a high-end NVIDIA
laptop GPU, driver 615.78.08, 1920x1080 (1280x720 traced), three rounds interleaved. The handheld:
RADV, Mesa 26.2.0-devel, 1280x800 (853x533 traced), the clock held at 1600 MHz, one run a
side. The integrated GPU: Adrenalin 32.0.21045.5002, Windows 11, 1920x1080, two rounds interleaved. Medians in
ms.

## What the walk held

The trace's kernels as the drivers compiled them, before:

| kernel | handheld VGPRs | handheld waves a SIMD | AMD iGPU VGPRs |
|---|---|---|---|
| `visibility` (no sea) | 128 | 8 | 112–128 |
| `visibility … sea` | 192, 20–37 SGPRs spilled | 5 | 234–247 |

The walk cut out for a measurement alone (water drawn wrong): the handheld's `trace` at
`seyda-neen-ship` 15.88 → 10.02, its `visibility … sea` at 128 VGPRs.

## The split

| host | place | trace before | trace + water after | frame before | frame after |
|---|---|---|---|---|---|
| handheld | seyda-neen-ship | 15.87 | 10.00 + 1.91 | 88.44 | 84.70 (−4.2 %) |
| handheld | seyda-neen-ship-dawn | 16.04 | 10.09 + 1.94 | 103.29 | 99.40 (−3.8 %) |
| handheld | balmora-mages-guild | 5.15 | 5.15 | 58.67 | 58.69 |
| AMD iGPU | seyda-neen-ship | 22.27 | 11.95 + 2.73 | 120.94 | 114.59 (−5.3 %) |
| AMD iGPU | seyda-neen-ship-dawn | 25.47 | 13.47 + 3.22 | 147.98 | 137.66 (−7.0 %) |
| AMD iGPU | balmora-mages-guild | 6.30 | 6.28 | 90.31 | 90.40 |
| desk | seyda-neen-ship | 0.68 | 0.55 + 0.11 | 6.04 | 6.04 (−0.06 %) |
| desk | seyda-neen-ship-dawn | 0.69 | 0.54 + 0.11 | 6.50 | 6.51 (+0.06 %) |
| desk | balmora-mages-guild | 0.37 | 0.37 | 5.34 | 5.33 (−0.2 %) |

- **The kernels after**: `visibility … sea` 128 VGPRs on the handheld (144 for the full tuple) and 145
  on the integrated GPU; `water` 192–256 on the handheld and 219 on the integrated GPU, run over the sea's tiles alone.
- **The peel traced again in `water`**, the first form: the handheld's `water` 2.95 against 1.91, and
  the desk's 0.135 against 0.111, its frame +0.66 % and +0.78 % at the ship and the dawn, past the
  desk's noise of 0.1 % over three rounds.
- **The pictures**: `shot --views=all --map --upscale=off` against base, every one the same but
  three within the denoiser's noise on the desk's card, two of them interiors, which run no water.
- The handheld's `bounce` and `landing` at the ship read 0.8–1.0 % more in both runs of the split, and
  0.2–0.3 % at the dawn; the integrated GPU's did not move.
