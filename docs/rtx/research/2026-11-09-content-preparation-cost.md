# What the loading screen's preparation costs

Measured 2026-11-09 on a high-end laptop CPU (8 performance cores, 16 threads, which the harness keeps to) with
the release build: `scene --cell="Seyda Neen"` under SDL's offscreen driver, each content list into a fresh
`--content-cache`, so every model and ground layer is walked. 16 preparation threads, as
`hardware_concurrency() / 2` gives on a machine of 32 logical CPUs. The time is the log's, from the content load's
end to `Prepared … models and ground layers`; the peak is the process's resident peak, `getrusage`.

## Cold and warm

| content | jobs | cold | CPU time | peak resident | store written |
|---|---|---|---|---|---|
| vanilla | 6,755 | 5.1 s | 83 s | 1,458 MiB | 221 MiB |
| Fullrest Repack | 44,762 | 364 s | 5,766 s | 4,717 MiB | 8.0 GiB |
| PBR replacers | 6,755 | 84 s | 1,263 s | 5,369 MiB | 6.0 GiB |

Warm, the manifest finding every job as it was, the Fullrest Repack's preparation ends 0.87 s after its last content
file starts to load, the rest of that load included; twice, 0.87 and 0.86 s.

Every preparation thread stands busy until the last seconds: on the Fullrest Repack, 15.9 of 16 threads in every
10 s window from the 10th to the 360th second; on the PBR replacers, 15.8 to 15.9 until the 75th second, then 2.3
in its last five. The CPU is the bound, not the disk: the Fullrest Repack read 6.8 GB and wrote 8.7 GB in 364 s.

## Where the CPU goes

`perf record -e task-clock -F 249 --call-graph fp` over the whole cold run; the preparation threads took 99.5%
(Fullrest Repack) and 99.1% (PBR) of the samples. Inclusive shares of the preparation threads' samples:

| function | Fullrest Repack | PBR replacers |
|---|---|---|
| `ChainPass::run`, every chain | 97.9% | 97.6% |
| `rgbcx::encode_bc1` | 61.6% | 30.0% |
| of it, `try_3color_block` | 19.9% | 10.8% |
| `bc7enc_compress_block` | 4.8% | 32.7% |
| `ChainBuild::build`, decode and box | 28.8% | 31.8% |
| of it, `ChainBuild::delight` | 14.3% | 4.6% |
| of it, `ChainBuild::buildNormals` | 0.3% | 18.5% |
| `lroundf`, out of libm (`quantise`) | 4.4% | 6.5% |
| `paintedLight` | 5.7% | 1.8% |
| `toEncoded` and the `powf` under it | 3.4% and about 5% | 1.1% |
| the template walk, shapes included | 1.3% | 0.9% |
| loading the models and images | 0.4% | 1.2% |

The keys, the store's writes and the manifest are under 0.3% each.

## What the stores hold

| content | painted chains (`ChainForm::Own`) | whole BC7 chains | coarser levels | shapes |
|---|---|---|---|---|
| vanilla | 4,026, 113 MiB | 72, 10 MiB | — | 19,904, 30 MiB |
| Fullrest Repack | 14,190, 6,814 MiB | 781, 732 MiB | 3, 6 MiB | 86,751, 263 MiB |
| PBR replacers | 998, 393 MiB | 3,236, 5,305 MiB | 3,118, 224 MiB | 19,907, 30 MiB |

The Fullrest Repack's largest chains are 4096-square painted BC1 colours, 10.7 MiB each, and the PBR replacers'
4096-square BC7 normal maps, 21.3 MiB.

## BC1 at each rgbcx level

400 BC1 files picked at random from the Fullrest Repack's loose textures, 16 to 2048 texels a side, 264.5 million
texels: each finest level decoded and scaled by a smooth light field of 0.7 to 1.3, as a painted colour's division
moves its texels off the file's palette, then encoded on one core. The error is against that scaled input.

