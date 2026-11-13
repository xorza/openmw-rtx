# Mip chains in one pass — 2026-11-13

A high-end NVIDIA laptop GPU. The chains under the images a dispatch writes — the sea's two cascades of two tiles
(512² and 128², RGBA16F), the wake's two tiles (1024², RGBA16F) and a ground composite's two (512², RGBA8, one
display-encoded) — built by a blit a level, a barrier between levels for every chain at once, against AMD's
single-pass downsampler (`MipChainPass`, `mipchain.comp`), a dispatch a chain and no barrier between levels.

## The chains alone, GPU timestamps

Debug build, the layers off; GPU work is the build's in every flavour. 100 frames after 20 thrown away, both ways
in one submit a frame, interleaved.

| chains | blit, median / p99 | one pass, median / p99 |
|---|---|---|
| ground, two of 512² | 0.092 / 1.536 ms; 0.098 / 0.160 ms | 0.086 / 0.143 ms; 0.096 / 0.156 ms |
| sea, two of 512² and two of 128² | 0.189 / 0.798 ms; 0.187 / 0.265 ms | 0.151 / 0.205 ms; 0.148 / 0.184 ms |

The wake's two 1024² chains: 0.61 ms by blits, 0.51 to 0.57 ms in one pass, of a zone of about 1.5 ms over a
footfall's ring, frame after frame.

## The release bench, 960×540

One run a side, `seyda-neen-ship` and `seyda-neen-pier` (1200 frames each) and the streaming route (597 frames), the
zones' medians and p99 from the record. No footfall stood in the water, so no frame stepped the wake.

| place | zone | median, blit / one pass | p99, blit / one pass |
|---|---|---|---|
| seyda-neen-ship | `waves` | 1.569 / 1.554 ms | 2.638 / 1.790 ms |
| seyda-neen-pier | `waves` | 1.572 / 1.550 ms | 2.599 / 1.750 ms |
| island-crossing | `waves` | 1.592 / 1.566 ms | 2.835 / 1.702 ms |
| island-crossing | `ground`, 344 frames | 0.633 / 0.681 ms | 2.009 / 1.872 ms |
| seyda-neen-ship | frame | 40.82 / 40.70 ms | 52.05 / 47.60 ms |
| seyda-neen-pier | frame | 39.68 / 39.56 ms | 49.83 / 45.56 ms |

The sea's p99 falls by a third at every place: a blit's level drained the queue, and the drains are what spiked.
The ground's median rose in the bench and not in the interleaved timing above, where its chains are no slower: the
zone holds the composite's bake and its BC7 encode beside the chain, one run a side.

## Pictures and kernels

`kernels` moved no tuple but the new kernel's. `shot --views=all --map --upscale=off` against the blits' pictures:
13 of 74 moved past the denoiser's noise, every one an exterior with water or ground in view and every interior the
same, with the scene the same on every frame and the trace's albedo, surface and light the images that moved. Most
by 1 to 6 of 255 on 1 to 4% of the pixels; the two that walk, `island-crossing` and `one-cell-walk`, by up to 47 and
38 on 1.5% and 1.4%, scattered pixels that read alike by eye. The means stay in full floats down the chain where a
blit rounded each level through the stored format, the ground's 8-bit levels most.
