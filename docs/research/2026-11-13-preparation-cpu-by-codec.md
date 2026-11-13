# Where the preparation's CPU goes, by codec — 2026-11-13

Measured on a high-end laptop CPU with the release build at `0011749652`: `scene --cell="Seyda Neen"` under
SDL's offscreen driver, each content list into a fresh `--content-cache`, on 15 preparation threads under the
harness's 16 CPUs. `perf record -e task-clock -F 249 --call-graph fp` over the whole run; a share is of the
process's samples, the load ahead of the preparation among them.

## Cold

| content | models and ground layers | preparation | its threads' CPU | store written |
|---|---|---|---|---|
| vanilla | 5,432 | 5.59 s | 47.1 s | 151 MiB |
| Fullrest Repack | 46,103 | 331.6 s | 4,934 s | 8.1 GiB |
| PBR replacers | 6,913 | 75.4 s | 1,118 s | 6.0 GiB |

## Inclusive shares

| function | vanilla | Fullrest Repack | PBR replacers |
|---|---|---|---|
| `ChainPass::run`, every chain | 23.6% | 96.7% | 95.0% |
| `rgbcx::encode_bc1` | 18.4% | 73.3% | 30.7% |
| `bc7enc_compress_block` | 1.1% | 5.7% | 40.0% |
| `ChainBuild::build`, decode and box | 3.8% | 17.0% | 22.7% |
| of it, `ChainBuild::delight` | 2.9% | 12.7% | 2.9% |
| of it, `ChainBuild::buildNormals` | — | 0.3% | 14.7% |
| `ShapePass::run` | 1.4% | 1.5% | 0.5% |

Vanilla's run is 19.6 s of which the preparation is 5.6, so its shares are of a process that mostly loads:
inside `ChainPass::run` the BC1 encoder is 78%.

By the shares and the threads' CPU, BC1 encoding is about 3,610 s of the Fullrest Repack's preparation and 340 s
of the PBR replacers', and BC7 encoding about 280 s and 450 s: BC1 is over five times BC7 across the two.

## Inside the BC1 encoder

Self time, of the process's samples:

| function | Fullrest Repack | PBR replacers |
|---|---|---|
| `rgbcx::bc1_find_sels4` | 35.2% | 14.2% |
| `rgbcx::encode_bc1` | 21.9% | 9.3% |
| `rgbcx::bc1_find_sels3_fullerr` | 12.3% | 5.7% |
| `rgbcx::bc1_get_block_colors4` | 1.9% | 0.8% |
| `rgbcx::encode_bc1_pick_initial` | 1.5% | 0.7% |
| `rgbcx::compute_least_squares_endpoints4_rgb` | 0.8% | — |

The build's level, 10, is two least-squares passes and then the 20 likeliest total orderings of a four-colour
block and the 8 of a three-colour one, each ordering's endpoints solved and its selectors found again
(`rgbcx.cpp`, `encode_bc1`): `bc1_find_sels4` and `bc1_find_sels3_fullerr` are that search's inner loop, sixteen
texels against four or three palette entries, a texel at a time. rgbcx holds no vector code, and the build
compiles it for x86-64-v2.

And inside BC7's, on the PBR replacers: `evaluate_solution` 18.4%, `encode_bc7_block` 6.6%,
`color_cell_compression` 6.3%, `find_optimal_solution` 3.7%, `handle_alpha_block` 2.3%,
`compute_least_squares_endpoints_rgb` 2.0%.

## The benchmark's corpus

`openmw_rtx_chain_encode_benchmark` on one core, the release build, `OPENMW_RTX_BENCH_TEXTURES` naming the
Fullrest Repack's loose textures: 18 BC1 files the chain pass makes a painted chain of, drawn from 512 of its
20,081 `.dds` files, 44.94 million texels over every level.

| benchmark | an iteration | texels a second | runs |
|---|---|---|---|
| `encodePaintedBc1`, `ChainBuild::encode` alone | 5.28 to 5.30 s | 8.50 to 8.53 million | three |
| `makePaintedChain`, `ChainPass::run` whole | 6.74 to 6.76 s | 6.66 to 6.68 million | three |

The encoder is 78% of the pass on one core, as it is 76% of `ChainPass::run` over the Fullrest Repack's
preparation. A run is 19 s with the corpus read and its chains made.

The rate follows the files drawn: 7.2, 7.9 and 10.2 million texels a second encoded over corpora of 26, 68 and 6
files of the same folder, drawn while the sampling was settled. So two encoders are compared over one corpus,
whose digest the label carries, and a rate is quoted with its corpus.

The three pictures the benchmark makes where no folder is named, 0.57 million texels: 6.2 to 6.3 million texels
a second encoded, 5.1 to 5.3 through the pass; CTest's dry run of it in the debug build takes 0.5 s.
