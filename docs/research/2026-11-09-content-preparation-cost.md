# What the loading screen's preparation costs — 2026-11-09

A high-end laptop CPU (8 performance cores, 32 logical CPUs) with the release build: `scene --cell="Seyda Neen"`
under SDL's offscreen driver, each content list into a fresh `--content-cache`, so every model and ground layer is
walked. The time is the log's, from the content load's end to `Prepared … models and ground layers`; the peak is the
process's resident peak. The encoders have since been made faster
(`2026-11-13-preparation-cpu-by-codec.md`), which holds the current cold times.

## Cold and warm

| content | jobs | cold | CPU time | peak resident | store written |
|---|---|---|---|---|---|
| vanilla | 6,755 | 5.1 s | 83 s | 1,458 MiB | 221 MiB |
| Fullrest Repack | 44,762 | 364 s | 5,766 s | 4,717 MiB | 8.0 GiB |
| PBR replacers | 6,755 | 84 s | 1,263 s | 5,369 MiB | 6.0 GiB |

The CPU is the bound, not the disk: the Fullrest Repack read 6.8 GB and wrote 8.7 GB in 364 s, every thread busy
throughout. Almost all of it is the texture chains: BC1 encoding over half of the Repack's, BC7 a third of the
replacers', building and delighting the rest. Warm, with each model's store keys checked, the Repack's preparation
takes 0.17–0.18 s.

## BC1 at each rgbcx level

400 BC1 files of the Fullrest Repack, 264.5 million texels, each finest level scaled by a smooth light field of 0.7 to
1.3 as a painted colour's division moves it off the file's palette, encoded on one core:

| level | 3-colour blocks | Mtexel/s | PSNR |
|---|---|---|---|
| 12 | on | 3.95 | 44.73 dB |
| 10 (the build's) | on | 7.36 | 44.65 dB |
| 10 | off | 11.73 | 44.47 dB |
| 8 | on | 15.76 | 44.51 dB |
| 5 | on | 45.31 | 44.03 dB |
| 3 | — | 87.96 | 43.27 dB |

The file's own block with its endpoints scaled by the light at its centre: 233 Mtexel/s, 42.88 dB.

## BC7 for normal maps

120 of the PBR replacers' normal maps, 128.3 million texels, laid as the unit normal in three channels and the height in
alpha, on one core:

| search | Mtexel/s | mean angle | height RMSE |
|---|---|---|---|
| mode 5 with a height, 6 without, least squares on (the build's) | 20.55 | 0.442° | 0.490 |
| least squares off | 26.87 | 0.511° | 0.490 |
| uber level 1 | 12.35 | 0.418° | 0.485 |
| mode 6 for every block | 18.62 | 0.362° | 1.712 |

## Threads

The PBR replacers' cold preparation by thread count, the harness's hold on the performance cores lifted:

| threads | CPUs | preparation | peak resident |
|---|---|---|---|
| 8 | the 16 of the performance cores | 107.5 s | 5,694 MiB |
| 16 | the 16 of the performance cores | 83.7 s | 5,397 MiB |
| 16 | all 32 | 79.7 s | 5,633 MiB |
| 31 | all 32 | 64.0 s | 6,114 MiB |

So the preparation runs on every usable CPU but one, the ground layers served before the models, so no thread waits
alone at the end.

## Memory

- **A normal map's means in a ring of eight rows**, in place of 12 bytes a texel under the finest level, took the
  replacers' peak 600–700 MiB lower; the peak moves by 900 MiB between runs of one build, with which threads meet the
  largest maps at once.
- **The dispatcher**: walks queue chain jobs, a dispatcher hands them out within a budget of the available memory, and
  the loader lets go of each file as its last job ends. Under a cgroup `MemoryMax` of 4 GiB the replacers' cold run
  held its count to the 2,560 MiB budget and took 78.5 s against 77.4 unbounded; every model walked against a full
  store peaked at 1,968 MiB where it had peaked at 7,269.
- **The reserve**: with the kernels compiled before, the screen's own growth over the preparation was 51 MB on the
  Fullrest Repack.

## Byte-exact arithmetic

`quantise` as a rounded double and the encoded byte read off 255 thresholds found by bisection agree with `std::lround`
and `powf` on every float from −3 to 3 (2,155,872,258 values), at 0.90 ns a value against 8.08. Landed with the
delight's light placed once a row and a column: vanilla 5.66 → 4.50 s, the Repack 364 → 319 s, every store the same.
