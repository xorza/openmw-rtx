# Proposal: read the occlusion in a PBR specular map

Status: proposal. No code is changed. When it lands, its lasting decisions go to `docs/rtx/architecture.md`, and this
file is deleted. The measurements and sources are in `docs/rtx/research/2026-11-07-specular-map-blue-channel.md`.

## The problem

Under `[RTX] specular map layout = metal roughness`, the trace does not read B. The reason in
`components/rtxvulkan/shaders/lib/traversal.glsl:1342` is that the traced bounce and `ambientReaching` already find
what real geometry occludes. That reason is correct for the mesh and wrong for the texture. A ray hits only the mesh.
A normal map or a parallax shift does not stop a ray, so a crevice in a texture gets no occlusion from the trace.

B holds that crevice. 87% of the maps measured have a non-flat B, and most of its variance is at a scale far under a
triangle. `AGENTS.md` also asks that what a replacer's companion maps add reaches the trace.

The height does not reach the occlusion today. It only shifts the texture coordinates (`parallaxShift`).

## The established answer

There are two practices, and they make different assumptions:

- **A renderer that gives AO to indirect light only:** glTF, Filament, Lumen, and Wareya's shaders. It assumes that the
  rays or the screen do not see the map's scale.
- **A renderer with no AO input:** Cycles, Falcor, and RTX Remix. It assumes that the geometry or the displacement holds the detail.

This renderer traces low-poly meshes under relief that exists only in textures, so the first assumption is the true one.

## Decisions

- **Read B as occlusion under `metal roughness` alone.** The value is one under `classic` and on a surface with no map,
  so vanilla keeps its picture. Refused: B left unread, which keeps the crevice lit in all indirect light.
- **Indirect diffuse light only.** The bounce and the fill take it, and the sun and the lamps do not (glTF, Filament).
  Refused: B multiplied into the albedo. It darkens direct light, which the glTF specification forbids.
- **Put back by the composite, in the channels it already reads.** At the eye, `solidLookOf` writes the occluded albedos
  to `CHANNEL_ALBEDO` and `CHANNEL_AMBIENT_ALBEDO`. Only the composite and the wavelet's composing level read them.
  The direct stage uses `Surface::mAlbedo`, so `direct + albedo * indirect + (ambientAlbedo - albedo) * fill` scales the
  two indirect terms alone. Refused: B multiplied into the light before the filter, which blurs texture detail. Refused: a
  new channel, which costs memory for no gain.
- **Other readers of a surface keep what they mean.** A new `Surface::mOcclusion` keeps `mAlbedo` and `mAmbientAlbedo` as
  they are. The fill at a path's end takes it (`addOccludedFill`, `landingLightOf`). The pane response does not, as
  `CHANNEL_PANE_ALBEDO` also carries a pane's direct light. `liftOf` does not, as Night-Eye's lift has nothing to occlude it.
- **Multi-bounce correction, held to one.** Use Jimenez et al. 2016 in Filament's form, per channel of the albedo, with a
  `min` to one. Without the `min`, a visibility of one and an albedo of one give 1.0001, and a surface would make light. At
  a visibility of 0.5, an albedo of 0.068 gives 0.5, and an albedo of 0.6 gives 0.724. So most of MVR PBR reads B as it
  is, and a bright plaster does not go grey in its cracks. Refused: plain B, which a crack's own bounce light proves too dark.
- **No specular occlusion in this change.** `CHANNEL_SPECULAR` holds the lamps and the bounce together, so the composite
  cannot occlude one of them. It must go on the lobe's bounce before the filter. The cost of this refusal is that a
  crevice in metal reflects too much light. A later change can add Lagarde's form if the pictures need it.
- **No micro shadows on direct light.** They are an estimate that HDRP and Uncharted 4 use, and direct light is outside
  what B promises. The cost is that the relief casts no shadow from the sun under B.
- **No occlusion derived from the height or the colour.** Refused: a neighbour sample of the height at each hit, or at
  load. Only 65 of the 436 flat maps have a height, and a flat white B states no occlusion. Where B varies, it already has
  the shape of the height's cavities in most of MVR PBR (median r 0.754).
