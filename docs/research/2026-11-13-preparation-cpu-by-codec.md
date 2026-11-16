# Where the preparation's CPU goes, by codec — 2026-11-13

A high-end laptop CPU, release build, `scene --cell="Seyda Neen"` under SDL's offscreen driver, each content list
into a fresh `--content-cache`, 15 preparation threads. `perf record -e task-clock --call-graph fp` over the whole
run; a share is of the process's samples.

## Cold, before and after

The tree at `0011749652` against `2099a298be`, back to back, every entry of both stores the same byte for byte.

| content | preparation, before | after | its threads' CPU, before | after |
|---|---|---|---|---|
| vanilla | 3.16 s | 1.67 s | 45.6 s | 23.9 s |
| Fullrest Repack | 329.1 s | 152.7 s | 4,895 s | 2,271 s |
| PBR replacers | 77.7 s | 40.4 s | 1,145 s | 595 s |

Inclusive shares after:

| function | vanilla | Fullrest Repack | PBR replacers |
|---|---|---|---|
| `ChainPass::run`, every chain | 75.7% | 93.3% | 90.0% |
| `ChainBuild::encode`, rgbcx's BC1 | 54.2% | 63.6% | 25.2% |
| `ChainBuild::encodeBc7`, bc7enc | 3.0% | 5.6% | 39.3% |
| `ChainBuild::build` | 18.3% | 24.2% | 24.7% |
| of it, `ChainBuild::delight` | 13.4% | 17.8% | 4.4% |
| `ShapePass::run` | 9.8% | 3.3% | 0.9% |

Before, BC1 was over 70% of the Repack's run, BC7 40% of the replacers'. BC1 encoding at level 10 is two
least-squares passes and then the likeliest total orderings of a block, each ordering's endpoints solved and its
selectors searched: that search was the whole cost.

## What made it faster, each the same bytes

Each change is held by `RtxChainBuildTest`'s digests, recorded off the encoders as vendored, and by millions of made
blocks compared against the vendored encoder under every setting the tree uses, asserts on and off.

- **rgbcx's BC1**: the selector searches four texels at a time (SSE4.1), each lane the scalar arithmetic in its order;
  an ordering whose endpoints were tried before skipped (two in three are); the texels' order by counting; and every
  ordering's endpoints made first, with no branch among them. The encoder 2.4 times as fast on one core.
- **The painted build**: the colour tables read inline and a texel's light lookup and division in their header; the
  pass whole 6.75 → 3.00 s on the benchmark's corpus.
- **bc7enc**: the vector instructions x86-64-v2 has, every float sum kept in the order it was added in, since no
  reordering makes the same bytes; a partition's subsets and pixels estimated at once.
- **A normal map's build** a palette entry at a time, not a texel at a time: 2.13 → 0.98 s for the pass.

## What one core overstates

A gain that comes from fewer mispredicted branches shrinks when both hardware threads of a core are busy, as the
preparation keeps them: BC1's orderings change was 15.9% a block with the other thread idle and 3.9% with both
encoding, and the cold Repack's BC1 fell 7.6% where the benchmark said 21%. A change that removes instructions holds
in both. The benchmark (`openmw_rtx_chain_encode_benchmark`) is quoted with its corpus, whose digest the label
carries: the rate moved 7–10 million texels a second between draws of one folder.

## Refused: a painted block made with its file's own selectors

A BC1 block kept with the file's selectors and its endpoints fit again by least squares, searched only where the fit's
error passes a limit. On the encoder as it stands: at a limit of 96, 5.6% of the Repack's cold CPU saved and 11.9% at
128, for files a median 0.01–0.02 dB worse; 55 of 58 vanilla views moved, a view's worst pixel by up to 18 of 255;
the replacers' runs not told apart. Too little for every store made again and every picture moved.
