# Proposal: parallax occlusion mapping for `_nh` maps

Status: proposal. No code is changed. When it lands, its lasting decisions go to `docs/rtx/architecture.md`, and this
file is deleted. The sources are in `docs/rtx/research/2026-11-08-parallax-occlusion-techniques.md`, and the
measurements of the content are in `docs/rtx/research/2026-11-08-height-map-range.md`.

## The problem

A detailed hit with an `_nh` map moves its texture coordinates one step toward the eye (`parallaxShift`). One step
cannot find occlusion: a high texel does not hide a low texel behind it, and at grazing angles the offset is wrong.
Players ask for parallax occlusion mapping (POM), and the PBR replacer profile holds 1173 `_nh` maps that surfaces
read, 130 of them on land. Vanilla and the Fullrest Repack hold none.

The height also casts no shadow. The landed occlusion decision names this cost: "the relief casts no shadow from the
sun" (`docs/rtx/architecture.md`, "No specular occlusion and no occlusion from a height").

## The established answer

POM marches the eye's ray through the height field in the texture's frame and reads every map at the first texel that
the ray meets (Tatarchuk 2006). RTX Remix ships it in a path tracer of the same kind as this one. It marches at the
primary hit, it marches again toward each light for self-shadowing, and it leaves indirect hits on the flat triangle.
True displacement in the BVH costs 20 to 60 ms a frame on an RTX 4090 (PDM 2025, RMIP 2023), and the hardware route is
NVIDIA's alone, so it is not part of this proposal.

## Decisions

- **The relief that OpenMW states, marched.** The depth is `PARALLAX_SCALE`, 0.04 of the texture's side, and the
  height 0.5 stands on the triangle, as `PARALLAX_BIAS` puts it. The march uses the frame that `parallaxShift` uses.
  The maps were made for this: the mean height is 0.500 to 0.504 on 80% of them. At normal incidence the march and
  the shift draw the same texel. Refused: the relief all under the triangle, as classic POM puts it, which moves every
  texture away from where the rasterizer draws it. Refused: a depth of the fork's own, since the content was made
  against OpenMW's.
- **One function for the shift and the march.** The shift is the march with no step, so `parallaxShift` stays as the
  march's coarsest level and not as a second route.
- **Each map's height bounds, read at load.** The march spans the lowest to the highest height of the map and not
  nought to one: the median map spans 0.082 to 0.686, so the bounds remove 38% of the steps. The bounds come from the
  BC3 blocks of every level: the two alpha endpoints, and in the six-entry mode the nought and the one where an index
  names them. Nothing is decoded. They go in `GpuMaterial` and `GpuLayer` as two bytes, which is the precision of the
  alpha. Refused: a full decode of the alpha (`Rtx::AlphaImage`), which reads every texel to find two numbers.
- **A linear march with piecewise-linear refinement** (Tatarchuk 2006): fixed steps through the bounds, then one
  intersection in float between the last two samples. The refinement joins the 8-bit filtered heights with a float root,
  which removes the stair steps. Refused for now: the max-mip quadtree that Remix uses by default. Its chain is one more
  byte a texel, 3.2 GiB at full residency for the 1173 maps, which is as much as the maps themselves. And these heights
  are broad shapes, 63% of their variance at scales over 1/16 of the side, where a linear march needs few steps. It
  stays the candidate if the bench shows that the step count is the cost. Refused: relaxed cone stepping (GPU Gems 3,
  ch. 18), since BC3 has no channel left for the cone and its published precompute is O(n²) a map. Refused: binary
  refinement (Policarpo et al. 2005), whose steps are dependent reads where the linear refinement is one solve.
