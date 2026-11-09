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

## Byte-exact arithmetic

Checked over every float from −3 to 3, 2,155,872,258 values, against the build's functions:

- `quantise` as `int(double(v * 255.0f) + 0.5)` held to 0 and 255, against `std::lround`: no difference.
- `quantise(toEncoded(x))` read off 255 thresholds, each found once by bisection on the float's bits, and a table of
  4,096 starting bytes: no difference; 0.90 ns a value against 8.08 ns for `powf` and `lroundf`.

## Scratch held per thread

From `ChainBuild` and `ChainPass` at a 4096-square file: the chain's texels, four bytes each over every level,
89.5 MB; a normal map's unnormalised means under its finest level, 12 bytes a texel, 67 MB; the alpha image; the
made chain, 10.7 or 21.3 MB, and its copy in the payload `ContentCache::keep` encodes. A thread keeps the largest it
met until the preparation ends.
