# The trace's stages: memory and bandwidth

Three changes the review of the stages found and left undone: each moves pictures or adds a branch a
pixel, which a measurement on the target card decides (`AGENTS.md`, "One path through a shader").
Measured on a Radeon 680M alone so far, which is no NVIDIA card. Each lands as its own change, read
with `./omw release bench` on the RTX card and `./omw shot --views=all --map --upscale=off
--against=<dir>`.

- **The pane mask weighed where the frame is composed, and not in the bounce's stages.**
  `storeBounced` (`lib/bounced.glsl`) loads ten channels a pixel — the direct light, both albedos,
  the shadowed sources, the lamps, the lobe and its albedo, the panes and their albedo — only to
  weigh `CHANNEL_UPSCALE_MASKS`' pane share against the whole pixel's light. `bounce.comp` runs it at
  every pixel whose bounce escaped and `landing.comp` at every one that landed, and the composite
  (`composite.comp`) or the cascade's composing level (`atrouscompose.comp`) loads the same channels
  again to compose the frame. Weighed there, the bounce's stages load none of them. **What moves**:
  the mask is weighed against the filtered light and not the frame's raw one, so every upscaled
  picture moves, and the two composing passes both need the weighing (`compose.glsl`'s
  `composedLight` is already shared by them). To read: the stages' and the composite's zones, the
  upscaled pictures at the places with panes and water, and `./omw noise --upscale=native`.

- **The shadowed sources, the lamps and the lobe written once a pixel.** `visibility.comp` writes
  nothing into `CHANNEL_SHADOWED`, `CHANNEL_LAMPED` and, with maps, `CHANNEL_SPECULAR` at every
  pixel, and `direct.comp` writes all three again at every pixel a solid stands at: up to 48 bytes a
  pixel written twice. The trace would write them only where it met no solid, and `direct.comp`
  write them as nothing where its walk is cut short at `WALK_TURNS`, the one path of its own that
  writes none today. **What moves**: no picture; a store in the trace under a per-pixel test of the
  solid, which needs the bench to say the write it saves is worth the divergence it adds.

- **The bounce's hand-over folded into the throughput.** `STAGE_BOUNCE` (`RGBA32UI`, 16 bytes a
  pixel, full frame) carries the bounce's weight and the lobe's modulation word to `landing.comp`,
  for the pixels whose bounce landed alone. Multiplying the weight into `STAGE_THROUGHPUT`, which the
  landing reads beside it, and finding the modulation word a place in `STAGE_RECORD`, frees the
  image: about 15 MB at a 1280x720 trace and 130 MB at 4K, of the stages' 104 bytes a pixel and the
  composed frame's 8. **What moves**: where the product of the weight and the throughput rounds, so
  the bounce's pictures move by an ulp, and `STAGE_RECORD`'s spare bits are spoken for. To read:
  `./omw shot --against`, held to the denoiser's noise.