- **The step count and the level follow the ray's cone.** The count is the number of texels that the ray crosses
  sideways inside the bounds, at the level that the cone reads (`coneLod`), with a cap of 32. Where the crossing needs
  more steps than the cap, the march reads a coarser level, so that each step stays one texel wide, as Remix stops its
  descent at the level the footprint resolves. Where the crossing is under one texel, the march takes no step and is
  the shift, so a distant surface keeps its picture and its cost. The last step blends into the shift, so no ring
  appears where the count changes (Tatarchuk's threshold blend). The median map is 21 texels deep at level 0 and is
  crossed over 118 texels at 80° from the normal, which is why the cap needs the level. The cap 32 is a starting
  value for the bench to set. Remix uses 64, and Tatarchuk 8 to 50.
- **Detailed hits only**, with the rule that `resolve` already states: the eye's surface, a reflection, and the bed
  under a water line. A diffuse bounce's far hit reads no relief, which a 1024-frame reference could not tell from its
  noise. A reflection's wide cone gives it few steps or none through the decision above. Remix also leaves indirect
  hits out ("expensive").
- **Marched once, stored for the stages after the trace.** A new `STAGE_RELIEF` image, `r32f`, holds the march's
  distance along the ray in the texture's frame. `storeStageSolid` writes it, and `storedHitOf` hands it to the resolve
  in place of a march, so every stage resolves the same surface to the bit. It costs 4 bytes a pixel: 3.5 MiB at a
  1280x720 trace and 31.6 MiB at 3840x2160. A frame whose tuple holds no relief keeps it one texel wide, as the
  landing's own images are. Refused: a march in every stage that resolves the solid (`direct`, `bounce`, `unbounced`
  and the rest), which computes one answer several times. Refused: the visible distance in `STAGE_RAY_ALONG`'s fourth.
  That slot holds the distance the eye's ray traced to the triangle (`Surface::mDistance`), and a second meaning in it
  makes each reader check which meaning it holds.
- **The visible point for motion and history, the triangle's point for rays.** The surface carries how far past the
  triangle the eye's ray meets the relief: the march's distance times the triangle's world length of one texture unit,
  the geometric mean of its two tangent lengths. `motionOf` and the distance in the G-buffer read the visible point,
  because a texel 20 texels deep moves with its own parallax, and the upscaler and the denoiser follow the motion they
  are given. `leaveSurface` keeps the triangle's point, because a ray from under the triangle meets the triangle.
  Unity's POM gives a pixel depth offset for the same reason. In Remix's G-buffer I found no such offset, so
  `./omw noise --upscale=native` checks this decision. If the visible point moves no number, it goes, and the motion
  stays the triangle's.
- **Relief shadows on direct light.** The direct stage marches from the visible point toward the sun, the moons and
  the held lamp in the texture's frame, with the same bounds and the same level rule. Each sample under the height
  removes visibility in proportion to its depth, so the shadow is soft (Tatarchuk 2006, Remix's
  `pomSampleVisibility`). The factor multiplies the light (`Shadowed::mLight`) and not the ray's open bit, so the shadow
  filter does not blur a shadow the size of a texel. The hardware ray still starts from the triangle's point. Refused:
  relief shadows on the bounce, as Remix takes them, since a `_spec` map's blue already occludes the indirect diffuse
  light, and a second occlusion of the same crevice counts it twice. Remix has no such input. Refused: horizon maps
  (Sloan and Cohen 2000), several bytes a texel for each direction.
- **A specialization of its own**, `HAS_RELIEF`, true where the frame's scene places a material or a layer with
  parallax, as `HAS_MAPS` and `HAS_DARK_ALPHA` are. A vanilla tuple, and a mapped tuple with no heights, do not
  change, and each tuple keeps its own spill ceilings.
- **Cutouts stay out**, as `materialresolver.cpp:563` keeps them now. The candidate loop tests a cutout's opacity at
  the triangle's coordinates, and a march that moved the coordinates would draw a cut that the shadow rays do not
  see.
- **No new setting.** The content states its relief in its file names, as it does for the rasterizer. The harness
  gets an A/B switch for `noise --ab` and `bench`, and the game does not.