| level | 3-colour blocks | Mtexel/s | RMSE | PSNR |
|---|---|---|---|---|
| 12 | on | 3.95 | 1.4787 | 44.733 dB |
| 10 (the build's) | on | 7.36 | 1.4931 | 44.649 dB |
| 10 | off | 11.73 | 1.5251 | 44.465 dB |
| 9 | on | 11.44 | 1.5076 | 44.565 dB |
| 8 | on | 15.76 | 1.5176 | 44.508 dB |
| 7 | on | 19.41 | 1.5324 | 44.423 dB |
| 6 | on | 23.59 | 1.5549 | 44.297 dB |
| 5 | on | 45.31 | 1.6038 | 44.028 dB |
| 5 | off | 87.80 | 1.7508 | 43.266 dB |
| 4 | — | 56.83 | 1.7289 | 43.375 dB |
| 3 | — | 87.96 | 1.7508 | 43.266 dB |

The file's own block with its two endpoints scaled by the light at the block's centre and its selectors kept:
233 Mtexel/s, RMSE 1.8300, 42.882 dB.

## BC7 for normal maps

120 of the PBR replacers' normal maps picked at random, BC1 and BC3, 16 to 2048 texels a side, 128.3 million texels:
each finest level laid as `ChainBuild` lays it, the unit normal in three channels and the height in alpha, then
encoded on one core with linear weights, mode 5 for a block with a height and 6 for one without. The error is the
angle between the normal laid and the normal decoded, and the height's against the height laid.

| search | Mtexel/s | mean angle | height RMSE |
|---|---|---|---|
| as the build searches, least squares on | 20.55 | 0.442° | 0.490 |
| least squares off | 26.87 | 0.511° | 0.490 |
| uber level 1 | 12.35 | 0.418° | 0.485 |
| mode 6 for every block | 18.62 | 0.362° | 1.712 |

## Threads

The PBR replacers' cold preparation with the preparation's thread count set by hand and the harness's hold on the
performance cores lifted, as the game runs:

| threads | CPUs | preparation | peak resident |
|---|---|---|---|
| 8 | the 16 of the performance cores | 107.5 s | 5,694 MiB |
| 16 | the 16 of the performance cores | 83.7 s | 5,397 MiB |
| 16 | all 32 | 79.7 s | 5,633 MiB |
| 24 | all 32 | 67.0 s | 6,029 MiB |
| 31 | all 32 | 64.0 s | 6,114 MiB |

## Byte-exact arithmetic

Checked over every float from −3 to 3, 2,155,872,258 values, against the build's functions:

- `quantise` as `int(double(v * 255.0f) + 0.5)` held to 0 and 255, against `std::lround`: no difference.
- `quantise(toEncoded(x))` read off 255 thresholds, each found once by bisection on the float's bits, and a table of
  4,096 starting bytes: no difference; 0.90 ns a value against 8.08 ns for `powf` and `lroundf`.

## The byte-exact arithmetic, landed

`unitByte` for `quantise`, `encodedByte` for `quantise(toEncoded(x))`, and the delight's light placed once a column
and once a row. Cold, as above, each store the same byte for byte as the one made before, the manifest aside:

| content | preparation | its threads' CPU |
|---|---|---|
| vanilla | 5.66 s → 4.50 s | 87.7 s → 68.5 s |
| Fullrest Repack | 364 s → 319 s | — → 5,038 s |
| PBR replacers | 84 s → 80 s | — → 1,217 s |

## A normal map's means in a ring of rows, landed

Each level's means a ring of eight rows, each row boxed as soon as the rows it reads stand, in place of 12 bytes a
texel under the finest level. The PBR replacers' cold preparation, the two builds in turn, the resident peak
sampled every 0.25 s up to the "Prepared" line; the two stores the same byte for byte:

| build | peak, first pair | peak, second pair | preparation |
|---|---|---|---|
| before | 4,900 MiB | 3,969 MiB | 87.8 s, 81.2 s |
| ring | 4,215 MiB | 3,383 MiB | 92.6 s, 81.3 s |

The peak moves by 900 MiB between runs of one build, with which threads meet the largest maps at once; the ring is
685 and 586 MiB under the run before it.

## Every usable CPU but one, the ground layers first, landed

The preparation on `Platform::Process::usableCpuCount() - 1` threads, 15 under the harness's 16 CPUs and 31 in the
game on this machine's 32, and the ground layers served before the models. The PBR replacers' cold preparation
under the harness: 77.8 and 81.8 s, its store the same byte for byte. Its busy threads a 5 s window, sampled at
99 Hz, from the first window to the last:

| window | 0 | 5–30 | 35 | 40–65 | 70 | 75 |
|---|---|---|---|---|---|---|
| busy threads of 15 | 14.5 | 15.0 | 12.6 | 14.5–15.0 | 14.2 | 10.0 |

Before it, the models first, the last window ran 2.3 threads of 16.

## Each model's store keys checked, landed

The manifest keeps each model's keys and a start asks the store for each. On the Fullrest Repack, against a store
that held every entry already:

| start | preparation | manifest |
|---|---|---|
| the first, its manifest of the format before, every model walked and every entry found | 4.69 s, 27.9 s of CPU on 15 threads | 6.68 MB before |
| warm, twice | 0.18 s and 0.17 s, 2.1 and 2.0 s of CPU | 9.06 MB |

Warm, the store stamps 101,934 entries a start. Before the change, a warm start asked the store for no entry
and took 0.15 s.

## The reserve

The memory at the preparation's start and its peak to the "Prepared" line, the manifest finding every job as it was
and the kernels compiled before: the Fullrest Repack 1,327 MiB at the start and 1,378 MiB at the peak, 51 MB; on
vanilla and the PBR replacers the preparation ended inside one sample of 0.1 s. A cold compile could not be had on
this card: with `XDG_CACHE_HOME` and `__GL_SHADER_DISK_CACHE_PATH` moved to empty folders, the longest pipeline
still compiled in 7 ms.

## The dispatcher, landed

Walks queue chain jobs, a dispatcher hands them out within a budget, and the loader's caches let go of each file as
its last job ends. The PBR replacers on 15 threads under the harness:

| run | preparation | its peak | counted at most | budget |
|---|---|---|---|---|
| cold, before | 77.8 to 81.8 s | 3,383 to 4,215 MiB | — | — |
| cold | 77.4 s | 3,715 MiB | 3,211 MiB | 53,889 MiB |
| cold, under a cgroup `MemoryMax` of 4 GiB | 78.5 s | 3,520 MiB | 2,560 MiB | 2,560 MiB |
| every model walked against a full store, before | — | 7,269 MiB | — | — |
| every model walked against a full store | 2.37 s | 1,968 MiB | 2,457 MiB | 53,852 MiB |

Each cold store the same byte for byte as the one before. Cold, the three content lists after the batches, on 15
threads under the harness: vanilla 5.9 to 6.1 s (the committed build beside it 5.9 s, run in turn), the Fullrest
Repack 337.6 s and 4,235 MiB at the peak, the PBR replacers 82.9 s and 3,479 MiB. Vanilla's 4.5 s after the
byte-exact batch did not come back on either build, its CPU time 86 s against 68.5 then. At the cold run's end the process fell from 3,589 to
1,042 MiB, and the scene built after it peaked at 4,937 MiB, as a warm start's does at 4,250.

## Scratch held per thread

From `ChainBuild` and `ChainPass` at a 4096-square file: the chain's texels, four bytes each over every level,
89.5 MB; a normal map's unnormalised means under its finest level, 12 bytes a texel, 67 MB; the alpha image; the
made chain, 10.7 or 21.3 MB, and its copy in the payload `ContentCache::keep` encodes. A thread keeps the largest it
met until the preparation ends.
