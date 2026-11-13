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

## The build, its tables read inline

`buildPaintedChain`, `ChainBuild::build` alone over the same corpus: its levels read, delit and boxed, 1.49 s an
iteration, 30.3 million texels a second, which is 40% of the pass once the encoder is as above. Its samples, the
corpus's making aside: `encodedByte` 26%, `delit` 23%, `paintedLight` 8%, `ChainBuild::build` 7%,
`ChainBuild::delight` 6%, `toLinear` 4%, `ShadingMap` 3%, `ColourBlock::read` 2%. A texel of a painted level
called eight functions of two other source files: the light's lookup, three decodes, the division and three
encodes, the decode's and the encode's tables each behind a first-ask check. By line, the encode's search for its
byte 12%, and in `delit` the greatest of three channels through `std::max`'s list, a call to `std::max_element`,
10%.

The same arithmetic with nothing between a texel and its tables (`DisplayCurve`, fetched once a loop), the
light's lookup and the division in their header, the greatest of three as two comparisons, and the encoded byte
one step past its bucket's start with no branch on the value, since no bucket holds two bytes' starts:

| benchmark | before | after | runs |
|---|---|---|---|
| `buildPaintedChain` | 1.49 s | 0.78 s, 57.5 to 57.9 million texels a second | three |
| `makePaintedChain` | 3.66 to 3.75 s | 2.99 to 3.01 s | three |

The digest of the made chains the same, `98d02ccb015d1514da0cc2e444d4b970`, and the made pictures' the same. The
pass whole from 6.75 s at the start of this note to 3.00 s; the encoder is 74% of it again.

What the build's samples hold after: `ChainBuild::delight` 46% with the encode's two lines 24% of the run, where
the division's wait is counted; `ChainBuild::build` 13%; `ShadingMap` 5%; `ColourBlock::read` 5%;
`toLinear(osg::Vec3f)` 4%, the estimate's read of a block's two interpolated colours through `powf`.

## A normal map's chain in BC7

The benchmark reads a corpus a kind since: a folder's files named as normal maps (`_n`, `_nh`) are read as normal
maps, whose chains the pass makes in BC7 a block in one mode, and the colours' corpus leaves the companion maps
out, `_spec` with them. So the Fullrest Repack's colours are another draw, 63 files and 45.08 million texels,
digest `b703d834d804669d1f211f7c46e4e214`: 0.77 s built, 2.68 s encoded, 3.45 s the pass.

The PBR replacers' normal maps, by their headers: 2,762 BC1 `_n`, 1,269 BC3 `_nh`, 123 of four loose bytes, 4 BC3
`_n` and 3 BC5; 5.45 thousand million texels at their finest levels, every one a format the build reads. The
corpus drawn of them is 27 files, 44.74 million texels over every level, digest
`0737a7a9e06ad6b30d2859f31f043be6`. On one core, three runs each:

| benchmark | an iteration | texels a second |
|---|---|---|
| `buildNormalChain`, `ChainBuild::build` alone | 0.58 to 0.59 s | 77 million |
| `encodeNormalBc7`, `ChainBuild::encodeBc7` alone | 1.55 s | 28.9 million |
| `makeNormalChain`, `ChainPass::run` whole | 2.13 s | 21.0 million |

A texel of a normal map is encoded in 0.57 of the time rgbcx's search takes one of the same folder's painted
colours now (35 files, 49.73 million texels, 3.03 s). What the encoding loses, over a map's three channels and its height where it holds one: a root mean square of 3.13 of 255,
38.23 dB, on the replacers' maps, and 5.03, 34.11 dB, on the Fullrest Repack's 45 (47.20 million texels, digest
`33690e992e56c75f0302e70a8745a86c`).

`perf record -e task-clock -F 1999` over `encodeNormalBc7`, the samples of the encoding alone:
`evaluate_solution` 48.3%, `color_cell_compression` 17.1%, `encode_bc7_block` 13.0%, `handle_alpha_block` 7.6%,
`find_optimal_solution` 6.6%, `compute_least_squares_endpoints_rgb` 4.5%, `ChainBuild::encodeBc7` 2.0%. By line:
`evaluate_solution`'s walk of a cell a pixel at a time, a projection, two palette entries and two distances in
64-bit sums, 30%; `set_block_bits`, a field written a byte at a time into the block, 7.5%; the rest sums of
floats a channel at a time. A block is two calls of `evaluate_solution`, one off the cell's principal axis and one
off the least squares of its selectors.

### The encoder made faster, the blocks the same

