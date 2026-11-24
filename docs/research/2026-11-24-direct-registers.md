# What the lifted shadow rays cost `direct` on AMD — 2026-11-24

`ed8cbe436c` lifts each shadow ray toward its own source (`liftedToward`), and carried the sky's and each held lamp's
lifted point through `direct`'s traversals. Measured at `7ccdebfbb4` (its parent), `34a5b6162d` (master) and the fix
over master.

- **Bisected** under Mesa 26.2.3's `amdgpu` drm-shim for Van Gogh, `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling`
  with Mesa's shader cache and the test's pipeline cache empty: `6b32fd21b9` and `7ccdebfbb4` hold `direct` under its
  ceiling of 96, `ed8cbe436c` and every commit after it at 112.
- **Demand** is RADV's own `Pre-Sched VGPRs` (`RADV_DEBUG=shaderstats`), and registers the count it allocates, under
  the same shim, `bench --frames=1 --views=seyda-neen-ship`.
- **The hardware**: the handheld (RDNA 2, Van Gogh, SteamOS's Mesa 26.2.0-devel) and the Windows machine's AMD
  integrated GPU under AMD's own driver, each staged by `omw release remote`; registers there are the highest VGPR its
  final code names, plus one.

## Registers, demand in brackets

| kernel | parent | master | fix |
|---|---|---|---|
| `direct`, shim | 96 (92) | 112 (98) | 96 (93) |
| `direct sea`, shim | 96 (94) | 112 (100) | 96 (93) |
| `direct maps`, shim | 128 (113) | 128 (118) | 128 (113) |
| `direct`, handheld | | 112 | 96 |
| `direct sea`, handheld | | 112 | 96 |
| `direct maps relief`, handheld | | 144 | 128 |
| `direct sea maps relief`, handheld | | 144 | 128 |
| `direct`, AMD's Windows driver | 112 | 112 | 112 |
| `direct sea`, AMD's Windows driver | 112 | 112 | 112 |
| `direct maps`, AMD's Windows driver | 112 | 126 | 128 |
| `direct maps relief`, AMD's Windows driver | 128 | 143 | 128 |

No other kernel's registers moved on the handheld. **AMD's own compiler held `direct` at 112 before the lift went
toward each source**, and holds `direct maps` a tier over its parent's still.

- **Each lifted point was three numbers through every traversal**: the parent carried one point for every ray. Lifting
  each ray at its turn from the shaded point and its lift brought `direct` to 95; the strata's lamp distances measured
  past the rays, 93; the sky's legs joined at the turn of the second, 93 with the sea, from 95.
- **The path end's lamp point stays made where the lamp is weighed**: made past the sky's terms, NVIDIA's `landing`
  took 1152 bytes of shared memory against its ceiling of 384.

## The handheld's frame

Its clock held at 1600 MHz, `bench --size=1280x800 --frames=300 --window=false`, a warm-up leg then one measured leg a
side, medians:

| place | `direct` | p99 | frame | p99 |
|---|---|---|---|---|
| ship, noon | 11.46 → 10.69 ms (−6.6%) | 12.08 → 11.26 | 83.06 → 82.34 ms (−0.9%) | 90.23 → 89.39 |
| ship, dawn | 21.40 → 19.84 ms (−7.3%) | 25.19 → 23.29 | 98.93 → 97.42 ms (−1.5%) | 108.31 → 106.51 |
| guild | 9.28 → 8.72 ms (−6.0%) | 9.50 → 8.93 | 54.99 → 54.33 ms (−1.2%) | 55.98 → 55.21 |

No other zone moved by more than 1%.

## AMD's Windows driver

`bench --size=1280x720 --window=false --frames=200`, a warm-up leg then one measured leg a side, medians; master's two
legs within 0.2% of each other in `direct` and 0.4% in the frame:

| place | `direct` | p99 | frame | p99 |
|---|---|---|---|---|
| ship, noon | 10.06 → 10.10 ms (+0.4%) | 10.57 → 10.76 | 63.83 → 63.69 ms (−0.2%) | 77.12 → 73.06 |
| ship, dawn | 16.68 → 16.88 ms (+1.2%) | 17.25 → 17.49 | 73.72 → 74.11 ms (+0.5%) | 86.28 → 89.46 |
| guild | 10.15 → 9.54 ms (−6.0%) | 10.51 → 9.92 | 50.04 → 49.36 ms (−1.4%) | 58.09 → 53.26 |

Where the driver's registers did not move, each ray's start made again at its turn is what the stage pays.

## Pictures

`shot --views=all --map --upscaler=off` against master: the trace and the scene the same on every frame of every view;
three composed frames differ by 1 of 255 under the wavelet, which this card's denoiser does.