- **Land in a second change.** The stack draws the weighted sum of its layers' albedos, so the relief it draws is the
  weighted sum of their heights: a march over that sum, each showing layer read through its own transform, inside the
  layers' joint bounds. Refused: a march for each layer, which draws reliefs that no layer's albedo shows, at a cost
  for each layer. Refused: the dominant layer's march alone, with a seam where the dominant layer changes. Until the
  second change, land keeps the shift.

## Risks

- **Divergence and registers in the trace.** A loop of variable length goes into kernels that are bound by occupancy.
  The one-path rule asks for a measurement that the branch pays for itself. If it does not, the alternative is a fixed
  count for each hit, with the level rule alone. The integrated AMD parts' spill ceilings are the first limit.
- **More movement at grazing angles than the content was made for.** OpenMW's shift limits the offset, and the march
  does not. 491 maps have a spread of 0.5 or more between their 1st and 99th percentiles. A texture can swim or stretch
  at grazing angles. This is the main reason to read the pictures before this lands.
- **Flat silhouettes.** The relief rises 0.19 of the scale over the triangle at the median, and the triangle's edge and
  the silhouette cut it. POM cannot change this.
- **Texture seams.** A march near the edge of a UV island reads texels across the seam. Tiled walls and land have no
  islands, but armour, creatures and unique meshes do.
- **Two darkenings at a grazing sun.** `bumpShadowing` already dims a mapped normal's light at grazing angles, and the
  relief shadow dims it again where the relief stands in the way. The pictures at `seyda-neen-wall-noon` and at the
  facet line show whether the two together go too dark.
- **The relief shadow fades with distance.** A coarse level holds a flatter height field, so the level rule takes the
  relief shadow away as the cone grows. But the mean darkening of many small shadows inside one pixel is not nought,
  so a relief lit by a grazing sun gets brighter with distance. Remix's shadow march always reads level 0, and its
  denoiser takes the aliasing. The pictures at a grazing street decide whether this needs a fix.
- **The visible point in the denoiser's history.** The held surfaces compare distances. The visible distance in both
  frames agrees with itself, but a march whose step count changes between frames moves the distance, and the history
  can drop.

## Tests and checks

- The bounds reader, in `apps/components_tests/rtx/image/`: hand-built BC3 blocks. In the eight-entry mode, the
  endpoints 200 and 40 give 40 to 200. In the six-entry mode, the endpoints 40 and 200 give 40 to 200 where no index is
  6 or 7, and 0 to 200 where an index is 6. A level that reaches past the top level widens the bounds. A loose map
  reads its bytes.
- `apps/components_tests/rtxvulkan/trace/visibility/surfaces.cpp`, beside
  `parallaxShiftsTheSheetTowardTheEyeByTheNormalMapsHeight`: a flat height of 128 draws the texel the triangle holds
  at every angle. A step in the height hides the texels behind it at the hand-computed angle. A cone too wide for one
  step draws exactly what the shift draws. Two maps with different bounds draw the same picture where their heights
  agree, which proves that the bounds remove steps and do not change the answer.
- `apps/components_tests/rtxvulkan/trace/visibility/light.cpp`: a ridge between a texel and the sun takes the computed
  share off that texel's direct light, and leaves its bounce and its fill. A sun straight overhead takes nothing.
- `./omw kernels`: a tuple with no relief does not move.
- `./omw shot --views=all --map --upscale=off` on vanilla and on the Fullrest profile: no picture moves. On the PBR
  profile: each moved picture is read by eye, before and after, with a close wall, a grazing street and a lit niche.
- `./omw release noise --ab=<switch> --upscale=native --strafe` at one place first, then the suite, for the visible
  point's motion. `./omw repeat --pairs=10`.
- `./omw release bench` on the PBR profile, back to back on a hot card: the trace and direct zones' medians and p99, the
  worst frame, and the spill ceilings of the new tuples. Then the same bench at a cap of 16 and 64, to set the cap.
