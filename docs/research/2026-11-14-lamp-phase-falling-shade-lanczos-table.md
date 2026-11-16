# Lamps under the fog's phase, falling sprites out of the self-shadow, a Lanczos table — 2026-11-14

A high-end NVIDIA laptop GPU, release, three 20-second legs a side after a warm-up; pictures by `shot --upscale=off
--against`, noise by `noise`.

- **Lamps scatter by the fog's phase** (`lampsInAir`): `air` 0.245 → 0.266 ms at `seyda-neen-ship` under snow, unmoved
  at `balmora-fog-night`. The pictures moved where lamps light the fog: halos toward the eye round the lamps down
  `balmora-fog-night`'s street (25% of pixels, worst 112 of 255). Noise as clean as its bar.
- **Falling sprites out of the self-shadow** (`spriteShadeWants`): `shade` under snow 0.95 → 0.03 ms, the pictures moved
  by at most 2 of 255 on under 0.5% of pixels under snow and rain.
- **The reprojection's Lanczos weight from a table** (`SampleLanczos2Weight`): `upscale` at `balmora-fog-night` 0.43 →
  0.40 ms at the median, its p99 1.03–1.33 → 0.69 ms.
