# A rate on the eye's bounce — 2026-10-03

A high-end NVIDIA laptop GPU, release builds, three alternated rounds. Measured with `ee8f3f80c6`
("Denoise lamp shadows and trace every bounce"), when `BOUNCE_RATE` was set to one; the figures
stood in its comment in `look.h` until the constant was removed.

A share of the eye's hits traced their bounce, and the rest took no indirect light; a traced
bounce was divided by the share.

## Time, the `trace` zone

| place | every bounce | half |
|---|---|---|
| `seyda-neen-ship` | 1.97–2.05 ms | 1.77–1.82 ms |
| `balmora-mages-guild` | 1.45–1.47 ms | 1.20–1.23 ms |
| `seyda-neen-ship`, 4K performance | 4.33–4.36 ms | 3.83–3.84 ms |
| `balmora-mages-guild`, 4K performance | 3.38–3.41 ms | 2.76 ms |

Four to six per cent of the frame.

## Noise

Frame noise against sixteen frames averaged, standing and strafing in, and bias against a
converged reference standing, in levels of 255.

| place | half: still | strafe | bias | every: still | strafe | bias |
|---|---|---|---|---|---|---|
| guild's planter at night | 1.21 | 2.26 | 2.50 | 0.96 | 2.18 | 2.30 |
| mages' guild | 0.94 | 1.68 | 1.80 | 0.79 | 1.56 | 1.67 |
| Seyda Neen's pier | 0.59 | 1.27 | 1.86 | 0.51 | 1.19 | 1.70 |
| Seyda Neen's pond | 0.54 | 1.21 | 1.61 | 0.47 | 1.19 | 1.53 |