bc7enc with the vector instructions x86-64-v2 has, each step the same bytes over 1.12 million made blocks of
twelve kinds under ten sets of parameters (a normal map's two modes, every mode in RGB(A) and in perceptual
error, uber levels 1 and 4, other weights, mode 6's endpoints quantized, alpha forced), and over 3.36 million
again at the end, with its asserts on and off. Time a block of the normal-map set on one core:

| step | a block |
|---|---|
| as vendored | 516 to 527 ns |
| a cell's selectors sixteen pixels at once: the palette a table a channel, a selector's colour a byte shuffle | 349 ns |
| a block's fields into two 64-bit halves, its selectors packed into one field | 311 ns |
| the cell's mean, covariance, projection and RGB least squares a lane a sum; mode 5's alphas sixteen at once | 257 to 264 ns |

A sum of floats stays in the order it was added in, a lane a sum, since no reordering makes the same bytes; the
projection's least and greatest are of the same numbers in another order, which differ by a zero's sign at most,
and the mean they are added to drops it. The vector walk holds a weight's product in 16 signed bits, so the
weights of an error are held to 128 each by an assert: bc7enc's own defaults are 128 at most, and the tree
encodes with those and with one.

The same corpus after, three runs:

| benchmark | before | after |
|---|---|---|
| `encodeNormalBc7` | 1.55 s | 0.75 to 0.76 s, 59 to 60 million texels a second |
| `makeNormalChain` | 2.13 s | 1.32 to 1.33 s |

The digest the same, and the made pictures'. The Fullrest Repack's normal maps encode in 0.75 s. Every mode in
RGB(A) error, a map of data's, goes from 4,866 to 4,187 ns a block in one run of the two, and a colour's, in
perceptual error, from 5,609 to 5,451: its selector walk tries every palette entry and stays a pixel at a time.
Either is sixteen to twenty times a normal map's block, and neither has a corpus here.

The encoder's samples after: `color_cell_compression` 32%, `evaluate_solution` 27%, `find_optimal_solution` 13%,
`encode_bc7_block` 12%, `handle_alpha_block` 9%, `ChainBuild::encodeBc7` 4%; no line over 4.3%, the largest the
division inside the principal axis's three power iterations. What is left is chains of float additions and
divisions whose order the bytes depend on. The build is 43% of a normal map's pass now, 0.58 of 1.32 s.

## A normal map's build, a palette at a time

`buildNormalChain`, `ChainBuild::build` alone over the PBR replacers' normal maps: 0.58 s an iteration, 77.8
million texels a second, 43% of the pass once bc7enc is as above. Its samples: the laying of a texel 47.5%, the
box of a level and its rows 31.3%, the finest level's decode 11.1%, the band read 3.0%, `ColourBlock::read` 2.8%,
the alpha's decode 1.5%. By line, `unitByte` 40%, its product widened to a double and held by two branches, and
the square root and three divisions of a unit normal 23%.

Every texel was made a unit normal, made one again where it was laid and rounded a channel at a time: sixteen
times a block, whose texels choose among four colours. And one normal at a time waits for the one before it:
the build's scalar square root keeps the rest of the register it writes, which the normal before wrote last, so
a turn's two square roots and six divisions ran end to end and were still 35% of the samples at a quarter as many
turns.

The same arithmetic over the same numbers, in four steps, the corpus's digest and the Fullrest Repack's and the
made maps' the same at each:

| step | PBR replacers' maps | the made loose maps |
|---|---|---|
| as it stood | 577 ms | 2.31 ms |
| a block's palette turned and not its texels (`walkBand`); a ring texel four values, its height with it, so a tap of the box is one of four; a height's byte read off a table and not divided nine times a texel | 370 to 420 ms | 3.5 to 3.7 ms |
| four normals worked together, a lane each: a palette, four loose texels of a row, four texels of a boxed row | 331 ms | 2.06 ms |
| `unitByte` held by two choices and no branch, so twelve of them are worked as vectors | 287 ms | 1.63 ms |
| a row's place in the ring and in the level found once a row | 241 ms | 1.49 ms |

The second row's two figures are one source built twice: the turn stood in line in one build and out of it in
the other. A loose map has no palette, so at that step it was turned a texel at a time inside one loop and
slower than before, which the third step mends.

After, three runs: `buildNormalChain` 0.24 s, 186 million texels a second, and `makeNormalChain` 0.98 s, from
2.13 s at the start of the section above; bc7enc is 76% of the pass. The painted colours' cases are where they
were. What the build's samples hold after: the directions of four normals and their bytes 41%, the finest
level's walk, its unit normals and its laying 25%, the box 16%, `ColourBlock::read` 8%, the alpha's decode 4%.

