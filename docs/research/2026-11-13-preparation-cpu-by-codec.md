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

What the encoding loses of that corpus, the made chain decoded against the delit texels it was made of, red,
green and blue: a root mean square of 1.341 of 255, 45.58 dB. Of the made pictures: 1.326, 45.68 dB.

The encoder is 78% of the pass on one core, as it is 76% of `ChainPass::run` over the Fullrest Repack's
preparation. A run is 19 s with the corpus read and its chains made.

The rate follows the files drawn: 7.2, 7.9 and 10.2 million texels a second encoded over corpora of 26, 68 and 6
files of the same folder, drawn while the sampling was settled. So two encoders are compared over one corpus,
whose digest the label carries, and a rate is quoted with its corpus.

The three pictures the benchmark makes where no folder is named, 0.57 million texels: 6.2 to 6.3 million texels
a second encoded, 5.1 to 5.3 through the pass; CTest's dry run of it in the debug build takes 0.5 s.

## The encoder, a line at a time

`perf record -e task-clock -F 1999` over `encodePaintedBc1` on that corpus, three iterations on one core, the
corpus's making among the samples:

| function | samples | where inside it |
|---|---|---|
| `rgbcx::bc1_find_sels4` | 42.3% | a texel's projection on the block's axis and its rounding 25%, the squares of its two nearest palette entries 38%, the axis's reciprocal 7% |
| `rgbcx::encode_bc1` | 28.8% | spread: the sort of a block's texels along its axis 12%, each ordering's least-squares endpoints 12%, their rounding to 5:6:5 11%, by its lines over half a percent |
| `rgbcx::bc1_find_sels3_fullerr` | 14.6% | three squared distances a texel |
| `rgbcx::bc1_get_block_colors4` | 2.7% | |
| `rgbcx::encode_bc1_pick_initial` | 2.0% | |

The two selector searches are 57% of the run, each a loop over sixteen texels with an early exit once the
block's error passes the best so far.

## The corpus's blocks

Counted, as the section after this one is measured, by code added to the benchmark for it and not kept.

| of the blocks | finest levels, 2,106,688 | levels under, 702,261 |
|---|---|---|
| one colour throughout, which rgbcx encodes without a search | 21.3% | 11.3% |
| delit to the file's own texels, every channel | 1.0% | 0.5% |
| within one of them | 11.8% | 10.0% |
| within two | 22.7% | 20.7% |
| made as a three-colour block | 8.5% | 7.3% |
| made as the file's own eight bytes | 13.2% | 14.4% |

Of the finest levels' blocks 2.3% are three-colour in the file. Delight moves nearly every block, so a block
copied where it did not is a hundredth of them.

## The file's selectors kept, its endpoints fit again

Each block of the levels the files carry, 2,808,949, of which 98.0% are four-colour in the file: rgbcx handed
the file's selectors (`pForce_selectors`) with no search, so the endpoints alone are fit by least squares to
the delit texels. All of them in 0.46 s on one core, their decoding and two error measures included, against
5.25 s for the build's search.

| encoding of the four-colour blocks | root mean square | signal to noise |
|---|---|---|
| the build's search, level 10 | 1.347 | 45.55 dB |
| the file's selectors, endpoints fit again | 1.948 | 42.34 dB |

The fit is no worse than the search on 59.8% of those blocks, and the loss is in the rest. The fit kept where
its squares, summed over a block's 48 channels, stand at or under a limit, and the search run on the rest and on
the three-colour blocks:

| limit | blocks searched | every block's signal to noise |
|---|---|---|
| 0, every block searched but an exact fit | 95.2% | 45.580 dB |
| 16 | 81.8% | 45.578 dB |
| 32 | 71.3% | 45.566 dB |
| 64 | 61.9% | 45.556 dB |
| 96 | 52.4% | 45.519 dB |
| 128 | 43.4% | 45.454 dB |
| 192 | 26.6% | 45.200 dB |
| 256 | 15.0% | 44.899 dB |

A one-colour block whose fit stands over the limit counts among the searched and costs the search nothing: no
count was taken of how many of the searched are one colour.

## The search made faster, the blocks the same

Three changes to `extern/rgbcx/rgbcx.cpp`, each making the bytes it made before:

- the two selector searches walk a block four texels at a time (SSE4.1), each lane the scalar loop's arithmetic in
  its order, with no early exit: every caller keeps a result only where the block's whole error is under the best;
- a search over total orderings passes by endpoints it has tried: of the 45.4 million orderings the corpus's
  four-colour blocks try, 67.2% round to endpoints tried before in the same search, a third of those the best so
  far;
- the texels' order along the axis by counting in place of a sort, and each ordering's least-squares fit and its
  rounding to 5:6:5 with red, green and blue a lane.

`openmw_rtx_chain_encode_benchmark` on the Fullrest Repack's corpus, one core, the digest of the made chains
`98d02ccb015d1514da0cc2e444d4b970` at every step:

| step | `encodePaintedBc1` | texels a second | `makePaintedChain` |
|---|---|---|---|
| before | 5.28 to 5.30 s | 8.5 million | 6.74 to 6.76 s |
| the selector searches four texels at a time | 3.10 s | 14.5 million | 4.57 s |
| and endpoints tried once | 2.57 s | 17.6 million | 4.03 s |
| and the fit, the rounding and the order | 2.19 to 2.23 s | 20.2 to 20.6 million | 3.66 to 3.75 s |

The encoder alone 2.4 times as fast and the pass 1.8 times; the encoder is 60% of the pass from 78%. The made
pictures' corpus: 90.5 to 34.2 ms, its digest the same.

Held the same by:

- `RtxChainBuildTest.aChainsBlocksAreTheOnesTheSearchMadeATexelAtATime`, whose three digests were recorded off the
  encoder as it stood before;
- the two selector searches against the scalar ones over 40 million blocks and endpoints drawn at random, half
  under an error limit: the same selectors and error wherever the limit kept the result, and no result kept that the
  scalar loop refused;
- 400,000 made blocks through the encoder as it stood and as it stands: the same bytes.

Cold, as at the top of this note, with the first two of the three changes; the Fullrest Repack twice:

| content | preparation | its threads' CPU |
|---|---|---|
| vanilla | 3.05 s → 4.09 s | 44.9 s → 30.6 s |
| PBR replacers | 78.0 s → 67.3 s | 1,156 s → 1,000 s |
| Fullrest Repack | 331.0 s → 210.8 s, 327.2 s → 211.0 s | 4,934 s → 3,138 s, 4,874 s → 3,147 s |

Vanilla's 19,401 files and the PBR replacers' 32,027 the same byte for byte in both stores, the manifest aside.
The Fullrest Repack's second pair of stores held 105,292 and 105,289 files: nine small entries in the first alone,
six in the second alone, and every other the same byte for byte, the manifest aside; the first pair differed by
five files the other way and was deleted unread. An entry is filed under a digest of what it is made from, so a
store holds an entry the other lacks where its run met a file the other's did not, whatever encoded it.

The encoder's profile after the three, of the benchmark's samples: the selector searches 24%, and the rest of
`encode_bc1` 52% with no line over 5%, the rounding's midpoint lookups and the already-tried check its largest.

