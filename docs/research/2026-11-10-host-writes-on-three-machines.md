# Staged against mapped host writes on three machines

Measured 2026-11-10 on release builds of `7b26b022b8` that differ in `sHostWrites` alone. On the
Windows machine the mapped build also has the refusal of a host window smaller than video memory
switched off (`PhysicalDevice`, "What the build's transport needs of the memory"), which is how the
mapped build ran there before staged writes landed. `bench --window=false`, the default suite and the
`streaming` suite, 1200 frames a place after 120 of warm-up, after a throwaway warm-up leg, with mapped
and staged alternated twice, back to back. Milliseconds a frame: median / p99 / worst.

- **A high-end NVIDIA laptop GPU** under Linux, driver 615.78.08, resizable BAR on, a 16376 MiB
  device-local heap. 1920x1080, traced at 1280x720.
- **An AMD handheld** (RDNA 2, 8 CUs) under SteamOS, RADV Mesa 26.2.0-devel (git-5c65d4fc31), a 6144 MiB
  device-local heap the host maps whole, the GPU clock held at 1600 MHz through `steamos-manager`.
  1280x800, traced at 853x533.
- **An AMD laptop's integrated RDNA 2 graphics** (12 CUs) under Windows 11, AMD's driver 26.8.1, a
  256 MiB host window over a 12299 MiB device-local heap, run in a remote desktop session. 1280x800,
  traced at 853x533.

## NVIDIA laptop GPU, resizable BAR

| place | mapped, run 1 | staged, run 1 | mapped, run 2 | staged, run 2 |
|---|---|---|---|---|
| seyda-neen-ship | 5.83 / 6.47 / 6.86 | 5.90 / 6.62 / 7.38 | 5.90 / 6.51 / 7.25 | 5.94 / 6.84 / 7.43 |
| seyda-neen-ship-dawn | 6.30 / 6.93 / 7.88 | 6.39 / 7.15 / 7.71 | 6.36 / 6.98 / 7.46 | 6.40 / 7.14 / 7.66 |
| balmora-mages-guild | 4.97 / 5.65 / 7.92 | 5.02 / 5.72 / 6.36 | 5.01 / 5.67 / 7.27 | 5.05 / 5.57 / 6.31 |
| island-crossing | 4.46 / 14.32 / 18.94 | 4.53 / 14.08 / 21.71 | 4.46 / 14.17 / 20.07 | 4.56 / 13.84 / 18.91 |

## AMD handheld

| place | mapped, run 1 | staged, run 1 | mapped, run 2 | staged, run 2 |
|---|---|---|---|---|
| seyda-neen-ship | 83.08 / 92.13 / 96.03 | 83.18 / 92.37 / 96.11 | 83.08 / 91.73 / 95.95 | 83.15 / 92.03 / 95.53 |
| seyda-neen-ship-dawn | 98.06 / 110.08 / 115.39 | 98.26 / 110.91 / 116.04 | 98.06 / 110.48 / 115.32 | 98.13 / 110.40 / 115.88 |
| balmora-mages-guild | 53.03 / 54.61 / 61.56 | 53.04 / 59.10 / 61.77 | 53.04 / 54.35 / 61.51 | 53.06 / 59.61 / 61.39 |
| island-crossing | 42.85 / 74.41 / 104.97 | 42.56 / 76.26 / 100.92 | 42.52 / 74.62 / 105.18 | 42.59 / 75.68 / 100.15 |

## AMD integrated graphics, Windows, 256 MiB window

| place | mapped, run 1 | staged, run 1 | mapped, run 2 | staged, run 2 |
|---|---|---|---|---|
| seyda-neen-ship | 60.39 / 81.42 / 84.53 | 60.25 / 81.25 / 85.83 | 60.41 / 81.05 / 84.78 | 60.26 / 80.34 / 85.36 |
| seyda-neen-ship-dawn | 72.75 / 92.02 / 96.29 | 72.67 / 92.39 / 100.90 | 72.61 / 92.50 / 104.99 | 72.59 / 91.73 / 97.48 |
| balmora-mages-guild | 43.28 / 52.33 / 56.44 | 43.23 / 52.62 / 55.20 | 43.34 / 52.49 / 56.58 | 43.30 / 53.28 / 55.93 |
| island-crossing | 42.93 / 98.54 / 119.46 | 42.27 / 105.19 / 124.46 | 44.93 / 101.27 / 131.89 | 41.88 / 97.91 / 140.94 |

## The host's side

Medians of the rows where the host writes, at the three standing places, mapped against staged:

| machine | `place` | `upload` | `finish` (the wait for the device) |
|---|---|---|---|
| NVIDIA laptop GPU | 0.34, 0.34, 0.22 / 0.32, 0.32, 0.21 | 0.29, 0.29, 0.21 / 0.27, 0.27, 0.20 | 3.97–4.04, 4.47–4.52, 4.01–4.05 / 4.04–4.10, 4.56, 4.06–4.08 |
| AMD handheld | 0.89–0.91, 0.90–0.92, 0.45 / 0.93, 0.92–0.93, 0.47–0.48 | 0.80–0.82, 0.81–0.82, 0.44 / 0.83, 0.83–0.84, 0.46–0.47 | 72.3–72.4, 87.6–87.8, 48.4 / 72.4, 87.7–87.9, 48.4 |
| AMD integrated, Windows | 5.13–5.14, 5.12–5.13, 2.75–2.76 / 5.17–5.18, 5.14–5.19, 2.79–2.80 | 4.34–4.35, 4.33–4.34, 2.70–2.71 / 4.39–4.40, 4.36–4.40, 2.74–2.75 | 42.9–43.3, 55.8, 33.0–33.2 / 42.8–42.9, 55.8–56.0, 33.1–33.2 |

## What they say

- **On the NVIDIA card with resizable BAR, mapped writes are 0.04 to 0.10 ms a frame faster at the
  median**, every place and both rounds, 1 to 2% of a frame. Staged writes cost the host less, since the
  staging is cached and the BAR window is write-combined, and the device more: the frame waits 0.04 to
  0.2 ms longer on the copy. As the earlier note found (`2026-11-10-staged-host-writes-cost.md`).
- **On the AMD handheld the transports are one figure**: medians within 0.2 ms of 53 to 98, under 0.2%,
  and the crossing's order flips between the rounds. Staged writes cost the host 0.01 to 0.03 ms more,
  a copy more into the same memory. One difference held in both rounds: the guild's p99, 54.61 and 54.35
  mapped against 59.10 and 59.61 staged, its median and its worst unmoved.
- **On the AMD integrated graphics under Windows the transports are one figure too**: staged medians
  0.02 to 0.16 ms under mapped at the standing places, inside what two runs of one transport part by, and
  the crossing moves by 2 ms between two runs of one transport.
- **The 256 MiB window serves mapped writes on AMD's Windows driver**: the build that refuses such a
  window refuses a card whose driver gives it a working budget, as NVIDIA's Windows driver did not
  (`2026-11-09-host-written-window-on-windows.md`).
- **Staged writes ship as they stand**: the one card where mapped writes win is one with resizable BAR,
  by about 0.1 ms, and neither AMD part prefers either.
