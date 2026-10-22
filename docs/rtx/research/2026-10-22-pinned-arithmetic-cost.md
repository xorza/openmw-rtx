# What the pinned arithmetic costs — 2026-10-22

RTX 4090 Laptop, driver 615.78.08, Linux, release build at `1f3b20786a`, 1920x1080 output, upscale
`quality`. A against B, in the order A B B A, each leg the eight places
`seyda-neen-shore,seyda-neen-pond,balmora,vivec,seyda-neen-pier,ald-ruhn,seyda-neen-ship-dawn,balmora-mages-guild`
after a throwaway leg: A the modules as the build makes them, pinned (`spirvpin.hpp`); B the same
modules with `openmw-rtx-spirv-pin` made to write nothing, so the driver may fuse, reassociate and
order the operations the pin fixes. Records in `.notes/perf/pin-ab/`.

The frame, summed over the eight places, in ms:

| leg | medians | p99s | worst |
|---|---|---|---|
| A, pinned | 48.79 | 56.96 | 70.29 |
| B, unpinned | 47.02 | 53.00 | 62.41 |
| B, unpinned | 47.38 | 54.14 | 63.68 |
| A, pinned | 49.45 | 55.67 | 67.35 |

The zones, the better of each side's two legs, summed, in ms:

| zone | pinned | unpinned | unpinned against pinned |
|---|---|---|---|
| landing | 5.015 | 4.624 | −7.8% |
| direct | 4.014 | 3.821 | −4.8% |
| trace | 4.313 | 4.246 | −1.6% |
| bounce | 2.906 | 2.942 | +1.2% |
| panes | 0.632 | 0.591 | −6.5% |
| denoiser's temporal | 1.618 | 1.520 | −6.1% |
| clamp | 2.424 | 2.370 | −2.2% |
| shadow filter | 1.328 | 1.280 | −3.6% |
| wavelet's first level | 2.215 | 1.988 | −10.2% |

**The convention costs about 4% of the frame**: 1.85 ms over the eight places' medians, 0.23 ms a
place, and about 3 ms of their p99s. Most of it is the wavelet's first level, the landing and the
direct light, the kernels with the most dot products and products to fuse. A record of the price
of frames that agree to the bit from process to process, not a proposal to pay less.