- **No new setting.** The layout already states what B means. Refused: a strength slider, unless the pictures show that
  it is necessary.
- **Land layers stay out.** A layer reads a `_diffusespec`, which has no occlusion. The cost is that ground takes no B.
- **The comment at `traversal.glsl:1342` goes** with the change. The scattering half of it stays.

## Risks

- **The diffuse is already dark in the crevices.** Where B is 0.59 of its surroundings in MVR PBR, the albedo is 0.53 of
  its surroundings. Under indirect light, that crevice goes to approximately 0.31, and under direct light it stays at 0.53.
  This is what the pack's shaders draw, but it is the main reason to look at pictures before this lands.
- **A map that bakes the mesh counts it twice.** The measurements show little of this (0.145 of the variance over 1/16 of
  a side), but no map states how it was made.
- **Tamrielic is strong.** Its B reaches a mean of 0.27 on one map. The four maps examined read as occlusion, but
  `SPECULAR_MAP_INTERPRETATION 2` in Rafael's shader was not read.

## Tests and checks

- `apps/components_tests/rtx/shaders/brdf.cpp`, for the correction in a shared header. A visibility of 1 gives exactly 1
  at each albedo. At a visibility and an albedo of 0.5, `a = 0.6878`, `b = -1.75585` and `c = 2.0679`, so
  `((0.5a + b) 0.5 + c) 0.5 = 0.6809625`.
- `apps/components_tests/rtxvulkan/trace/visibility/light.cpp`: a map with a low B scales the two albedo channels and leaves
  the lamps' channel and the lift. `classic` gives one. A landing's fill falls by the function, with and without a sky.
- `apps/components_tests/rtxvulkan/trace/visibility/pane.cpp`: a pane's lamps and its albedo do not change.
- `./omw kernels`: a tuple with no maps does not move.
- `./omw shot --views=all --map --upscale=off` on vanilla: no picture moves. On the PBR profile: the pictures moved
  are read by eye, with a before and an after at each place.
- `./omw release noise --ab=<switch>`, `./omw repeat --pairs=10` and `./omw release bench` on the PBR profile. The read
  already exists, and a channel does not, so I expect no noise or memory change. The bench must confirm the frame cost.

## Mods to download

Each one goes in a data folder of its own, so that the census can run on each pack alone. The first one answers an open
question. The next three test the proposal on content that the present measurements do not cover.

| Mod | What it decides |
|---|---|
| [Rafael's Shader Pack](https://www.nexusmods.com/morrowind/mods/53667?tab=files), file "Enhanced PBR Lighting" | What the shader does with B, its height AO, and `SPECULAR_MAP_INTERPRETATION 2`. Nexus refuses my fetch of the page. |
| [Tamrielic PBR - Dwemer Ruins 2K](https://www.nexusmods.com/morrowind/mods/57266) | Architecture from the same series as the armors, for the crevice risk on walls. |
| [Morrowind Watercolor HD Upscale](https://www.nexusmods.com/morrowind/mods/56990), its specular and normal files | A hand-painted diffuse, with AO "for some textures", for the risk of AO in the paint. |
| [SaintJ's Landscape Remastered 2.0](https://www.nexusmods.com/morrowind/mods/57594), folder `01 PBR` | Landscape, for the decision to keep land layers out. |

Useful but not necessary:

| Mod | What it adds |
|---|---|
| [PBRified](https://www.nexusmods.com/morrowind/mods/59078) | Maps for popular texture mods, from another author's process. |
| [PBR for Morrowind Enhanced Textures](https://www.nexusmods.com/morrowind/mods/57946) | Another base texture set. |
| [PBR maps for Wareya's core shaders](https://www.nexusmods.com/morrowind/mods/53645) | Maps made for the one shader whose B I read. |
| [Full Dwemer Retexture PBR](https://www.nexusmods.com/morrowind/mods/52217) | Maps made for Zesterer's shaders, to test whether `metal roughness` holds for them. |

I know these mods only from search summaries, so their file lists are not confirmed.
