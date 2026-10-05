# Review: the fork against upstream

> Whoever addresses an item deletes it. A group with no item left goes with its last item.

Scope: `git diff 4969186f34 HEAD` (upstream/master merge base, 2026-10-04), test code excluded, `extern/fidelityfx`, `files/licenses` and `files/lang` not reviewed. Eleven reviewers read one part each. Groups are sorted by severity and benefit inside each section, and the sections are sorted the same way. A "Checked by the merge" line means the claim was read again in the code. All other items are the reviewers' reading and are not run.

## Pictures that are quietly wrong

The trace, the denoiser or the rasterizer shows a result that is not the light of the scene, with no error.

### A material is keyed on the nearest state set but read from the whole chain

> Checked by the merge: the key is `shading.back().mStateSet` and `readMaterial` reads the whole `shading` span.

- [ ] `components/rtx/mirror/materialresolver.cpp:277-305` (`resolve`), `:209-237` (`read`, the ring's
  reading through `TemplateWalk::take`) — **[bug]** `describeSurface` folds every link of the chain into
  the material: textures, `GL_CULL_FACE`/`FrontFace` (two-sidedness), `OVERRIDE` locks. The key is only
  `shading.back().mStateSet`. NifOsg puts an `NiNode`'s own properties, `NiTexturingProperty` and
  `NiStencilProperty` included, on *that node's* state set (`nifloader.cpp:505-557`, `applyNodeProperties`),
  and puts the material, alpha and vertex-colour properties on the shape's state set. Two passes then
  make equal state sets into one object. `Resource::SharedStateManager` does it across every loaded
  file: osgDB's default mode is `SHARE_ALL`, called at `scenemanager.cpp:927` and `:1017`. The
  optimizer's `SHARE_DUPLICATE_STATE` does it inside one model. Take two shapes whose own state sets are
  equal (the same material and alpha) but whose parent `NiNode`s name different textures, or where one
  parent is two-sided and the other is not. They resolve to one material, and the second is drawn with
  the first one's texture and sidedness. Whichever is met first decides, so the result depends on load
  order and walk order. The ring adopts under the same key, so the error also reaches the distance.
  The comment at `:277-280` gives the reason for the key: "what the parents above contribute … is
  light and render-bin state rather than material". That is false for NifOsg content.
  → Target shape: key the material on what it was read from. While the chain is built
  (`Shading::under`, the way `mAnimatedThrough` is folded), fold a chain identity from the pointers of
  the links that `describeStateSet` reports as stating something. Key the material on that, or on the
  nearest state set plus the nearest ancestor that states a texture or a face mode. Use one function
  for both `resolve` and `MaterialResolver::read`, so that the ring and the walk still meet under one key.

- [ ] `components/rtx/mirror/materialresolver.cpp:84-161` (`animate`), `sceneextractor.cpp:571-577` —
  **[bug, conditional]** The header (`materialresolver.hpp:130-136`) says the per-node state set exists
  because "what a material is keyed on has to be unique to the placement". `mAnimated` is keyed on the
  node handed in. For a drawable with a state set of its own under an animated chain, that node is the
  drawable, and `SceneUtil::CopyOp` shares drawables between clones (`clone.cpp:58-73`; only rigs, morphs
  and particle systems are copied). Every instance of the model under any animated chain therefore
  gets one shared `Animated` state set and one material. Each placement rewrites that material
  (`resolve`, `own.mAnimated`) with its own chain, and the last placement walked wins. Example: two
  different enchantments on one base model, both in view. Both show the glow colour of whichever was
  walked last, and the row is rewritten twice a frame. → Target shape: key the animated copy of a
  drawable on the placement (the drawable together with the path identity `who`, or the nearest
  per-instance node), never on the shared drawable.

### A reused ESM::Cell carries the last cell's groundcover into the next

> Checked by the merge: `GroundcoverStore::initCell` calls `Cell::blank()`, which leaves `mContextList` alone, and assigns it only for a cell that has groundcover.

- [ ] `apps/openmw/mwrender/rtx/tracedgroundcover.cpp:89` — **[bug]** `collect` keeps one
  `ESM::Cell mCell` across cells and refills it with `GroundcoverStore::initCell`, which calls
  `ESM::Cell::blank()` and assigns `mContextList` only where the store has an entry for the cell.
  `blank()` (`components/esm3/loadcell.cpp:326`) does not clear `mContextList`. So for a cell
  the groundcover files place nothing in (sea, towns, bare rock: most cells), the context list
  is still the previous cell's. The loop then restores those readers and reads the previous
  cell's plants again, with their world positions, and hands them to
  `CellReader::readGrass` as this cell's. The result: every grass-free cell read right after a
  grassy one stands a second copy of that cell's plants at the same transforms. That doubles the
  instances in the BVH and the alpha-tested hits, and the copy outlives the original when the
  first cell leaves the ring. Upstream's `Groundcover::collectInstances` makes a fresh
  `ESM::Cell` per cell, so it never hit this.
  → Target shape: clear `mCell.mContextList` before `initCell` (or have `initCell` clear it, so
  every caller is safe). Either way the scratch cell keeps its capacity and nothing is allocated.

### FSR's auto-exposure reset sentinel is cleared to a value the shader treats as a measurement

> Checked by the merge: `sFreshFrameInfo` stores `1.0` in `.y`, `ffx_fsr3upscaler_luma_pyramid.h:97` smooths below `resetAutoExposureAverageSmoothing = 1e4`, and the frame info is the bound exposure of every FSR pass.

- [ ] `components/rtxvulkan/upscale/upscaler.cpp:298` (and its uses at `:405` and `:568`) — **[bug]**
  `sFreshFrameInfo` clears `FRAME_INFO_LOG_LUMA` to `1.0`. The luma pyramid
  (`extern/fidelityfx/gpu/fsr3upscaler/ffx_fsr3upscaler_luma_pyramid.h:97`) only takes a fresh
  frame's log luma outright when the stored one is `>= resetAutoExposureAverageSmoothing` (`1e4`,
  `ffx_fsr3upscaler_common.h:102`). That check exists to detect the reset value. With `1.0` stored
  it never fires, so after every reset (each cut, teleport, worldspace change, resize, mode switch)
  the exposure eases from a log luma of 1 toward the scene's own at `1 - exp(-dt)` a frame, which is
  a time constant of about one second, and `ffxMax(0, …)` clamps it on the way. `FRAME_INFO_EXPOSURE`
  is the `Exposure()` every later pass reads. It scales the input colour in `upsample.h:76,103-105`,
  the reprojected history in `reproject.h:74` and `accumulate.h:161`, and the luma samples in
  `prepare_reactivity`, `shading_change_pyramid` and `luma_instability`. So for the second or so
  after every cut, the accumulation's tonemap and its lock and reactivity thresholds work at the
  wrong exposure. The picture is quietly different, and no test sees it because it converges. The
  comment ("a luma of one") reads the slot as a luma when it is the log luma's reset sentinel.
  → Target shape: clear `.y` to a value at or above the shader's threshold, as the SDK host does
  (`ffx_fsr3upscaler.cpp` clears FRAME_INFO to `{-1, 1e8, 0, 0}`). The format is RGBA32F, so `1e8`
  is representable. Also fix the comment.

### The two halves of a stopped shadow ray

- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:612-629`; `shading.glsl:203`, `:264`
  — **[bug]** With `split` set, `gather` keeps `Passage::mThrough` "as though the rays got through",
  even when `mOpen` is 0. `passageToward` traces with `gl_RayFlagsTerminateOnFirstHitEXT`, so on a
  stopped ray `blocked` sums only the see-through candidates the traversal happened to visit before
  the first solid. That includes media and panes beyond the occluder, and the set depends on BVH
  order: the very order-dependence `SHARE_UNIT` was introduced to remove.
  - `composite.comp:91-94` multiplies this pixel's own `CHANNEL_SHADOWED` light by the filtered
    visibility of its neighbourhood.
  - So in a penumbra, an occluded pixel's unshadowed sky or lamp light comes back scaled by whatever
    random part of the translucent layers was visited. Examples: a blight cloud shell beyond a roof,
    a window behind a wall.
  - That value changes with every TLAS refit, so it is neither repeatable nor the light of any path.

  → Target shape: a stopped ray reports an order-free through, `mThrough = 1` where `mOpen = 0`,
  since the stopped ray's through is unknown and nothing reads it unsplit. Document that the bit
  then carries the only shadow information.

- [ ] `components/rtxvulkan/shaders/lib/underwater.glsl:135-145`, `:290` — **[bug]** `skyPassage`
  returns open for `frame.mNoSkyShadows` (every picture inside the interface, `visibility.h:198-201`).
  `skyPassageThrough`'s underwater leg calls `lightPassage` directly and ignores the flag. In a local
  map tile or a preview, a submerged bed under a hull or a pier, and the water shaft march, are still
  shadowed, while the dry ground beside them is not. → Target shape: the flag answers both legs.
  Gate `skyPassageThrough` itself, so one test covers the surface, the shaft march and the froxel.

### What the reuse's validation reads

- [ ] `components/rtxvulkan/shaders/trace/bouncevalidate.rgen:80`, `:84-90`;
  `lib/shading.glsl:133-140` — **[bug]** The validation re-shades a kept sample with
  `bounceLanding(..., PATH_INDIRECT)`. Out of doors, `gather` then draws the `INDIRECT_LIGHT_RATE`
  coin, so half the time the far end's direct light is dropped and half the time it is doubled.
  - The coin's seed is `pixelKey(pixel) + SEED_INDIRECT_LIGHT`, the same one the trace's own bounce
    landing used for that pixel this frame.
  - The validation acts only on a fall past `BOUNCE_VALIDATION_FALL = 2`. A sample whose light is
    mostly sun therefore reads fill-only on every "unlit" coin and is replaced, with confidence 1, by
    that dark value. A "doubled" coin is never applied.
  - The result is a systematic darkening of reused exterior bounce. This fits `.notes/reuse.md`'s
    "Outdoors by day: either reuse adds 0.06 to 0.10 of bias", a figure the reuse A/Bs are judged on.

  → Target shape: the validation shades the far end without the rate coin (a `PATH_INDIRECT` whose
  rate is one), or draws it from `SEED_*_VALIDATED`. The fall test then compares estimators whose
  only noise is the lamp pick and the occlusion ray, as `BOUNCE_VALIDATION_FALL`'s comment says.

### The temporal filters never see the previous frame's jitter

> Open question from the merge: an accumulated history is the mean of many jittered samples, so its texel centre is the pixel centre, and only the last frame's surface channels and a short history sit at `+ jitterPrev`. Decide which histories take the previous jitter before the change, and measure with `noise --strafe`.

- [ ] `components/rtxvulkan/shaders/lib/surfacematch.glsl:40` (`historyFootprint`), used by
  `trace/denoise/accumulate.comp:267,292`, `trace/denoise/shadowtiles.comp:425`,
  `trace/denoise/specular.comp:128`, `trace/denoise/pane.comp:185`, `trace/bouncetemporal.comp:150`.
  **[bug]**
  - **The defect.** The footprint is `corner = at + 0.5 + jitter + moved - 0.5`. That puts each
    history texel `i` at `i + 0.5` on the previous unjittered screen. But texel `i` of every history
    (the cascade's first level, the shadow history, the glossy and pane means, the surface history)
    holds what the previous frame's ray through `i + 0.5 + jitterPrev` found.
  - **The concrete failure.** Take a still eye under any jittered mode (FSR at every mode, native
    included). `moved` is 0, so `corner = at + jitter`. Every frame each history is re-sampled as a
    bilinear mix of the pixel and a neighbour, weighted by this frame's Halton offset. The correct
    offset is `jitter - jitterPrev`.
  - **What the error does.** It is `jitterPrev`, up to half a pixel per axis, random every frame. It
    adds up as a sub-pixel random walk on each history:
    - The glossy and pane means have no spatial pass, so their highlight and window edges smear.
    - The shadow history's penumbra edge smears the same way.
    - At silhouettes, the depth test (`heldSurfaceMatches`) reads the neighbour's distance and
      accepts or refuses the wrong taps.
  - **Not in the plumbing.** `HistoryConstants` (`accumulate.h:138`) carries only this frame's
    `Eyes`. `Basis` (`camera.h:14`) states outright that a previous eye keeps "this frame's"
    jitter. `FsrFrame::advance` keeps `mPreviousJitter` for FSR alone (`fsrframe.cpp:47`).
  - **Field practice.** NRD asks for `cameraJitter` and `cameraJitterPrev` for this reason. FSR's
    own reprojection uses `PreviousFrameJitter()`.
  - **The tests cannot see it.** `aStillPictureHoldsStillThroughEveryUpscale` passes, because a
    blur that is the same every frame holds still.
  - **The same mistake in a second place.** `accumulate.comp:147-150` (`samePlane`) rebuilds the
    previous frame's ray through `tap` with this frame's jitter (`eye = frame.mEyes.mWorld;
    eye.mBasis = previous`). The stored `w` was measured along `tap + 0.5 + jitterPrev`.
  - → **Target shape:**
    - Carry the previous frame's world jitter in `HistoryConstants`. The host has it, and the
      `ImagePair` turn says when it is valid.
    - `historyFootprint` takes it and returns `corner = at + jitter - jitterPrev + moved.xy`.
    - `samePlane` rebuilds through a camera with the previous basis and the previous jitter.
    - `specular.comp`'s `toEyeBefore` and the accumulator's `anchor` read the unjittered previous
      screen position. They stay as they are.
    - This is one change in the shared helper plus one field, and all five filters pick it up.

### The spatiotemporal resolve keeps an unvalidated reservoir as history

- [ ] `components/rtxvulkan/shaders/trace/bounceresolve.rgen:127,133,142` **[bug]**
  - **The defect.** `asked = merge.mKeptInput > 0u` is true whenever a neighbour's sample won the
    spatial merge. Then `seen` is true without a ray, and `bounceHistory[at] = packBounce(seen ? own
    : noBounce())` stores the pixel's own temporal reservoir. Nothing checked that reservoir's
    sample from this point this frame.
  - **The comment it contradicts** (lines 134-136): "nothing where it is hidden from here now: a
    sample behind a door that closed is let go of at once".
  - **The concrete failure.** An aged own sample (`mAge > 0`) that is now occluded survives in the
    history for as long as neighbours keep winning. The next frame's temporal merge takes it back
    in by its confidence, up to `BOUNCE_AGE_CAP`. That is the lingering light the separate history
    was written to avoid.
  - **Reach.** Only `--bounce-reuse=spatiotemporal`, which is off by default but kept for A/Bs.
  - → **Target shape:** where a neighbour won and `own` holds an aged sample, ask `bounceSeen(origin,
    ownReach)` for the history write. Or record which reservoir's visibility was actually
    established, and store `own` only when it was.

### The painted-light estimate averages texels the alpha says are not there

- [ ] `components/rtx/image/shadingmap.cpp:47,81,89` (and `components/rtxvulkan/shaders/texture/shadingsum.comp:67`,
  which it is held to) — **[bug]** A texel is left out of `ShadingMap`'s cell means only when it is BC1's
  punch-through entry. BC2 and BC3 blocks are summed with `punchThrough = false`, so all 16 texels count, and a loose
  RGBA texel never reads its alpha. The texels where a cutout is a hole are usually painted black or with bled colour,
  and they enter the luminance of every cell they share with the leaf. Those cells' estimate drops, and the surface is
  divided by a factor below one there, up to `1 / SHADING_FLOOR = 2×` brighter at the leaf's edge. A cell that is
  entirely hole also pulls the normalising mean down. `material.hpp` says every DXT3 leaf, banner and rope the game
  ships has a soft edge, so vanilla foliage is affected, not only replacers. The code's own comment states the rule
  ("A transparent texel is not a colour and does not belong in an average"), and BC1 alone follows it.
  → Target: weight each texel's luminance and count by its alpha, as `MipChain` already weights colour. A block sums
  `Σ a·L` and `Σ a`, a loose texel uses its fourth byte, and BC1's binary rule becomes the special case. Change the
  device sum to the same rule so the host/device test still holds.

### The mip chain's rule applies colour semantics to data and loses odd edges

> Checked by the merge: the level is `floor(above / 2)` wide, so the `min(2x + dx, above - 1)` clamp never fires and the last odd column and row are never read.

- [ ] `components/rtx/image/mipchain.cpp:39,106` with `components/rtx/scene/scenetextures.cpp:146` — **[bug]** A file
  with one level gets `mCompleteChain` under every encoding. The chain then weights each level's RGB by its alpha
  (`weighed += texel * alphaHere`), and the device does the same (`mipchain.comp:77-84`). Alpha is coverage only in a
  colour slot. In an `_nh` map it is the parallax height, and in a classic `_spec` map it is the exponent. So a
  single-level normal-height or specular map gets coarser levels whose normals are weighted by height, or whose
  specular colour is weighted by gloss, and where the alpha is zero the RGB is dropped entirely. `mEncoded` only
  controls the sRGB decode, not whether alpha means coverage.
  → Target: weight by alpha only for `TextureEncoding::Colour`, and use an even box for `Data` and `Normal`. Carry
  the encoding into `MipChainConstants`, so the host statement and the kernel apply one rule.
- [ ] `components/rtx/image/mipchain.cpp:88` — **[bug]** For an odd extent `W`, the next level is `floor(W/2)` wide
  and reads columns `2x, 2x+1 ≤ W-2`, so the last column and last row of every odd level never reach the level below.
  A 3×3 level drops 5 of its 9 texels into nothing. The `min(…, above.mWidth - 1)` clamp only takes effect when the
  level above is 1 wide. `mipchain.comp:66` says "the level above's last texel serves twice along an odd edge, which
  is how the host halves an odd extent", but neither side does that. Any non-power-of-two single-level texture is
  affected.
  → Target: an odd-aware reduction, the standard practice for non-power-of-two chains (NVIDIA, *Non-Power-of-Two
  Mipmapping*, 2005). On an odd axis with `n = floor(W/2)`, output `x` reads source texels `2x, 2x+1, 2x+2` with
  weights `(n-x)/(2n+1)`, `n/(2n+1)` and `(x+1)/(2n+1)`, which together cover every source texel exactly once.
  Apply it identically on host and device, and correct the kernel's comment.

### The night sky reader skips what its sibling readers apply

- [ ] `components/rtx/environment/nightsky.cpp:138` — **[bug]** `LayerReader` takes each vertex's direction from its
  raw `Vec3Array` value. `readCloudShell` and `readAtmosphere` both place their vertices through
  `computeLocalToWorld(getNodePath())`, and their comments say why: Morrowind's sky meshes hang under transforms (the
  cap sits 15 units under its `NiNode`, and the atmosphere is rotated upside down). Any transform in a star dome, from
  vanilla or a sky mod, is ignored here while the rasterizer draws it. A translation biases every direction, patch
  centre and angular radius, and therefore `mHorizon` and `mUvRate`. A rotation moves the patches and the field
  without telling the trace. The header promises that a replaced mesh is read as it is drawn. `imageOf` (`:205-207`)
  also looks only at the drawable's own state set and its first parent's, and misses a texture bound higher up the
  path.
  → Target: place vertices as the two siblings do (one shared `placedVertices(geometry, nodePath)` helper for all
  three readers), and fold the state sets down the node path to find the sheet.
- [ ] `components/rtx/environment/skybuilder.cpp:279-290` — **[design]** Each patch is redrawn as a disc with an
  invented orientation (up toward the zenith) and an invented shape (radius only). The three constellations are drawn
  figures, and the reader measured a 1.5-tile long axis on them. They come out turned and squashed compared with the
  rasterizer's picture, which breaks the rule that the vanilla picture does not change. The comment argues that a
  wash or a scatter cannot look turned, but a figure can. The orientation and aspect can be recovered: `LayerReader`
  holds every vertex's UV beside its direction.
  → Target: fit each patch's UV axes against its directions at read time, the same least-squares fit `fitSheet`
  does for the cloud cap. Store `mRight`/`mUp` scaled per axis in `NightSky::Patch`, and let `describePatches` only
  roll them with the sphere.

### The twin fold matches positions and ignores the attributes it throws away

- [ ] `components/rtx/preprocess/shape/shapefold.cpp:207-283`, `shapepass.hpp:24-38` — **[bug,
  conditional]** A reversed twin is dropped when its three positions match. Its texture coordinates,
  vertex colours and normals are never compared, and `ShapePass::Input` does not even carry
  coordinates or colours. From then on the kept triangle stands for both faces with its own
  attributes. Where the back copy maps another part of the atlas, mirrors its UVs (so heraldry or
  text reads correctly from behind), or carries different baked vertex colour (a darker painted
  side), the trace shows the front's mapping mirrored and the front's colours from behind. The
  rasterizer shows the back copy's. `dropPockets` has the same blind spot: it keeps the first wall's
  mapping. This breaks AGENTS.md's "a vanilla picture does not change" for exactly those meshes.
  → Target shape: a pair is a twin only where the reversed corners also carry equal coordinates (both
  sets) and colours. A pair that differs stays as two triangles, and the mesh is flagged so that a ray
  takes the face its winding faces — the culling `FoldedShape::mFolded` now turns off. First, count over
  the vanilla archives how many twin pairs differ in their attributes, to size the problem.

### Hooks that answer for a renderer that is no longer current

- [ ] `apps/openmw/mwrender/rtx/tracedterrain.cpp:186` — **[bug]** `TracedTerrain` hangs
  `mAnswer` (under its terrain root) and `mStaticsAnswer` (under the scene root), and does not
  override `Terrain::World::enable`, which is a no-op in the base. When
  `RenderingManager::enableTerrain` swaps to another exterior worldspace's ground, the old
  ground's two answers stay in the graph and keep answering every CPU ray. They ask
  `mDistance` (the one ring, now following the new worldspace) which cells stand, then map the
  segment to cells with the old storage's cell size and fill `mFar` from the old storage's land.
  So in a second exterior worldspace (ESM4 content), casts meet phantom ground from the old
  worldspace's heights at the new worldspace's cell indices, plus duplicate static hits. Interiors
  are spared only because the ring drops its slots indoors. The rasterizer's `QuadTreeWorld`
  does override `enable`.
  → Target shape: `TracedTerrain::enable(bool)` masks `mAnswer`, `mStaticsAnswer` and `mBorders`
  off, so the only ground answering a ray is the one the game drives.

- [ ] `apps/openmw/mwrender/rtx/tracedoverlay.cpp:159` — **[bug]** `finish` drops a pending paint
  as soon as it holds the tile's last reference (`use_count() == 1`). Under the ray tracer a
  tile's picture arrives frames after `exploreCell`: up to three world views a frame,
  `waitsForGround` deferral, then two frames until the copy lands. In that window
  `LocalMap::removeExteriorCell` (`mapwindow.cpp:434`, once the cell leaves the map grid) and
  the widget entry can both let go. The paint is then discarded, and that explored cell stays
  black on the world map for the session. It happens when the player crosses cells quickly
  right after a load or teleport, while the ring is still adopting ground. The rasterizer
  takes the tile's texture into the blit camera at once (`GlMapOverlay::paintTile`), so it
  always paints.
  → Target shape: the pending paint keeps the view alive until its copy lands, and the queue
  draws it whether or not the local map still lists it. If a view's draw should stop when the
  map lets go, the overlay should say so explicitly, not infer it from a reference count.

### The rasterizer's no-technique resolve ignores the frame it should draw into

> Not run: the canvas draws to the bound framebuffer on this path. Confirm with GL, post-processing off, and `[Video] resolution` unlike the window.

- [ ] `apps/openmw/mwrender/pingpongcanvas.cpp:143` — **[bug]** When the canvas has no
  technique to run (`filtered.empty() || !mPostprocessing`) it draws the scene texture into
  whatever framebuffer is bound, which is the window. That covers the default
  `[Post Processing] enabled = false`, and post-processing on in a room whose techniques are all
  exterior-only. Only the technique path calls `bindDestinationFbo()`. Upstream never set
  `mDestinationFBO`, so the window was always right. Under the fork's presentation
  (`GlRenderer::makeFrame`, `mFramed` true whenever `[Video] resolution x/y` differs from the
  window), `PostProcessor::setOutput(mFrameFbo)` expects the world in `mFrame`. The GUI camera
  then draws into `mFrame` without clearing it (`wireFrame`: `GL_NONE` once a world exists), and
  `mPresent` clears the window and draws `mFrame` over it. The world is overwritten and the
  screen shows the interface over uninitialised texture. It only shows up at a non-native
  resolution, and the shipped default is native (`resolution x = 0`), which is probably why
  nobody has seen it. Not run here: confirm with GL, PP off, and a resolution unlike the
  window's.
  → Target shape: the fallback binds the destination exactly as the resolve pass does
  (`bindDestinationFbo()` plus `resolveViewport->apply`), so every path of the canvas ends in
  the one destination.

### Gamma under multiview reads no texture

> Checked by the merge: `:190` gives the resolve texture to `mMultiviewResolveStateSet` only.

- [ ] `apps/openmw/mwrender/pingpongcanvas.cpp:192` / `:416` — **bug** With stereo multiview,
  post-processing on and `[Video] gamma ≠ 1`, the final multiview resolve pushes
  `resolveStateSet(true)`, which is `mMultiviewGammaStateSet`. Only `mMultiviewResolveStateSet`
  ever gets the resolve framebuffer's texture at `Unit_LastShader` (= 0), set at line 192.
  `mMultiviewGammaStateSet` has a program and uniforms but no texture, so the draw samples whatever
  unit 0 last held: the last pass's input rather than its output. Moving the gamma slider off 1 in
  VR silently drops the last post-processing pass, or shows the wrong buffer. The non-multiview
  gamma draw is correct because it calls `applyTextureAttribute` explicitly. → Target shape: set the
  resolve texture on both state sets where the framebuffer is made (or apply it explicitly before
  `drawGeometry` in the multiview block, as the `resolved` block does). Better still, keep one state
  set per resolve and swap only the program, so the two can never diverge.

## Vulkan ordering and submission

### Contracts stated where the code no longer keeps them


- [ ] `components/rtxvulkan/vulkanrenderer.hpp:235-238` — **[code]** The reason given for declaring `mPresenter` after `mTarget` cites `VUID-vkDestroyImage-image-01000` for "a recording names it". That VUID concerns submitted commands, not executable recordings, and `Image`'s destructor buries through the graveyard whatever the member order. The order is harmless, but the stated reason is wrong. → Target shape: remove the reason, or state the real constraint if there is one.

## Settings and the SDL3 port

### The frame resolution does not survive a round trip through the settings

> Checked by the merge: index 0 falls to the `else` branch and writes the spin boxes, and `migrateUserSettings` fires on every file with `resolution x` and no `window width`.

- [ ] `apps/launcher/graphicspage.cpp:215` — **bug** The launcher cannot save Native. In
  `saveSettings`, `if (standardRadioButton->isChecked() && resolutionComboBox->currentIndex() > 0)`
  sends the Native item (index 0) to the `else` branch, which reads the custom spin boxes. In the
  Native case `loadSettings` never fills those boxes (it fills them only when `resIndex == -1`), so
  they hold the `.ui` minimum of 800 × 600. Steps: a user whose `resolution x/y` is `0 0` opens the
  launcher and closes it or presses Play. `saveSettings` writes `resolution x = 800`,
  `resolution y = 600`. If the user file already has `window width` (written once the window is
  dragged in Windowed mode), the migration does not step in, and the game then renders an 800 × 600
  frame scaled to the window. The picture comes out blurred, and nothing reports it. → Target shape:
  three explicit cases. Standard plus Native writes `0 0`, standard plus a listed mode writes that
  mode, and custom writes the spin boxes, e.g. `if (customRadioButton->isChecked()) {...} else if
  (currentIndex() > 0) {...}`, with cWidth/cHeight left at 0 otherwise. Add a test of
  load → save → load on a Native file.
- [ ] `components/settings/migration.cpp:9` — **bug** The rule that decides a file is upstream's
  ("states `resolution x` and no `window width`") also matches files this fork writes. The launcher
  never writes `window width`. The game writes it only from `WindowManager::windowResized`, and only
  in Windowed mode. So a user in Fullscreen or Windowed Fullscreen who picks a frame of 1920 × 1080,
  in the launcher or in the settings window, has the file saved with `resolution x = 1920` and no
  `window width`. The next `Settings::Manager::load`, in the launcher or in the game, moves 1920 into
  `window width` and resets the frame to Native. The user's choice is undone on every first restart,
  and it is quietly turned into a window size. → Target shape: decide by a marker only the fork
  writes, not by the absence of a key the fork may never write. For example, a `[Video] settings
  version` that the migration writes with its result, or write `window width/height` whenever the
  resolution is written (the launcher's `saveSettings` and `SettingsWindow::onResolutionAccept`).
  Add a test that a fork-written file with a frame resolution and no window size is left alone.

### SDL3 port: regressions against upstream's SDL2 behaviour

- [ ] `components/sdlutil/sdlcursormanager.cpp:89` — **bug** Upstream's fallback to the `arrow`
  cursor for a name the map does not hold (`b3a0227e44`, "Add Lua custom cursor registration") was
  lost in merge `b72c95e167`. `_setGUICursor` now does nothing for an unknown name. Cases that hit
  it: `ui.setCursorOverride` naming a cursor whose image failed to load (`createLuaCursor` logs and
  keeps the override), or a MyGUI pointer whose image is missing. The previous cursor stays (a
  resize arrow, say), or SDL's system cursor shows after `removeCursor` destroyed the current one,
  where upstream showed the game's arrow. → Target shape: restore
  `if (it == mCursorMap.end()) it = mCursorMap.find("arrow");`.
- [ ] `components/sdlutil/sdlvideowrapper.cpp:19` (`centerWindow`, called from `setVideoMode`) —
  **bug** SDL3's `SDL_SetWindowSize`, like `SDL_SetWindowFullscreen`, is asynchronous on X11 and
  Wayland: it records `pending.w/h` and returns (`SDL_video.c:3207`). `centerWindow` reads
  `SDL_GetWindowSize` straight after, so it centres the size the window had before: the fullscreen
  size when leaving fullscreen from the settings window, or the creation size at startup, where
  `openWindow` calls `SDL_SyncWindow` only after `setVideoMode` returns. The window ends up off
  centre. SDL2's X11 backend waited for the configure. → Target shape: position with
  `SDL_WINDOWPOS_CENTERED_DISPLAY(display)`, which SDL resolves against the applied size, or call
  `SDL_SyncWindow` before reading the size.
- [ ] `components/sdlutil/sdldisplay.cpp:30` — **bug** `displayResolutions` truncates
  `mode.w * pixel_density` with `static_cast<int>`. On a fractionally scaled Wayland output, SDL
  lists the desktop mode in rounded logical points with the density beside it (e.g. 2194 × 1.75 =
  3839.5), and SDL itself rounds that up (`SDL_ceilf`, `SDL_video.c:3319`). The launcher and the
  settings window then offer 3839 × 2159 for a 3840 × 2160 panel, and picking it gives a frame one
  pixel short, scaled by a non-integer factor. This is the same truncation AGENTS.md records as
  fixed for window sizes (`SDLUtil::windowPoints` rounds). → Target shape: round as SDL does
  (`std::ceil`), or `std::lround` to agree with `windowPoints`, and add a test with a fractional
  density.
- [ ] `apps/openmw/mwinput/bindingsmanager.cpp:368`, every `onControllerButtonEvent` — **bug**
  (behaviour change, not listed) The SDL2 code used `SDL_CONTROLLER_BUTTON_A/B/X/Y`, which SDL2
  maps by label on Nintendo pads by default (`SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS`). The port
  maps them to SDL3's positional `SOUTH/EAST/WEST/NORTH`. On a Switch Pro or Joy-Con, confirm moves
  from the button printed A to the one printed B, back moves the other way, and X/Y swap; a saved
  binding to "button 0" now means the other face button. The icons were made to follow the new
  positions (`controllermanager.cpp:519`), so the change was chosen, but AGENTS.md does not record
  it. → Target shape: either record it as part of the port's accepted behaviour, or keep upstream's
  label semantics by mapping through `SDL_GetGamepadButtonLabel` where a pad reports
  `SDL_GAMEPAD_BUTTON_LABEL_A/B` on east/south.
- [ ] `apps/openmw/mwgui/windowmanagerimp.cpp:1866` — **design** `createLuaCursor` now scales a
  script's cursor width, height and hotspot by `mCursorScale` (interface scale × shown scale).
  Upstream handed them to SDL as window pixels, unscaled, while scaling only the MyGUI pointers. A
  32 × 32 Lua cursor is now drawn at 64 window pixels under a scale of 2, which changes the Lua API
  for existing mods. The presentation needs the shown-scale part. The interface-scale part is a new
  rule that AGENTS.md does not name. → Target shape: scale Lua cursors by `shownScale()` alone (what
  upstream's pixels mean once the frame is scaled into the window), or record the new unit in
  `files/lua_api` and in AGENTS.md's SDL3/presentation entry.

## Crash reports, CI and the release

### Crashes the catcher never reports, or reports and then loses

> The line numbers of the macOS item refer to the hang path through `crashpadclientposix.cpp:20-28` and `crashpadclient.cpp:54-59` (`CRASHPAD_SIMULATE_CRASH` inside the `SIGUSR2` handler).

- [ ] `components/crashcatcher/crashpadclientwin32.cpp:414` / `crashpadclient.cpp:123` — **[bug]** On Windows,
  fail-fast crashes get no report at all. That covers `/GS` cookie failures (`STATUS_STACK_BUFFER_OVERRUN`),
  heap corruption the NT heap catches (`STATUS_HEAP_CORRUPTION`), and every `__fastfail` (MSVC STL hardening,
  CFG). They skip the in-process unhandled-exception filter, and the catcher's hooks only run from that filter.
  Crashpad catches them only through its WER runtime-exception module (`crashpad_wer.dll`, registered with
  `CrashpadClient::RegisterWerModule`). The fetched Crashpad builds that module (`handler/CMakeLists.txt:132`),
  but nothing links, ships or registers it. Result: the game vanishes, `HandlerMain` returns with `mDumps`
  empty, `writeSessionPackage` writes nothing (`dumps.empty()`), and `tellPlayer` is skipped
  (`crashpadmonitor.cpp:598`). The log gets no line and the player gets no box. `describeException`
  (`crashpadmonitorwin32.cpp:271-272`) names these two codes as though they could arrive, and they never do. On
  Linux every one of these is a signal Crashpad catches, so Windows alone loses them. → Target shape: build
  `crashpad_wer`, install it beside the executable, and call `sClient.RegisterWerModule(<dir>/crashpad_wer.dll)`
  after `StartHandler`. Also have the monitor say what an unreported end was: it holds the game's handle
  (`GameProcess::mHold`), so `GetExitCodeProcess` after `HandlerMain` returns can log "the game ended with
  0xC0000409 and no dump was written" when no dump came. That keeps a cause even where WER is turned off.

- [ ] `components/crashcatcher/crashpadclientposix.cpp:248-256, 285-295` — **[bug]** On macOS the hang
  report runs code that allocates, inside a signal handler, on a thread the kernel picks at random.
  `requestHangReport` sends a process-directed `SIGUSR2` (`kill`, `crashpadmonitorposix.cpp:103`), and macOS
  hands that to any thread that does not block it. `onHangSignal` → `reportHang` → `CRASHPAD_SIMULATE_CRASH` →
  `crashpad::SimulateCrash`, which builds an `ExceptionPorts::ExceptionHandlerVector` (a `std::vector`) and
  talks Mach IPC (`client/simulate_crash_mac.cc`). If the interrupted thread holds a libmalloc zone lock, the
  handler deadlocks on it. The game is then hung for good, the report the monitor asked for never comes, and
  only the dialog's End gets the player out. Linux's `DumpWithoutCrash` is built for signal context
  (`crashpad_client_linux.cc:792`), but it still has to dump from inside a signal frame. The comment at
  `:287-290` makes that frame's stack size a concern. → Target shape: the handler only signals. It writes a byte
  to a pipe, or calls `semaphore_signal` on macOS or `sem_post` on Linux (both async-signal-safe). A reporter
  thread started in `install` waits on it and calls `reportHang()` in ordinary context. The dump still covers
  every thread, and `markOf(Hang)` already leaves the receiving thread unmarked, so the summary loses nothing.
  Windows already has this shape (`hangEntry` on a thread of its own).

- [ ] `components/crashcatcher/crashpadclient.cpp:106-125`, `crashpadmonitor.cpp:583-599` — **[bug, check on
  the AppImage]** The monitor runs from the AppImage's FUSE mount, but it does its post-game work after the
  game is gone, which can be after the mount is gone. It is `Platform::Process::executable()`, i.e.
  `/proc/self/exe` inside `/tmp/.mount_*`, and the packaging and the player's dialog run only once the game has
  exited, by design. Crashpad's spawn closes every inherited descriptor but stdio and its socket
  (`util/posix/spawn_subprocess.cc:189`, `CloseMultipleNowOrOnExec`). So the monitor holds nothing that keeps
  the type-2 runtime's mount alive. When the last process holding the runtime's keepalive exits (the game,
  since the launcher quits after "Play"), the mount goes away. Any page of the monitor's own code or of a
  bundled library (SDL's message box, zlib for the package) that is not yet resident then faults with `SIGBUS`
  or `ENOTCONN`. The minidump is on disk by then, but the package and the dialog, which are what a player
  sends, are lost on the one Linux build players run. The cost is in this tree's code; the unmount is the
  runtime's documented lifetime, and no test runs the catcher under the AppImage. → Target shape: verify by
  crashing `openmw-*.AppImage` with `OPENMW_CRASH_DIALOG=1`. If confirmed, either keep the mount for the
  monitor's lifetime (an AppRun hook that waits on the monitor, or hand the runtime's keepalive descriptor
  through Crashpad's `preserve_fd`), or have the monitor copy itself out of the mount before exec. The first
  needs no change to the catcher.

### The upstream-merge agent and the release are not contained by what their comments say

- [ ] `.github/workflows/upstream.yml:25-31, 76-83, 296` — **[bug: security]** The header says "the worst a
  run can do is land a change that builds and passes CI". The allowlist does not hold that. `Bash(git:*)`
  admits any command that starts with `git`, including `git -c alias.x='!sh -c …' x`, `git -c
  core.sshCommand=…`, `git -c core.pager=…` and `git -c core.hooksPath=…`. `Write` can write
  `.git/hooks/post-commit`, which the prompt's own `git commit` then runs. So the agent can run arbitrary
  commands, and the text it reads is upstream's, which is untrusted by the file's own account. Those commands
  inherit the agent's environment, with the maintainer's `CLAUDE_CODE_OAUTH_TOKEN` in it. They also see the
  job's `contents: write` token, which `persist-credentials` (left on, `:80-83`) writes into `.git/config`, a
  file the `Read` tool can read. That is exfiltration of a personal subscription token, plus pushes to every
  ref no ruleset guards, from one injected commit message. → Target shape: the agent job has no write
  credential (`persist-credentials: false`, `contents: read`) and produces the resolved merge as an artifact
  (a `git bundle`). A second job with no agent and no untrusted reading pushes the branch and opens the pull
  request. Inside the agent job, narrow git to the subcommands it needs (`Bash(git diff:*)`, `Bash(git
  add:*)`, `Bash(git commit --no-edit:*)`, …), refuse `-c`, and set `core.hooksPath=/dev/null` in the system
  config before the agent starts.

- [ ] `.github/workflows/rtx-release.yml:6-8, 74-79` — **[bug]** Nothing checks the header's claim that
  "`ci.yml` has already built and tested the commit the tag names". The workflow runs on any `rtx-v*` tag, on
  any commit, whether or not CI ever ran there or passed. The `package` job builds `omw archive` and runs no
  tests. `sanitizers` runs Linux only. So a tag on a commit whose Windows leg fails, or on a commit never pushed
  to master, still produces a draft release with a Windows archive nobody tested. → Target shape: the release
  calls `ci.yml` as a reusable workflow on the tagged commit, as it already does `sanitizers.yml`, and lists it
  in `needs`. Alternatively, a first step refuses unless the tag's commit is reachable from `origin/master` and
  every required check run for that SHA succeeded (`gh api repos/$GH_REPO/commits/$SHA/check-runs`).

### Session packages accumulate without bound

- [ ] `components/crashcatcher/crashpadmonitor.cpp:595`, `crashpackage.cpp:319-363` — **[design]** Every
  session with at least one dump writes a new `<app>-crash-<time>.zip` into the reports folder: a
  `Crash::report` that the game survived, or a hang that recovered with the player choosing Wait. Nothing ever
  deletes these. Each package copies dumps that Crashpad's database already keeps and prunes itself (its
  default prune condition), plus the whole log. A non-crash session's package is named "crash" and the player
  is never told about it (`tellPlayer` runs only when `crashed || mEnded`). Harness runs and players who hit
  recurring reports grow `crashes/` forever. → Target shape: write the package only for the sessions the
  player is told about (crashed or ended), or prune the folder's packages by count or size when writing a new
  one, as the database prunes its own.

### Platform abstractions: one spelling that is not one word, test-only code in the library

- [ ] `components/platform/processwin32.cpp:121-129` — **[bug]** `shellWord` claims a word that "means the
  same on either" shell, but on Windows it does not. `cmd` expands `%NAME%` inside double quotes, so a path
  with `%…%` (legal on Windows) is a different path once `runShell` hands it to `cmd /c`. A `"` inside the text
  also ends the word, though no path contains one. The caller is `apps/rtxtool/film.cpp:700`, which quotes the
  frame and output paths for the encoder command. → Target shape: double each `%` as `^%` (or `%%` in a batch
  context) and refuse a `"`. Better still, give `runShell` an argument vector and spawn the encoder with
  `CreateProcessW` and `posix_spawnp`, with no shell at all.

- [ ] `components/platform/memory.hpp`, `memoryposix.cpp:7-12`, `process.hpp:194` — **[code]** Two pieces of
  the production `components` library exist only for tests. `Platform::Memory::allocateAligned` and
  `freeAligned` are called only by `apps/components_tests/rtx/support/allocations.cpp`, and
  `Process::disableCoreDump` only by `apps/components_tests/rtx/support/death.hpp`. On top of that,
  `allocateAligned`'s rounding `(size + alignment - 1) / alignment * alignment` wraps for a size within
  `alignment` of `SIZE_MAX`, and then returns a small block where the contract (and `aligned_alloc`) owes null.
  → Target shape: move both into the test support library that uses them. If they stay, check
  `size > SIZE_MAX - alignment` and return null.

### CI hygiene

- [ ] `CI/before_install.macos.sh:14`, `CI/before_script.macos.sh:88-91` — **[code]** The macOS leg compiles
  every shader with whatever `shaderc` and `spirv-tools` Homebrew has that day. Every other build takes them
  from the SDK `tools/omw/pins.py` pins. A Homebrew release can break the leg, or let it pass a module that
  `spirvpin` would refuse with the pinned tools, with no change to the tree. Also, the headers are installed at
  `$DEPS_DIR/vulkan` while `before_script` spells the same path again as `/tmp/vulkan/include`. → Target shape:
  take glslc and the SPIR-V tools from LunarG's macOS SDK at the pinned version (or pin the Homebrew formula
  versions), and derive the include path from the one variable.

- [ ] `.github/workflows/rtx-release.yml:62, 69` — **[bug]** For a run started by hand, the release names its
  archives and its artifact after `github.ref_name`. A branch name with `/` (for example `vsg/230-…`, the form
  upstream's branches take) is refused by `upload-artifact`, whose names may not contain `/`. That happens at
  the end of a three-hour build, and `omw archive` has already made a file name with a slash in it. → Target
  shape: sanitise the name once (`${GITHUB_REF_NAME//\//-}`) into an env variable both steps read.

## Measurements that can mislead

The harness reports a result that its data does not support.

### The gate's verdict

- [ ] `tools/omw/gate.py:44-56` — **[design]** On a flavour whose preset builds no tests (`release`), the gate prints "tests: … has none" and carries on through `check` and `repeat` to `gate: clean`, exit 0. `omw release gate` therefore passes a change no test ran against, under the same final word as a full gate. → Target shape: refuse `gate` on a flavour with no tests (`Refusal`, pointing at `omw debug gate`), or end it with a status that is not "clean". The rule is the gate's, not a printed aside.
- [ ] `tools/omw/gate.py:28-56`, `tools/omw/main.py:27-28`, `AGENTS.md` (Verification) — **[code]** The gate runs `spellings.check()` after the format check and `testing.timing()` after the tests. AGENTS.md ("format check, the driver's tests, build, the listing check, the release compile, tests, `check`, one repeat pair") and USAGE both omit those two steps. → Target shape: one statement of the order, matching the code: the module docstring, with USAGE and AGENTS.md naming both steps.

### The hashes reader accepts what it says it refuses

- [ ] `apps/rtxtool/instruments/framehashes.cpp:226-237, 265` (`FrameHashes::read`) — **[code]** `std::from_chars(from, from + 16, into[half], 16)` is accepted on `ec` alone. A half such as `01234567zzzzzzzz` parses as 0x01234567, and a frame field `12x` as 12. "Every line or none" and "a truncated reference is a failure" therefore hold only for cells with no digits at all. The damage stays visible: a corrupted reference reads as a moved frame, not a pass. It is still the boundary parse the file's own contract promises. → Target shape: require `ptr == end` for every number. `model/wholenumber.hpp` already has that rule for base 10; give it a base parameter and use it here.

### The driver's refusals are narrower than what it fixes

- [ ] `tools/omw/repeat.py:44, 59-65` — **[code]** `WALK_SWITCHES` refuses only `--views/--suite/--seconds/--frames`. `repeat` also fixes `--window`, `--upscale`, `--filter`, `--validation`, `--hashes`, `--against` and `--hold` on every run. Any of those on the line is named twice, Boost rejects the repeat as `multiple_occurrences`, and the user sees "the run itself failed" plus a log tail instead of the refusal the walk switches get. → Target shape: refuse every switch `repeat` sets itself (derive the list from `bench`'s own list, so the two cannot drift), with the same message.
- [ ] `tools/omw/noise.py:176-177` — **[code]** `folder.with_suffix(".log")` on a leg folder named from a decimal distance (`--walk=1.5` gives `walk1.5`) yields `walk1.log`: `with_suffix` replaces `.5`. The log then no longer names its leg's distance. → Target shape: `folder.parent / (folder.name + ".log")`.

### A frozen walk no longer counts what the reuse check measures

- [ ] `components/rtx/mirror/sceneextractor.cpp:971-972` — **[design]** A frozen root adds only
  `mInstances` to the walk's stats. `mMeshesReused` and `mMaterialsReused` stop counting everything
  that froze. The harness's `WalkTwice` check (`apps/rtxtool/stopwriter.cpp:585-590`) asserts
  `mMeshesReused > 0` on a second walk over the same graph. That second walk now mostly runs
  `passFrozen` and no longer exercises the identity maps for any static. At a stop where every root
  froze, the check fails even though nothing is wrong. → Target shape: `FrozenRun` keeps the counts of
  meshes and materials its walk resolved and adds them back as reused when the run is passed, or the
  stats name a `mFrozenPassed` count that the check reads beside the reuse counts.

## Performance

Work done twice, at the wrong time, or for nothing.

### Sprite light bake is cubic in a texture's side

- [ ] `components/rtxvulkan/shaders/texture/spritelight.comp:44-73` (`main`, `across`), dispatched for
  every level by `texture/spritelightpass.cpp:24`. **[perf]**
  - **The cost.** Each texel walks its whole row and column. Each step is a `texelFetch` and a
    `pow`. Level 0 alone is `W·H·(W+H-2)` fetches and `pow`s:
    - 64²: 0.5 M (vanilla, which the comment sizes for: "a few thousand texels").
    - 512²: 268 M.
    - 1024²: 2.1 G, for one mist or smoke sheet from a PBR replacer.
  - **Why it matters.** The project serves those replacers. The bake runs on texture arrival
    while the game runs, so that work lands in a frame. AGENTS.md: "a spike taken at load is a
    spike a player feels".
  - **The reason given for not scanning does not hold.** The comment rejects a scan because it
    "would sum in another order". A sequential suffix or prefix product per row (and per column)
    does the same multiplies in the same order:
    - Start at 1.0, store, then multiply by `across(at)` from the far edge inward.
    - That produces exactly the bits the per-texel loop produces, and matches the host's
      `Rtx::SpriteLightMap` to the last multiply.
    - `pow(1 - a, 1/N)` is also recomputed `W+H` times per texel, from the same input each time.
  - → **Target shape:**
    - First compute `across` once per texel.
    - Then run one invocation per row for `fromRight`/`fromLeft` and one per column for
      `fromBelow`/`fromAbove`, each a sequential running product in the current order.
    - That is `O(W·H)` work and `pow` once per texel, with bit-identical output.

### The world's chain keeps the bounce reuse's reservoirs for a mode that is off by default

- [ ] `components/rtxvulkan/trace/tracechain.cpp:190-191`, `trace/bouncereservoirs.cpp:360-386`,
  `vulkanrenderer.cpp:83` — **[perf]** The world's chain is built with `reuses = true`, so every
  `resize` (and every `setIndirect` back to traced) allocates full-extent reservoirs: 32 B
  reservoirs + 32 B history + 2 × 28 B origins + 4 B through + 4 B paired, which is 128 B a traced
  pixel. Since `9c069bd9dc` the reuse is `Off` by default, and the trace's writes are gated on
  `mBounceReuse != OFF` (`visibility.rgen:501`, `visibilityhit.rchit:133`). So this memory is
  allocated and never touched. At 1920×1080 quality (1280×720 traced) it is 118 MB. On this
  machine's 7680×2160 at quality (5120×1440 traced) it is 944 MB. At native 4K it is 1.06 GB. That
  is device memory taken from what textures and structures are budgeted against (`MemoryUse`).
  `architecture.md` §8 states the decision ("the world's chain keeps its reservoirs whatever a frame
  runs"), but nothing in the code needs them kept: a picture's chain already binds 1-pixel
  stand-ins, and `TraceChain::record` already re-derives what it keeps per frame through
  `setIndirect`.
  → Target shape: treat the reuse mode the way the indirect light is treated. The chain makes the
  full-extent reservoirs when a frame first asks for a reuse mode other than `Off` (a harness A/B),
  and holds the 1-pixel ones otherwise. `BounceReservoirs::resize(…, reuses)` already has both
  shapes, and making them anew is already a reset.

### Moving a frozen reference root forces a full sweep on every frame it moves

- [ ] `components/rtx/mirror/mirroridentity.hpp:131-145` (`Kept::drop`), `sceneextractor.cpp:953-974`
  (`passFrozen`), `:985-1007` (`endFrozen`) — **[perf]** When a frozen root's `FrozenFace` changes,
  `passFrozen` thaws it *before* the walk stamps its entries again. Each `drop` then meets an entry from
  the old epoch and sets `mAbandoned`, so `whole()` is false for `mPlacements`, `mMeshes` and
  `mMaterials`, and `retire()` walks every entry of all three on that frame. `endFrozen` then freezes
  the root again at its new place, so the next frame does it all again. Concrete cases: a door opening,
  which `World` rotates step by step and does not report as a jump; and any object without
  controllers that a script moves (`SetPos`, `Rotate`, `Move`). For the whole motion, each frame pays
  a sweep of tens of thousands of entries plus a thaw, a record and a run allocated and released.
  That is a per-frame cost that only some frames pay. The flag is not needed in `drop`: after the
  drop the entry is unheld and unreached, so `mReached + mHeld < size` unless the walk stamps it again,
  and the counts already give the right answer. → Target shape: `drop` adjusts the counts only and
  leaves `mAbandoned` alone (`abandon` keeps it). Do not freeze a root whose face changed on this walk
  until a later walk finds it standing still.

### Performance: picture uploads through `osg::Image::getColor`

- [ ] `components/myguirtx/slottexture.cpp:134-148` — **[perf]** Every image that is not packed RGBA8 is
  converted one pixel at a time: a virtual `getColor` call, a `Vec4f`, and four clamps and roundings per
  pixel. The global map's base is `GL_RGB`/`GL_UNSIGNED_BYTE` (`apps/openmw/mwrender/globalmap.cpp:80`), sized
  `18 px × cells` on each side. Vanilla that is about 0.6 Mpx; with Tamriel Rebuilt it is tens of megapixels,
  all on the main thread whenever the map is first drawn or its base is rebuilt (`SharedTexture::refresh`).
  The GL backend uploads the same bytes directly. Save thumbnails (RGB) and every UI texture loaded from a
  compressed DDS (`Texture::loadFromFile`) take the same path. → Target shape: direct byte loops for the
  formats the game actually supplies (`GL_RGB`, `GL_LUMINANCE`, `GL_LUMINANCE_ALPHA`, `GL_ALPHA`, `GL_BGRA` at
  `GL_UNSIGNED_BYTE`), reading rows by `getRowStepInBytes()` so RGB row padding is honoured, the same shape as
  `Texture::widen`. Keep `getColor` only for the rest, and decode DXT through a block decoder rather than per
  texel.

### The sea is synthesised once per trace, not once per water time

- [ ] `components/rtxvulkan/trace/tracechain.cpp:263-268`, `trace/wavepass.cpp:230-290` — **[perf]**
  `TraceChain::record` records `WavePass::record` for every trace with `mSea`. That includes each
  picture of the world (`PictureTracer::trace` → `mChain.record`), and every one of them gets the
  same `mWaterTime` from the frame's `WorldReading` (`frameworld.cpp:206`). Per `ViewQueue::draw`,
  a cell crossing traces a row of three map tiles and a fresh load traces up to nine. In an exterior
  with water, each of those re-synthesises all cascades: rows, columns and the mip chains, which
  the comment at `tracechain.cpp:262` puts at a fifth of a millisecond. The frame then synthesises
  the same tiles again. On a crossing that is about 0.6 ms of duplicate device work landing on the
  frame that already carries the arrival, which goes against "compute nothing twice".
  → Target shape: `WavePass` keeps the seconds it last synthesised and records nothing when asked
  for the same seconds. Once the queue order makes those tiles visible to every later trace, they
  are the answer. Alternatively the renderer synthesises once per frame before the views and the
  trace, and the chain only reads the result.

### `noise`: work every run pays for nothing

- [ ] `apps/rtxtool/main.cpp:1027-1046` (`picture`, through `measureFrames`) — **[perf]** Every stop warms `sHistoryFrames` = 128 frames, which is four times the accumulator's length. The bar and its 32 limit draws are traced unfiltered (`side.unfiltered()`: no denoiser, no reuse, upscaler off) under a held exposure. They read no accumulator history, so the 128 frames are sized for a history they do not have. Per place and side that is 33 stops × 128 = 4224 warm-up frames out of about 9 400, or 45 % of the run. The reference is excluded: it measures its exposure and needs the adaptation time. What an unfiltered, held-exposure frame does carry over frames is the air, which decays by 0.9 a frame (`run.hpp:88`). Reaching the accumulator's own 1.7 % residue takes 0.9ⁿ ≤ 0.017, so n = 39. → Target shape: an unfiltered held-exposure stop warms for a count derived from the air's decay, not from `ACCUMULATE_FRAMES`, beside the filtered stops' `sHistoryFrames`. Expect about 30 % off every `noise` run. Confirm with one place that the bar and limit pictures do not move.
- [ ] `apps/rtxtool/main.cpp:1012` (`ownBar`) — **[perf]** A single flag decides whether the other side traces its own reference and its own bar, by `versus->unfiltered() != played.unfiltered()`. `referenceOf` forces `mJitter = true`, `mNoise = WhiteHash` and `mLevelEpsilon = 0`. So for `--ab=jitter`, `--ab=noise=…` and `--ab=level-epsilon=…` the two references are the same request: the bar differs and the reference does not. The second side still retraces the 384-frame reference and re-measures an exposure that comes out the same. → Target shape: two tests, `ownReference = referenceOf(*versus) != referenceOf(played)` and `ownBar` as now. `NoiseSide` names its reference separately from its bar.

### The pane and glossy filters gather history for pixels whose answer they throw away

- [ ] `components/rtxvulkan/shaders/trace/denoise/pane.comp:184-208,216-217` and
  `trace/denoise/specular.comp:138-155,167` **[perf]**
  - **The waste in `pane.comp`.** It runs on every denoised frame (`denoisepasses.cpp:190`). For
    every pixel with no layer (`stands` false, most of most frames) it still:
    - loads the pane motion;
    - runs the four-tap `heldBefore` gather;
    - and only then selects `vec4(sampled, 0.0)` / `vec4(0.0)`.
    The gather cannot match anything, because `heldSurfaceMatches` with a zero normal is false.
  - **The waste in `specular.comp`.** On a mapped scene every pixel with no lobe (`sampled.a < 0`)
    loads four held surfaces. Each matches, so it also loads four means, and all eight are
    discarded by `reflects`.
  - **Why a branch is cheap here.** Layer and lobe coverage are spatially coherent, so a branch on
    `stands` / `reflects` is warp-uniform almost everywhere.
  - **What the tree's rule asks.** "One path through a shader" requires a measurement before a
    branch. The loads saved are provably dead: up to four 8-byte history taps plus a motion fetch
    per pixel, in a pass that is all memory traffic.
  - → **Target shape:**
    - Gate the gather on the result being kept: `if (frame.mReset == 0u && stands)` in `pane.comp`,
      and `if (previous && reflects)` in `specular.comp`.
    - The outputs are unchanged bit for bit.
    - Measure the `pane` zone before and after, as the rule asks.

### Every bottom level pays for no-duplicate any-hit, including meshes no ray ever any-hits

- [ ] `components/rtxvulkan/scene/structurebuild.cpp:20-23` — **[perf]** `describeTriangles` sets
  `VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR` on every geometry, static and deforming. Its
  stated reason is `candidateStops` summing a see-through surface's reports, which applies only to
  instances `placeRow` marks `FORCE_NO_OPAQUE` (cutout, translucent, additive,
  `sceneacceleration.cpp:640-641`). An opaque instance never reaches an any-hit shader or a
  candidate, so the bit buys it nothing. The bit exists because a builder may split primitives,
  which is what produces duplicate reports. Requiring no duplicates takes that freedom away from
  every structure in a cell built with `PREFER_FAST_TRACE`, most of which are walls and ground only
  ever traced opaque. What this costs in trace time is the driver's to say, and the code does not
  measure it.
  → Target shape: measure first (`./omw release bench`, with the bit only where needed against the
  bit everywhere). If it pays, set the bit only on meshes that some non-opaque material wears at
  arrival, and rebuild the structure in the one case where `setMaterial` makes a placement
  non-opaque on a mesh built without the bit.

### Changeable marks that the walk does not need

- [ ] `components/rtx/mirror/sceneextractor.cpp:356-359` — **[perf]** `NodeKind::Lod` marks a
  reference root changeable, so it is never frozen. The comment says "a level of detail … which the eye
  or the clock turns". But the mirror answers every LOD with `nearestLevel` whatever the eye
  (`worlddescent.hpp:94-104`), so the child it walks never changes. Every reference that carries an
  `NiLODNode` is walked in full every frame for nothing. → Target shape: take `Lod` out of the
  changeable test, and keep the switch, the sequence and the billboard, which do change.

### Rasterizer local-map tiles no longer release their render targets

- [ ] `apps/openmw/mwrender/localmap.cpp:166` — **[perf]** Upstream built a
  `LocalMapRenderToTexture` per request and took it out of the graph once drawn
  (`LocalMap::cleanupCameras`). The colour texture survived; the RTT camera, the FBO and the
  `GL_DEPTH24_STENCIL8` buffer were freed. The seam's `OffscreenView` is now kept per segment
  (`MapSegment::mView`). Under GL, `GlTileView` keeps the RTT node, camera, FBO and depth buffer
  for every mapped segment for as long as the segment lives, masked off but resident. That is
  `res² × 4` bytes of depth per tile (256 KiB at 256 px, 1 MiB at 512 px with a raster scale of
  2) times every exterior segment in the map grid plus every interior segment.
  → Target shape: `GlTileView` drops its camera's attachments (or the RTT node's per-view data)
  once the draw has been made, and builds them again on `redraw`. The texture the widget shows
  stays.

## Design: data, ownership and dependencies

### The scene graph's state reading lives in `scene/`

- [ ] `components/rtx/scene/surface.hpp:152,272,441` / `surface.cpp` — **[design]** `describeStateSet`,
  `SurfaceLocks`, `sTextureRoleNames`, `TextureRole` and the 60-case `readAttribute` table read `osg::StateSet`,
  `SceneUtil::Material` and `SceneUtil::TextureType`. That is the walk's job. Every caller is in `mirror/`
  (`shading`, `materialresolver`, `emitterresolver`, `cells/prepared`), and the architecture table puts "the walk
  from the scene graph" there. `scene/` is meant to hold the description and its rows. As it is, every user of
  `material.hpp` pulls in `<osg/StateAttribute>`, `<osg/CopyOp>` and the reader through `surface.hpp`.
  → Target: keep the vocabulary the description needs in `scene/` (`AlphaMode`, `AlphaTest`, `BlendKind`,
  `VertexColour`, `additiveSurface`, `meanUnder`). Move `SurfaceDescription`, `SurfaceLocks`, `TextureRole`,
  `UnreadState` and `describeStateSet` into `mirror/`, next to `shading.hpp`.

### Sibling tables and readers that answer the same question differently

- [ ] `components/rtx/scene/placementtable.hpp:173` with `scene/mesh.hpp:135` — **[design]** Whether a placement
  slot is standing has two sources. One is `MeshInstance::isPlaced()` (`mMesh != sNoIndex`), which `recordOf`,
  `moveRecord` and `forEachPlacement` read. The other is `SlotRows`' free list, which `take` and `free` maintain.
  `drop` keeps them in step by hand: `row = PlacementRow{}` followed by `mRows.free`. The table also uses `SlotRows`,
  whose per-slot hold counts it never takes, so it carries `mHolds` and asserts on counts that are always zero.
  → Target: read liveness from one place. Either readers ask `mRows.isLive(slot)` and `isPlaced` is removed, or
  placements use a `SlotPool` with plain rows and no hold column.
- [ ] `components/rtx/scene/materialtable.hpp:58-59` — **[code]** On `MeshTable`, `TextureTable` and
  `DeformerTable`, `getArrived()` returns the slots that arrived. On `MaterialTable` it returns `ArrivedRuns` (layer
  and mask runs), and its slot arrivals are `getWritten()`. The same name means two different things across sibling
  tables that a backend reads side by side.
  → Target: `getArrivedRuns()` for the runs, keeping `getWritten()`, or the other way round, so `getArrived` means
  slots everywhere.
- [ ] `components/rtx/environment/moonbuilder.cpp:143`, `skybuilder.hpp:102`, `nightsky.hpp:76`,
  `moonbuilder.hpp:158` — **[code]** The three sky readers take their textures in two ways. Sheets and stars call
  `checkUploadable`, then `takeTexture`, and are refused before taking a slot. Moons call `textures().add(path,
  image-or-null)` and then `holdTexture`, take a slot for an unreadable face, and leave it to the upload to refuse.
  Their parameters are also in different orders: `(scene, scenes, facts, holds, …)` in
  `addCloudSheet`/`readNightSky`/`addSkyContent` against `(scene, images, sizes, holds, facts)` in `addMoonFaces`.
  → Target: one helper that opens, checks and takes a held sky texture, used by all three, with one argument order.

### One rule stated twice: which texel fact a blended surface needs

- [ ] `components/rtx/mirror/materialresolver.cpp:226-233` vs `:501-518` — **[code]** `read` (the
  ring's thread) chooses between the mean and the solid reach with
  `additiveSurface(mAlphaMode, mBlend)` on the description. `describe` (the frame) chooses with
  `material.isBlended()` and `isAdditive()` on the material. Only a debug assert keeps the two in
  agreement. If they ever disagree, a release build dereferences an empty `std::optional`
  (`*read.mMean` / `*read.mReachesSolid`). → Target shape: one function over `SurfaceDescription` that
  names the fact wanted, called by both, and the assert goes.

### The content cache's key machinery is dead, and its docs say otherwise

- [ ] `components/rtx/preprocess/contentpreprocessor.hpp:19-23`, `contentcache.hpp:295-305`,
  `contentpreprocessor.cpp:80-84`, `docs/rtx/architecture.md` §7 — **[code]** The class doc and
  architecture.md say every pass "is keyed on everything it reads and asked of `ContentCache` first".
  With `sHolds = false`, no key is ever made. `ContentDigest`, every pass's `digest`, the image
  `FinestTexels` holds between the digest and the run, and `PassStats::mHits`, `mKeyMs` and
  `mKeyBytes` are dead in every build, and the reports print three columns that are always zero.
  `ContentCache`'s own comment contradicts itself: "its key … which is made on every ask already",
  three lines above `sHolds`' "no key is made". → Target shape: either fix the comments to say plainly
  that the key machinery is kept unused for the store to come, and drop the always-zero columns from
  the reports, or remove the machinery until the store lands.

### The walk survives a throw, except for its frozen recording

- [ ] `components/rtx/mirror/sceneextractor.cpp:976-983`, `:644-661`, `:275-314` — **[code]** The walk
  is written to survive a throw: `WalkGuard` resets the pass, `Traversal::begin` resets the class,
  glow and jump state "a walk that threw" left, and `walk` clears `mGlows` and the pending emitters.
  None of them resets `mRecording` or `mRecordedChangeable`. If a world walk throws inside a
  reference root, `mRecording` stays true, and the first `recordFrozen` of the next walk fails its
  assert (`"a reference root recorded inside another"`). → Target shape: `WalkGuard` (or `begin`)
  clears the record state along with everything else it resets.

### A lifetime counter reported as this frame's refusal

- [ ] `components/rtx/scene/scenetextures.cpp:126` — **[code]** `TextureTable::getRefused()` counts refusals over the
  table's lifetime. After the array has overflowed once, every later `describe` (every arrival frame for the rest of
  the session) pushes a "past the N textures the array holds" refusal and builds its string with `std::to_string`,
  even when nothing was refused in that call. `Refusals` deduplicates, so the log stays quiet, but each arrival frame
  allocates, and the refusal is attributed to a describe that refused nothing.
  → Target: have the table record how many it refused since the last `clearArrivals` (a per-hand-over counter beside
  `mChanges`), and report only when that is non-zero.

### Smaller items in the shared headers and the pipeline

- [ ] `components/rtx/shaders/visibility.h:615` (`sunSource`) and `sky.h:297` (`moonSource`) — **[code]**
  Both are `SkySource{ direction, irradiance, std::sin(angle) }`: one rule spelled twice.
  → Target shape: one `skySource(direction, irradiance, angularRadius)`. The sun's caller passes
  `SUN_SHADOW_RADIUS`.
- [ ] `components/rtx/shaders/sky.h:299` — **[code]** It names `std::sin` without including
  `<cmath>`, relying on `hosttypes.h`. `visibility.h` includes `<cmath>` for the same call. Per
  "include what you name", either include it or spell `sin` through `hosttypes.h`'s using
  declaration, which exists for exactly this.
- [ ] `components/rtxvulkan/spirv/spirvfile.cpp:14` — **[code]** It defines its own
  `sSpirvMagic = 0x07230203` beside `spirvpin.cpp:488`'s `spv::MagicNumber`, in the same library,
  which already has the SPIR-V headers on its include path. → Target shape: use
  `spv::MagicNumber`, and drop the blank line inside the anonymous namespace.

## Duplication, dead code and stale narration

### The same history rule spelled out in five kernels

- [ ] `trace/denoise/accumulate.comp:109-115`, `trace/denoise/shadowtiles.comp:294-300` and
  `trace/denoise/specular.comp:101-107` **[code]**
  - **The duplication.** Each defines an identical `sameSurface(ivec2, vec3, float, vec3)`:
    `outsideOf` plus `heldSurfaceMatches` against its own `heldSurface` binding.
  - **The loops around it.** The bilinear "four taps, keep the ones that are this surface, divide
    by the kept weight" loop is written out four times: `accumulate.comp:162-179`,
    `shadowtiles.comp:450-463`, `specular.comp:140-154`, `pane.comp:192-207`.
    `bouncetemporal.comp:155-172` has a nearest-tap variant.
  - **Why it matters now.** The jitter fix above touches every one of them. A sixth filter would
    write the loop a sixth time.
  - → **Target shape:** one lib function that returns the four matched bilinear shares (a `vec4`, nought
    for a refused tap) from the footprint and the four held texels the caller loaded. Each kernel
    then weights its own payload by it.
- [ ] `components/rtxvulkan/shaders/trace/denoise/pane.comp:179,217` **[code]**
  - **The defect.** It re-spells `heldSurfaceOf` (`surfacematch.glsl:20`) as `stands ? vec4(normal,
    seen.y * frame.mDistanceScale) : vec4(0.0)`. That is the same texel, but the shared function
    says it is "one statement" for every history that keeps a surface.
  - → **Target shape:** `heldSurfaceOf(seen, frame.mDistanceScale)`.
- [ ] `trace/denoise/atrous.comp:147`, `trace/denoise/shadowfilter.comp:105` (`filteredVariance`) and
  `trace/fogintegrate.comp:122-127` (`fogTentWeight`) **[code]**
  - **The duplication.** The 3×3 `[1 2 1]²` tent is written three times, two of them as the same
    `(x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25)` expression.
  - → **Target shape:** one `tentWeight(ivec2 offset)` in `lib/`, normalised or not as one parameter.

### The wavelet's first level claims a saving it does not make

- [ ] `components/rtxvulkan/shaders/trace/denoise/atrous.comp:197-203` **[code]**
  - **The mismatch.** The comment says the history fix "does not pay the nine loads of the
    variance's prefilter". But `const float noise = ATROUS_WIDE ? varianceAround(at) : centre.a;`
    calls the prefilter at every pixel of the wide level, fixing ones included.
  - **Why the compiler cannot drop it.** `noise` feeds `spread`, which is computed before the
    `fixing ? 1.0 : exp(...)` select, so the 18 loads (9 surface, 9 source) are paid.
  - → **Target shape:** either `ATROUS_WIDE && !fixing ? varianceAround(at) : centre.a` (which makes
    the comment true, at a branch the disoccluded pixels take together), or drop the claim.

### Small inconsistencies in the kernels

- [ ] `components/rtxvulkan/shaders/trace/bouncetemporal.comp:159` **[code]**
  - **The defect.** `outsideOf(uvec2(tap), ...)` converts a tap that can be negative to unsigned.
    It still lands outside through wrap-around, so the result is correct. But `pixels.glsl` has
    the `ivec2` overload for exactly this case, and its header says the one spelling exists so a
    reader need not check that a fourth form means the first.
  - → **Target shape:** `outsideOf(tap, ...)`.

### Dead and duplicated code and stale narration in the shader library

- [ ] `components/rtxvulkan/shaders/lib/traversal.glsl:640-655` — **[code]** `lightThrough` has no
  caller; only `lightPassage` is used. It is still named as a live rule in `traversal.glsl:109`,
  `:586`, `:657`, `:669` and `lights.glsl:60`. → Target shape: delete it, and point those comments at
  `lightPassage`.

- [ ] `components/rtxvulkan/shaders/lib/lights.glsl:61-71` — **[code]** Two `skyVisible` overloads
  take their arguments in different orders: `(SkySource, vec3, vec2)` and `(vec3, uint, vec2)`.
  `fogscatter.rgen:233,235` calls both, one line apart. → Target shape: one overload,
  `skyVisible(SkySource, position, draw)`, and the caller passes `skySourceAt(SKY_SOURCE_SUN)`.

- [ ] `components/rtxvulkan/shaders/trace/fogscatter.rgen:208-212`, `:223-225`;
  `lib/underwater.glsl:182-187` — **[code]** Narration that contradicts the code:
  - "`lampVisible` ... returns one for an empty reservoir". It does not: `lampPassage` traces toward
    light row 0, and its own doc says both callers refuse an empty reservoir.
  - "`skyVisible` ... reads the cloud deck over the point" and "`skyVisible`, with the cloud deck in
    it". But `lights.glsl:41-42` says "The cloud deck stands in no ray's way", and `skyPassage`
    reads no deck.

  → Target shape: correct the three comments.

- [ ] `components/rtxvulkan/shaders/lib/fog.glsl:686-692`; `lib/lights.glsl:519-531` — **[perf]**
  `considerLamp` stores `kept.mFrom = from` on every hold, but both callers overwrite `mFrom` right
  after the walk (`shading.glsl:250`, `fogscatter.rgen:201`). So `lampsInAir`'s per-lamp, per-cell
  `place = origin + direction * clamp(closest, from, to)` is computed for nothing in the fog's
  hottest loop. Its comment at `:686-687` already says nothing about where the lamp stands has to be
  worked out there. → Target shape: drop `considerLamp`'s `from` parameter and the `place`
  computation. `Reservoir::mFrom` is set once by whoever traces.

- [ ] `components/rtxvulkan/shaders/lib/lights.glsl:671-678` — **[perf]** `lampPenumbra` reloads
  the `GpuLight` row and recomputes `length(lamp.mPosition − kept.mFrom)`, both of which
  `lampPassage` (`:644-646`) just did for the same reservoir on the same path (`shading.glsl:252`,
  `:283`). → Target shape: `lampPassage` returns the penumbra radius beside `mOccluder` (or
  `Passage` carries the source's `radius / distance`), so `gather` reads one row and takes one
  length per lamp ray.

### Dead code in the device layer

- [ ] `components/rtxvulkan/device/memory/imageuse.hpp:111` — **[code]** `Use::sAnyGeneralWrite` has no reader anywhere in the tree. → Target shape: remove it.
- [ ] `components/rtxvulkan/device/validation.hpp:50`, `validation.cpp:150-154` — **[code]** `ValidationLog::clear` has no caller, tests included. Every reader goes through `takeErrorsOnThisThread`, whose comment says why a separate clear is the wrong shape. → Target shape: remove it.

### `setSea` is a test-only path whose contract comments claim a wait that does not exist

- [ ] `components/rtxvulkan/trace/wavepass.hpp:34-37`, `trace/tracemedia.hpp:57`,
  `vulkanrenderer.hpp:114-116`, `vulkanrenderer.cpp:132-138` — **[code]** `WavePass::describe` says
  "nothing may be in flight: `VulkanRenderer::setSea` waits the frames out first", and the renderer's
  header says it "waits the frames in flight out first". `setSea` waits for nothing. It compares and
  calls `describeSea`. The replacement is in fact safe without a wait, because the buffers it
  replaces bury themselves and the batch is flushed behind every frame in flight. So the comments
  state a precondition that is neither needed nor met. Also, nothing in `apps/` or `components/`
  calls `setSea`: the only caller is `apps/components_tests/rtxvulkan/trace/visibility/fixture.hpp:472`,
  so the game always traces `SeaState{}`.
  → Target shape: correct the three comments to say what holds: the burial makes the swap safe, and
  the flush is a stall the caller pays. Then either wire a real caller or say in the header that the
  sea is fixed and the setter serves the harness's fixtures.

### Stale narration in a member comment

- [ ] `components/rtxvulkan/scene/sceneacceleration.hpp:241-244` — **[code]** "Two totals, each
  assigned, because one accumulated. …" introduces a single member (`mTopLevelBytes`). The bottom
  levels' total moved to `BottomLevelStore`/`StructureStorage` long ago, and what remains narrates
  a fixed bug. → Target shape: one line saying the top level's size is assigned whenever it is made
  again, and not summed.

### Narration that no longer matches the code

- [ ] `components/rtx/scene/surface.hpp:99` and `surface.cpp:37` — **[code]** These say "`NiVertexColorProperty`'s
  three vertex modes" and "the three modes a NIF can state map one for one. The other three are
  `SceneUtil::Material`'s alone". The enum has four non-`None` cases (`Tint`, `Diffuse`, `Ambient`, `Glow`), and
  `vertexColourOf` maps four of `SceneUtil`'s six. → Target: say which modes map and why `Specular` does not.
- [ ] `components/rtx/image/texturedata.hpp:103` — **[code]** This says "`describeImage` says why the header's alpha
  flag is not consulted". The reason is in `texels.cpp`'s `sGlFormats` (`readFormat`), not in `describeImage`.
  → Target: point at `readFormat`.
- [ ] `components/rtx/environment/moonbuilder.cpp:68` — **[code]** `foldedPhase` says "Morrowind's phases are
  multiples of a quarter pi, so the fold is a subtraction". The phase is continuous now (`MoonModel::phaseEighths`,
  in the Accepted diff). The fold is still right, but the stated reason is not. → Target: drop the clause.
- [ ] `components/rtx/scene/scenedesc.hpp:45` — **[code]** This says "It appends and it dedups paths, and nothing
  else". The class also counts holds and drops rows with the last hold, recycles slots, keeps the walk/sweep/hand-over
  turn, and orders the lights. → Target: describe what it owns now.

### `docs/rtx/architecture.md` claims the code contradicts

- [ ] `docs/rtx/architecture.md:15-20` — **[code]** It says "but for four corrections". AGENTS.md's Accepted diff
  lists five; the fifth is the groundcover stood under its model's own transforms (`GroundcoverShapes`). The seam's
  change is then the sixth, not "a fifth". → Target: list five, and call the seam's the sixth.
- [ ] `docs/rtx/architecture.md:151-166` — **[code]** The rule is "a folder includes only the folders before it",
  and the table lists `shaders/` last. `RtxSourceTreeTest`'s `sFolderOrders` puts `shaders` first, and almost every
  folder includes `components/rtx/shaders/*.h`. Read literally, the document forbids most of the tree.
  → Target: put `shaders/` first in the table, as the test has it.
- [ ] `docs/rtx/architecture.md:200-201` — **[code]** It says "Companion maps (`_n`, `_nh`, `_spec`) and tangents
  reach the material as data slots". A normal map is taken under `TextureEncoding::Normal`, a third encoding with its
  own companion (`Spread`), and `MaterialTable::holdTextures` makes that a contract. → Target: name the normal
  encoding.
- [ ] `docs/rtx/architecture.md:423-424` — **[code]** The harness verbs are listed as
  "`info`, `scene`, `shot`, `view`, `bench`, `check`, `film`". `apps/rtxtool/verbs.cpp` also has `noise`, which
  AGENTS.md documents at length. → Target: add `noise`.
- [ ] `docs/rtx/architecture.md:397-404` — **[code]** §11 Threads lists the cell reader and the launch compile. It
  does not mention the construction-time hands `SpecularAlbedo` builds its table on (`runInParallel`, "albedo hand"),
  which the kernel compile's own `runInParallel` also uses. → Target: one line for the one-shot parallel builds at
  construction.

### The settings declaration names a reason that is not true

- [ ] `apps/openmw/mwrender/rtx/rtxsupport.cpp:270` — **[code]**
  `{ "Physics", "async num threads", sDrawThreads }` declines physics' worker-thread count under
  the ray tracer, with the reason "This sets the rasterizer's draw threads". The setting is the
  physics worker pool, which runs the same under either renderer. The rasterizer reads it only
  to drop a profiler line (`initStatsHandler`). `RtxSupportTest` made the entry necessary, and
  the reason was filled in wrong.
  → Target shape: `{ "Physics", "async num threads", {} }`, honoured, and drop `sDrawThreads`.

### Smaller defects in covered hunks

- [ ] `files/settings-default.cfg:1288` — **code** `[RTX] enabled` says it needs "a build
  configured with -DOPENMW_RTX=ON". That option no longer exists: since `41554ed6a9` the ray tracer
  is in every build, and no CMake file defines it. The comment points players and packagers at a
  flag that does nothing. → Target shape: drop the clause.
- [ ] `components/settings/categories/rtx.hpp:47-58` — **design** `upscale`, `indirect light` and
  `specular map layout` come from fixed sets but are `SettingValue<std::string>`. Each reader
  re-parses the name (`Rtx::menuIndex` in the launcher and the settings window, `rtxsettings.cpp`
  in the renderer), and a misspelt value is found only when the renderer reads it. The settings
  layer already parses enums at load with a sanitizer (`WindowMode`, `VSyncMode`, `HrtfMode`,
  `NavMeshRenderMode`). → Target shape: typed settings (`SettingValue<Rtx::Upscale>` and so on)
  with the parse and the refusal in `components/settings`, so menus and renderer read the enum.
  `sUpscaleNames` stays the one spelling list.
- [ ] `apps/openmw/mwscript/visibilitygates.cpp:247` — **perf** `update` re-runs every script with
  a watched input that moved, on the frame it moved. A gate script that reads `GameHour`, or any
  global that changes every frame (content that disables a lamp or window by the hour), therefore
  re-runs every frame. That is up to 16 event combinations × 16 interpreter frames
  (`VisibilityRun::run`), plus `rewatch()`'s quadratic rebuild of `mWatched`, on a path the header
  calls "a load or a step of the story". The worst frame depends on content the code does not
  bound. → Target shape: treat inputs that move continuously (`GameHour`, `DaysPassed`, …) as
  undecided in the gate (`Undecided` is already the answer for history-dependent scripts), or
  re-run at most once per game hour, so per-frame cost stays flat.

### Stale narration in the harness

- [ ] `apps/rtxtool/main.cpp:937-939` (`commandCheck`) — **[code]** "Two measured frames, because one of the claims is about a pair of them. A still camera resolving to a still picture cannot be asked of one frame." No row of `sChecks` (`model/benchrun.cpp:63-77`) compares two frames. The only claim about a still's frames, `findStillMoved`, runs only on hashed stops, and `check` neither hashes (`VerbPolicy` row: `mHashes = false`) nor turns off jitter. Either the claim went with an old check, or `check` was meant to ask it. → Target shape: if the still claim belongs to `check`, have `check` hash its frozen, unjittered stops. Otherwise drop the two frames and the comment.
- [ ] `apps/rtxtool/run.hpp:88-93` (`sHistoryFrames`) — **[code]** "where its weight on the frame the cut left is `(15/16)^64`, 1.6%, and the air's `0.9^64`" is the arithmetic of a 16-frame accumulator. `ACCUMULATE_FRAMES` is 32 (`look.h:1142`), so the constant is 128 frames, (31/32)¹²⁸ = 1.7 %, and the air's 0.9¹²⁸ ≈ 1·10⁻⁶. → Target shape: state the derivation in terms of `ACCUMULATE_FRAMES`, or with the current figures.

## Conventions

Includes and file layout against AGENTS.md.

### Include what you name: `runs.hpp` used as the index header

- [ ] `components/rtx/common/slots.hpp:13`, `scene/{texturetable,placementtable,scenetextures,scenedesc,rowhold,instancerecord}.hpp`,
  `scene/instancerecord.cpp`, `environment/{skybuilder,moonbuilder,nightsky}.hpp`, `renderer/sceneuploader.cpp` —
  **[code]** Each of these spells `Index` or `sNoIndex` and uses no `Run`, `RunAllocator`, `RunBuffer`, `RunList` or
  `BlockedValues`. They include `runs.hpp` only to reach `index.hpp` transitively, which also pulls
  `<memory>`/`<numeric>`/`<new>` into most of the scene. `mirror/{sceneadopter,meshresolver,materialresolver,mirroridentity}.hpp`
  and `mirror/cells/held.hpp` do the same and belong to another reviewer's scope.
  → Target: `#include <components/rtx/common/index.hpp>` (or `"index.hpp"` in `common/`) where the index is what
  the file names.

### Convention: includes from other folders spelled relatively

- [ ] `apps/openmw/mwrender/rtx/rtxrenderer.cpp:70` — **[code]** AGENTS.md: quoted includes are
  "of the file's own folder only, and any other folder is spelled from the root". Fifteen
  fork-only files under `mwrender/rtx/` include `"../ground.hpp"`, `"../../mwworld/ptr.hpp"` and
  similar: rtxrenderer.cpp/.hpp, worldmirror.cpp, skyreader.cpp, rippleemitters.cpp/.hpp,
  tracedterrain.cpp, tracedoverlay.cpp/.hpp, tracedview.hpp, tracedground.cpp/.hpp,
  classmasks.hpp, debugwalk.cpp and rtxwindow.cpp. So do the fork-only mwrender files
  framedescriber.cpp, glground.cpp, glrenderer.cpp, glworld.cpp and ripplerules.hpp/.cpp. Some
  mix both spellings in one file: rtxrenderer.cpp has `<apps/openmw/mwrender/mapoverlay.hpp>`
  beside `"../renderingmanager.hpp"`, and glground.cpp has `<apps/openmw/mwworld/cell.hpp>`
  beside `"../mwworld/cellstore.hpp"`.
  → Target shape: `<apps/openmw/...>` in the components/apps block for every header outside the
  file's own folder.

- [ ] `apps/openmw/mwrender/rtx/rtxrenderer.hpp:88` — **[code]** The class comment says every seam
  call is `noexcept` except the constructor, `awaitShaders` and `renderFrame`. But
  `poseForIntersection` (line 122) and `groundReadsGates` (line 130) are overrides without
  `noexcept`. Either the contract or the declarations are wrong; `poseForIntersection` runs on
  every drawable a CPU ray reaches.
  → Target shape: mark both `noexcept`, as the rest of the seam overrides are.

### Conventions in the harness

- [ ] `apps/rtxtool/{measurer,stopwriter,session,cameradriver,standingnote,film,run,numbervalue}.hpp`, `{measurer,stopwriter,stager,film,cameradriver,run,options,homekey,main}.cpp` — **[code]** 47 quoted includes reach into another folder (`"instruments/cardwatch.hpp"`, `"model/benchrecord.hpp"`). AGENTS.md allows quoted includes "of the file's own folder only, and any other folder is spelled from the root". `model/benchrecord.hpp` and `hosted.cpp` already use `<apps/rtxtool/instruments/…>` and `<apps/rtxtool/model/…>`. `stopwriter.cpp` mixes both forms for the same folder (lines 38 and 72). → Target shape: `<apps/rtxtool/instruments/…>` and `<apps/rtxtool/model/…>` throughout, in the `<components/…>`/`<apps/…>` block.
- [ ] `apps/rtxtool/instruments/threadcounterswin32.cpp`, `threadcountersposix.cpp` (the `#else` branch) — **[code]** The Windows file repeats the macOS branch of the POSIX file verbatim: an empty `Events`, the same "this system gives a process no counters" constructor, and empty `start`/`stop`. → Target shape: one `threadcountersnone.cpp` that CMake chooses for Windows and macOS, and a Linux-only `threadcounterslinux.cpp` without the `#if`. This is the pattern AGENTS.md names for code only one build has (`crashunsupported.cpp`).

### Conventions in the crash catcher and its neighbours

- [ ] `components/crashcatcher/crashpadmonitor.cpp:2`, `components/crashcatcher/crashunsupported.cpp:1-10`,
  `components/platform/libraryposix.cpp:1-3`, `components/platform/librarywin32.cpp:3` — **[code]** These
  break the include-block rule:
  - `crashpadmonitor.cpp` puts `"crashnote.hpp"`, which it does not implement, in its own-header block.
  - `crashunsupported.cpp` puts its own headers (`crash.hpp`, `crashinstall.hpp`) after the standard and
    component blocks.
  - `libraryposix.cpp` includes `<cstdint>` before `"library.hpp"`.
  - `librarywin32.cpp` includes `<windows.h>` directly, where every other `…win32.cpp` in the folder includes
    `<components/misc/windows.hpp>`.

  → Target shape: own header first, local headers last, `components/misc/windows.hpp` in place of
  `<windows.h>`.

## Fork hunks that the Accepted diff does not cover

Each item is a decision: add an Accepted-diff entry that gives the reason, or revert the hunk.

### Hunks no Accepted-diff entry or integration hook covers

- [ ] `CMakeLists.txt:681` — **design** `4244` and `4267` (implicit narrowing) are added to MSVC's
  `WARNINGS_DISABLE` for the whole tree. AGENTS.md accepts five checks *added* to upstream's. This
  hunk *removes* two that upstream's `/W4` reported, from upstream code as well as the fork's, so a
  narrowing upstream's MSVC build warned about now passes silently. → Target shape: drop the hunk,
  or confine it to the fork's targets (`openmw-rtx-vulkan`, components' rtx sources) with
  `target_compile_options`. If it must stay tree-wide, list it in AGENTS.md beside the five checks.
- [ ] `CMakeLists.txt:1-23`, `:521` — **design** Build-floor and toolchain changes that neither the
  checks entry nor any hook names:
  - `cmake_minimum_required` goes from 3.16 to 3.31, only so `CMakePresets.json` may carry
    `$comment`.
  - `find_package(Boost 1.83.0 …)` goes from 1.70 (`77cde9132a`, for flat maps).
  - `CMAKE_CXX_SCAN_FOR_MODULES OFF`.
  - The ccache-launcher fallback.
  - The rewrite of the MSVC ccache `/Zi → /Z7` block into `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`.
  - The `$<COMPILE_LANGUAGE:C,CXX>` wrapping of `/MP /bigobj /Zc:__cplusplus /utf-8`. The comment
    ties this to Crashpad's MASM, so it falls under the crash catcher entry, but that entry names
    only the component.

  Each one changes how upstream code is configured. → Target shape: list them in AGENTS.md under
  the entry each serves (presets/driver, Crashpad, the RT's containers), or revert the ones no entry
  needs. The 3.31 floor in particular exists only for comments in a JSON file.
- [ ] `CMakeLists.txt:885`, `files/licenses/*` — **design** `install_fork_licenses` and the
  licence texts ship third-party notices for what the fork compiles in. They are necessary, but
  AGENTS.md lists only `README.md` and `CI/` among the non-code changes that stay. → Target shape:
  add them to the Accepted diff's "rest of the tree".
- [ ] `.github/workflows/*`, `CMakePresets.json`, `.zed/tasks.json`, `omw`, `omw.cmd`,
  `.claude/skills/*`, `.gitattributes`, `.gitignore` — **design** The fork deletes upstream's
  `macos.yml`, `push.yml`, `release.yml` and `windows.yml`, adds its own workflows, and adds driver
  and editor tooling at the repository root. AGENTS.md accepts `CI/` and names `./omw` as the way
  in, but no entry covers `.github/` or the root tooling files as changes to the upstream tree.
  → Target shape: one Accepted-diff line naming the fork's tooling files and the workflow
  replacement, so the next merge from upstream knows the deleted workflows are meant to stay
  deleted.

## Coverage

What each reviewer read and found sound. Not items: a later review reads these to skip ground already covered.

### Review 01: `components/rtxvulkan/shaders/lib/`

- `brdf.h`, `gloss.glsl`: GGX D, the height-correlated `V`, `G2/G1` as the VNDF weight (the spherical
  caps), Schlick with `F90`, the table taps and the host integration (no zero `whole`, so no infinite
  compensation), the `1 + F0(1/E − 1)` compensation. Checked that the lobe and the table agree, and
  that the floor matches on both sides.
- `bounceDraw` / `bounceLight` / `bounceArriving`: the half chance, the sheet face chance
  `T/(1+T)`, the rate, the diffuse `(1−F)/(1−chance)` and the lobe weight. The reuse's
  `mDiffuseChance` and `diffuseShare` match these term for term. No NaN at `chance = 1` (selected
  away) or for metals.
- `gather`: the sky pick by weight (no quotient), the RIS reservoir `total/weight`, the ratio
  estimator for the split lamps, `keepsSecond` and `mixSplit` share arithmetic, the draw ordering of
  the sequence.
- `lights.glsl`: `falloffAlong`'s polynomial division and its `atan` remainder, re-derived; the
  chord clip; `pickByWeight`'s fallbacks; the fill ball blend; the cone sampling in `lampPassage`.
- `random.glsl`: PCG RXS-M-XS, the 24-bit floats, the fixed-point blue-noise turn, the cone, sphere
  and cosine draws, and that the seed chain has no duplicates (0x51–0x6E).
- `fog.glsl` / `froxel.glsl`: Jendersie–d'Eon Mie fit coefficients, `fogColumnOver`'s four sign
  cases, `fogThrough`, the slice edge reads, the light-grid DDA and its budget, the edge ramp.
- `underwater.glsl`: the closed-form beam with its `g → 0` limit (the albedo convention matches the
  sky term), the shaft ratio march.
- `sea.glsl`: lost-slope moments, caustic Jacobian/gain, rain lattice.
- `water.glsl`: Fresnel from below via the refracted cosine, TIR fallback, shore fade, leg media.
- `sky.glsl`: the disc's solid angle `π·chord²` (exact), the moon's lunar-Lambert blend, the deck's
  fade, the compositing order.
- `starfield.glsl`, `bloom.glsl`: the 13-tap and tent weights sum to one.
- `medium.glsl`, `sprites.glsl`: order-free coverage sums, chord fractions, `paintedOver`, the
  six-way bake.
- `payload.glsl`: the 25-word pack and unpack round trip, the flags layout.
- `sharedexponent.glsl`: RGB9E5 exponent carry (no overflow at the largest value).
- `reproject.glsl`: the cancellation-free `farther`, the previous-screen bounds. `surfacematch.glsl`,
  `runningmean.glsl`, `geometry.glsl` (Hanika lift, `acrossTriangle`), `texturing.glsl` (cone LOD,
  anisotropic gradients, normal-map spread), `ground.glsl` (delight floor 0.5, so no divide by
  zero; the composite's companion is neutral, so nothing is delit twice).
- `bouncereservoir.glsl` / `bouncepairs.glsl`: pairwise MIS against Wyman et al. Algorithm 7, the
  confidence cap, the reconnection Jacobian, the pairing self-inverse.
- No other dead function in `lib/`: every other definition has a caller outside comments.

### Shader kernels review: trace, upscale, scene, display, texture, probes, gui

- **`accumulate.comp`.** The sky write-through, reset, count and moments blend, the 9e5 fast
  means, and the dual-motion search bounds and `hidden` gate (apart from the jitter above).
- **`accumulateclamp.comp`.** Shared-memory spans and insets (no out-of-bounds index), surface
  gating of loads, antilag, and variance selection. The ring loaded with the switch off is already
  in `.notes/reuse.md`.
- **`accumulatesurface.comp`, and the passes and barriers in `accumulatepass.cpp`,
  `denoisepasses.cpp`, `denoisehistory.cpp` and `temporalturns.hpp`.** Turns, fresh/discard
  semantics, and the clamp barriers.
- **`atrous.comp` and `atrouspass.cpp`.** The three-image ping-pong, read-after-write and
  write-after-read barriers, and the history-fix stride (never 0 for counts ≤ 3).
- **The shadow denoiser** (`shadowmask.comp`, `shadowtiles.comp`, `shadowfilter.comp`,
  `shadowpass.cpp`):
  - mask packing and its tile guard;
  - row assembly with a negative corner (exact because `REACH % 8 == 0`);
  - uniform early-outs;
  - the cleared or skipped-level copy chain into the history, scratch and visibility images;
  - half packing of `SHADOW_NO_RECEIVER`.
- **`specular.comp`.** `toEyeBefore` against `previousScreenThrough`, including the arms' spread.
- **`pane.comp` and `composite.comp`.** Binding formats and the shadow/no-shadow select.
- **`visibility.rgen`, `visibilityhit.rchit`, `visibility.rahit`, `visibility.rmiss`,
  `visibilityunshaded.rmiss`:**
  - arms-then-world peel and layer budget, and the stack identities;
  - motion vector (unjittered at both ends, as FSR wants);
  - the miss count (one `traceRayEXT` site, so one miss per pixel);
  - reuse channel writes against the resolve;
  - the sun-glare query.
- **Bounce reuse** (`bouncevalidate.rgen`, `bouncetemporal.comp`, `bouncepairs.rgen`,
  `visibilitypass.cpp::recordBounceReuse`). Validation reads nothing the concurrent trace writes,
  the handovers sit between the kernels, and no thread leaves before the group-mean barriers.
- **The fog** (`fogdepth.rgen`, `fogscatter.rgen`, `fogintegrate.comp`). No divide by zero in
  `lampScatter`, the reprojection bounds hold, and the slice and tent logic is sound.
- **The sprites** (`spritecomposite.rgen`, `spriteshelter.rgen`, `spriteemitters.rgen`,
  `scene/spriterects.comp`, `spritestarts.comp`, `spriteruns.comp`, `spriteshade.comp`, and the C++
  in `spritebin.cpp` and `spritepasses.cpp`):
  - the shown-pixel partition (no write race);
  - the scan, and the overflow to `SPRITE_LIST_UNBINNED`;
  - the three-word barrier cycle in the runs pass;
  - the odd-even merge sort bounds;
  - uniform barriers.
- **The sea and ripples** (`waverows.comp`, `wavecolumns.comp`, `ripplestep.comp`,
  `ripplecompose.comp`, `ripplepass.cpp`). The Nyquist row/column is zero by the generator's
  `wavelength <= shortest` cut, so the packed real fields stay conjugate-symmetric. The ripple
  ping-pong and its barriers are sound.
- **`sunglare.comp` and `stress.comp`.**
- **The display chain** (`histogram.comp`, `exposure.comp`, `bloomdown.comp`, `bloomup.comp`,
  `tone.comp`, `digest.comp`, `line.vert`, `line.frag`, and the C++ in `bloompass.cpp`,
  `exposurepass.cpp`, `tonepass.cpp`, `displaychain.cpp`):
  - workgroup size equals the bin count;
  - the 64-bit reduction;
  - the histogram extent equals the shown extent for the world;
  - the reverse depth in clip space;
  - the bloom level barriers (all in `GENERAL`).
- **The upscaler** (`fsrcallbacks.glsl`, the seven FSR wrappers, `fsrframe.cpp`, `upscaler.cpp`).
  The depth reconstruction matches `mDeviceToViewDepth` and `sNear`, the motion scale and jitter
  options are right, and the pass order, parity and clears are sound.
- **Skinning and morphs** (`scene/skin.comp`, `scene/morph.comp`, `skinpass.cpp`), which match
  `RigGeometry::cull` and `MorphGeometry::cull`.
- **The texture passes** (`texture/mipchain.comp`, `shadingsum.comp`, `shadingmap.comp`,
  `normalspread.comp`, `groundcomposite.comp`). Barriers are in uniform control flow, odd-extent
  clamps hold, and the shared-memory reductions are sound.
- **`gui/gui.vert` and `gui.frag`.**
- **`probes/`** (test-only device probes, built only where tests run): skimmed, and not reviewed as
  production code.

### 03 — shared shader headers (`components/rtx/shaders/`) and SPIR-V pinning (`components/rtxvulkan/spirv/`)

- **Layout agreement.** Every shared structure was summed by hand against its `static_assert`:
  `GpuMesh` 24, `GpuInstance` 64, `GpuLight` 40, `GpuLightGrid` 28, `GpuLayer` 64, `GpuMaterial`
  108, `GpuSprite` 56, `GpuEmitter` 40, `GpuEmitterFrame` 16, `GpuPresence` 24, `GpuTables` 184,
  `Basis`/`ScreenBasis` 44, `Camera` 68, `Eyes` 136, all of `sky.h`, `VisibilityConstants` (4 bytes
  of padding before `mTables` at 1416, alike on both sides), `SpriteBinConstants`, `SkinConstants`,
  `GroundCompositeConstants`, `FsrConstants`, the denoiser blocks, `LineConstants` and
  `FrameCounts`. Nested structures without their own assert (`SkySource`, `SpriteBinFrame`) are
  covered by their owner's. Every 8-byte member is 8-aligned on both sides. Every GLSL block that
  reads a shared structure is declared `scalar`.
- **Bit fields.** No overlaps among: the `MATERIAL_*` bits (including the unit and alpha-side
  fields), `INSTANCE_LAMP_BODY` above the ray masks, `LIGHT_*` traits, `TANGENT_*` (two 15-bit
  coordinates plus two flags), `BOUNCE_STATE_*`/`BOUNCE_ORIGIN_*`, and the run word
  (`RUN_COUNT_MASK` is checked in `meshresolver.cpp`; `first` cannot reach 2^24 with
  `unsigned short` vertices).
- **Packing round trips.** `octahedralStep`/`octahedralCoordinate` give odd counts and exact axes.
  The surface-normal code stays under 2^24 (4094 + 4094 × 4095). `unpackSurfaceNormal` selects
  nought for `SURFACE_NO_NORMAL`. The integer ranges of `tracedPixelUnder` and `shownPixelsFrom` fit
  in 32 bits. `lowBits` avoids the shift by 32. `digestFold`/`digestTexel` are MurmurHash3's
  round and fmix32.
- **`portable.h` and `hosttypes.h`.** The macros (`RTX_SHADER`, `RTX_PRECISE`, `RTX_ZERO`,
  `RTX_INIT`) and the host spellings of the GLSL builtins: `std::clamp` keeps its references valid
  within the full expression, and `min(low, fast) <= max(high, fast)` always holds.
- **`spirvpin`.**
  - The treatment table covers all 81 GLSL.std.450 instructions against the Vulkan precision
    appendix.
  - Every lowered form matches the order its header states: dot, the three matrix products, mod,
    rem, length, distance, normalize, cross, mix, smoothstep, reflect, refract, faceforward,
    asin/acos, round, fma.
  - The fusion pass only treats ids as reads where they are not literals. Unknown literals only
    cost a fusion, and no id is ever mistaken for a literal.
  - `precise` propagates to every rewritten step.
  - A multiply that a name or debug instruction still mentions is kept.
  - Capabilities, extensions, imports, decorations and declarations are inserted where SPIR-V's
    layout allows them.
  - `verify()` runs on the result.
  - `VK_KHR_shader_fma` / `shaderFmaFloat32` are required by the device.
- **`spirvdigest`.** Tokenising, colour refinement (monotone and terminating), first-naming order
  for globals that refinement leaves alike, an unambiguous canonical byte stream, and the call
  order from the entry points. Non-semantic debug info is stripped by `omw kernels` before the
  digest, so the "debug skipped" claim holds for its input.
- **`spirvfile` and `spirvpintool`.** The write beside the module and then rename, the magic check,
  and the error paths.
- **No dead shared constant or function** outside test probes: every `RTX_SHADER` function and
  shared constant has a reader. `UNITS_PER_METRE` equals `Constants::UnitsPerMeter` as a float.

### Review 04 — Vulkan device, pipeline, present, display, frame ring, renderer

- `Timeline`, `Graveyard`, `Retiring`: monotone stamps under the lock, prefix release, cross-thread burials from compile threads, `markIdle` after `vkDeviceWaitIdle`. Teardown order in `Device` and `VulkanRenderer`: the pool's staging and holds bury before the graveyard's `collectIdle`, the allocator is asserted empty, and the instance and surface go last.
- `CommandPool` and `Batch`: deferred batches ride every submit, `submitAndWait` and present included. Staging blocks are stamped at release, with best fit. Individual reset and `ONE_TIME_SUBMIT` re-begin. `discard` of a buffer still recording. Scratch submit vectors are allocation-free once settled.
- `FrameRing`: two slots, `makeRoom` before reuse, place buffers grown to the busiest frame, `skip`, the report queue bounded at `sFrameSlots`, the host-read barriers for counts, digest and picture (`orderForHostRead` HOST/HOST_READ), and a picture ring of slots + 1.
- `Presenter` and `Swapchain`: one acquire semaphore per image, reused only after its blit's timeline value; a render semaphore per image; present fences reset before reuse and awaited before a rebuild; acquire, present and source barrier at `ALL_TRANSFER`; `PRESENT_SRC` with a `NONE` second scope; `OUT_OF_DATE` and `SUBOPTIMAL` handling; the hidden-surface rebuild guard; FIFO, mailbox and relaxed selection; transfer-dst usage and opaque alpha refused when absent.
- Device loss: every submit, wait, acquire, present, query read and idle goes through `checkVk(device, …)` or `checkVkWait` to `deviceFailed` with the fault and checkpoint report; the 10 s patience; `VK_INCOMPLETE` in enumerations.
- `Instance` and `Device` bring-up: 1.4 floor, volk single load, required extension and feature tables against Turing and RDNA 2 under RADV (position fetch, maintenance1, primitive culling for `SKIP_AABBS`, shader clock, fma floors), surface-maintenance needs, the queue family, and the format checks.
- `MemoryAllocator`: exact-placement type masks, content pools per video type, the budget ceilings, priorities, the dedicated requirement, the scratch, SBT and structure alignments, `ReadBack` as coherent cached memory, `HostWritten` as BAR.
- `Image`, `Buffer`, `AccelerationStructure`, `StructureStorage`: bury order (views before images, handle before memory, structure handle before its room cools), mutable-format views, and readback layout restoration.
- `Barriers` batching and merging, `handOver`, and `Image::addTransition`'s same-layout memory-barrier path.
- `pipeline/`: inline modules through maintenance5, the creation-feedback chain, specialization lifetimes (the deque), the SBT layout (handle and base alignment, hit-record stride, raygen size equal to stride), recursion depth 1, the `NO_NULL_*` and `SKIP_AABBS` promises, push-descriptor writes checked against the binding table, layout and handle construction order.
- `display/`: the bloom pyramid's barriers, the exposure clear and reduce ordering with the inline fixed write, the tone pass's bindings, the debug lines' per-slot vertex buffer, and pictures kept off the frame's exposure and glare.
- `PictureTracer`: deferred batches stamped with `notePictureRide`, the growth burying what a pending picture names, constants written inline in queue order.
- `GpuTimer` (zones reserved, reset per pair, `WAIT_BIT` after the frame's wait), the AMD marker ring, the pipeline cache (validated header, atomic rename, sweep), frame-path allocation in the ring, presenter, pool and renderer (none found), and `CMakeLists.txt` (shader pin and validate chain, private Vulkan, `OPENMW_RTX_DEBUG_NAMES` as 0/1).
- Core leakage: `components/rtx` names no Vulkan type outside the shared shader headers' prose.

### Review 05: components/rtxvulkan/{trace,scene,texture,upscale,gui}

- **Acceleration structures** (`sceneacceleration`, `bottomlevelstore`, `structurebuild`):
  - The TLAS is rebuilt from the placing slot's row copy only where that copy owes, a refit runs,
    or compaction moves a structure. Growth doubles, the old handle is buried before the storage
    grows, and the scratch is kept at its high-water mark.
  - The refit lays out one scratch over every deformed mesh, with the rota's whole rebuild taking
    build scratch. `sizeRefitScratch` matches `prepareRefit`'s layout, and the base address is
    aligned (`buffer.cpp:49`).
  - The rota takes one rebuild per posed placement and skips one built on the same placement.
  - Compaction applies only to static meshes, so a refit never targets a compacted handle. It is
    budgeted per placement, its rows are rewritten before the sync, and the old structure is
    buried under the placement's submit.
  - Compaction queries are asked once per structure and read only after the timeline passes them.
    A replaced pool re-asks its outstanding queries, and `VK_NOT_READY` retries.
  - A freed room cools until the timeline passes the stamp it was buried with
    (`StructureStorage`).
  - Barriers: build → AS read, skin → AS build (SHADER_READ is the right access for build
    inputs), and the head barrier ahead of the builds.
- **Skinning and pose copies** (`skinpass`, `skintables`, `SlotBlocks` use): owed sets, arrival
  staging into copy 0, the previous-pose copy for motion, normals and tangents posed with the
  positions, and the way `finishReads` ignores a pending stamp.
- **Scene tables** (`scenebuffers`, `devicescene`, `sceneslots`):
  - Per-slot `SlotTable` debts for the instance, mesh and material rows, and whole per-placement
    tables, all written only after `finishReads`.
  - Blocked attributes keep their addresses. `mLightGrid` stays consistent with the slot the trace
    reads.
- **Textures** (`texture`, `texturearrival`, `texturecost`, the passes):
  - Side choice and fallback levels, and the phase-ordered barriers of an arrival (writes → chains
    → shading → spreads → bakes).
  - Burial of a slot re-stood or dropped while a frame in flight samples it, and per-set debts
    with `mBound` stamps against update-after-bind.
  - Anisotropy re-owes every slot. Every picture is placed (synced) before it is traced
    (`OffscreenTrace` → `SceneUploader::hand`).
  - Composite bakes run in the placement after arrival, over the freshly synced set and tables.
- **Trace** (`tracechain`, `visibilitypass`, `gbuffer`, `fogvolume`, `spritebin`/`spritepasses`,
  `ripplepass`, `tracemedia`, `bouncereservoirs`, `sunglarepass`):
  - Discards and hand-overs per pass. The frame block goes through `vkCmdUpdateBuffer` in queue
    order. Pushed set zero persists across the fog launches.
  - The bounce-reuse ordering: validate touches only history and origins-before, and the history
    is written only by the resolve.
  - The sprite bin's list is sized from waited-for reports. The ripple field steps on whole ticks
    only.
  - Pictures defer behind the frame's tables (`pictureRides`).
- **Denoiser** (`denoise/*`): turns and freshness, the discard, the accumulate → clamp → wavelet
  barriers (the clamp writes only its own pixel of `blended`, and the neighbours come from the fast
  blend), the shadow filter's level ordering, and the composite's stand-ins.
- **Upscaler**, apart from the finding above: the jitter sign, the reversed-infinite depth
  parameters (they match `setupDeviceDepthToViewSpaceDepthParams`), the phase-count walk, the
  history parity, the per-frame clears, the per-slot constant blocks, and the reset on resize and
  on lost reprojection. `mTanHalfFov` is read by no 3.1.4 pass, so its horizontal value does no
  harm.
- **GUI** (`guitextures`, `guidrawer`, `guipass`): deferred batches are ordered ahead of the
  picture batch that writes a texture. Dropped textures are kept on the batch. Read-backs carry a
  host-read barrier into coherent memory. The vertex ring waits on the draw two back.
- **Folder order and includes** in the five folders: no later folder is included, and local
  includes come only from the file's own folder.

### Review 06 — `components/rtx/mirror/` and `components/rtx/preprocess/`

- **The walk** (`SceneExtractor::Traversal`): the identity fold (stamp restarts at depth ≤ 2, child
  index below), transforms accumulated in double and narrowed once, billboards turned toward the eye
  in local space (`transform3x3` is row-vector, correct), the particle step under a borrowed
  `CULL_VISITOR` with the emitter clock's own frame numbers (one step per frame across the rain, sea
  and world walks), the sequence clock visitor, `markReached` on the game's frame number, switch,
  sequence and LOD descent, distortion refusal, class, glow and lamp-body propagation, and the
  jump flag.
- **Freezing**: what `FrozenFace` covers against what the game changes on a static reference root
  (door rotation, `Animation::addEffect` on `mInsert`, enable and disable, cell re-parenting). The
  hold and release symmetry of `FrozenKey` (refused meshes, null material keys). The thaw of unmet
  runs ahead of the sweep, and member destruction order.
- **`Kept`'s counting** (stamp, hold, drop, reach, abandon, whole, settle), apart from the redundant
  flag above. Entries are re-found after inserts everywhere a map can grow (`placeSprites`, `describe`).
- **Meshes**: refusals filed once; the deformer fit test; a slot re-read where a rig's source or skin
  changed; the rig pose matches `RigGeometry::cull` (`skinToSkel * mTransform` composed per bone); morph
  bases; overall-bound normals and colours; texture coordinates for the second unit; tangents at
  `sTangentUnit`; vertex colours decoded once to linear.
- **Materials**: the animated per-node state set (set up again on a chain-shape or generation
  change), the `Worn` ring of 32+ images, texture refusals per slot retried only after a slot frees,
  the texture transform algebra `(uv-0.5)·s+0.5+o`, ambient override folded into emission.
- **Emitters**: deferred read after the walk, fade and opacity rules, finite checks on simulated data,
  the oriented-streak axis, sprite texture and bake holds swept with the emitter.
- **Cell ring**: the ask, sift, wait and adopt order; one cell and one cell's grass per frame; supply
  threading (`Monitor`, `onTheWay`, swap-based request hand-off with no frame-path allocation after
  warm-up); `CellHolds` deferred releases; the placer's size-rule prefix, gates, blacklist, day-night
  restand, ground flatten rewrite and `standsAsHeld`; ground reading (index and UV order checked
  against `BufferCache` and `fillVertexBuffers`, the no-land quad's winding, the mask transform);
  the lamp anchor matching `SceneUtil::addLight`; groundcover's own material key.
- **Template walk**: read-only over shared templates. It walks the same drawables and LOD and
  sequence rule (`descendInWorld`), every `NightDaySwitch` branch with modes matching
  `DayNightCallback`'s index rule, and has its own `NodeKinds`, content and facts per thread.
- **Preprocess**: `ShapeFold`'s canonical spelling and chain pairing (the first-written copy is
  kept), the pocket test (Möller–Trumbore, generalized winding with a dipole far field, signs
  consistent), the probe table, `CreaseSplit`'s weld, smoothing groups, union-find and
  angle-weighted normals, `withCopies` for every attribute, `ImageFactCache` normalising like the
  texture table, and the texel mean taken in linear light.
- **Dependencies**: `mirror/` includes only `common, image, preprocess, scene, frame, shaders` and
  itself; `preprocess/` only `common, image` and itself. No `rtxvulkan` include and no `apps/` include.
  OSG, `osgParticle`, `osgUtil`, `SceneUtil`, `NifOsg`, `Terrain` and `ESM` are content and
  scene-graph readers, which architecture.md §2 and §7 allow ("the scene arrives as an `osg::Node`
  graph, and the core reads it"). The few `GL_*` constants (`meshresolver.cpp:46`,
  `groundreader.cpp:125`) are OSG's own encoding of primitives and pixel formats, not API calls.
  `PoseCull` stands up an `osgUtil::CullVisitor`/`RenderStage` that culls and draws nothing, for
  upstream's CPU skinning. That is consistent with the rule.

### Review 07: components/rtx core (common, image, scene, environment, frame, renderer, view) and docs/rtx/architecture.md

- `common/`: `SlotPool`'s min-heap and double-free byte, `SlotSet`'s Removed state and the compact-before-read rule,
  `SlotChanges`' "last word wins", `SlotRows`/`HeldRows`, `RunAllocator` best fit with block boundaries (the hole
  invariants hold through allocate, release and the end-shrink), `BlockedValues`, `RunBuffer`, `RunList`'s counting
  sort, `Spares`/`Recycled`/`reuseKeeping`, `Monitor`'s idle/busy handshake, `Job`, `Worker`, `runInParallel`,
  `HashState`, `fromHalf`, `NamedEnum`'s compile-time checks, `followsMenu`.
- `image/`: sRGB tables and `toLinear` exactness, BC1/BC2/BC3 colour and alpha decode (endpoint replication, 4-colour
  mode for BC2/BC3, the BC3 6/8-value palettes), 16-bit and loose widening (`(v·255 + top/2)/top`), the
  layout-against-OSG byte check and the volume first-slice handling in `describeLevels`, `laidBytes`/reserve
  agreement in `SceneTextures`, `AlphaImage`, `reachesSolid`, `meanTexel`.
- `scene/`: mesh, material, texture and deformer table holds and frees (including the hold-new-before-drop-old order
  in `MaterialTable::set`), layer and mask run release, `DeformerTable` stand/pose/release, placement links and
  counts, `updateInstanceRecords` (the settled/moved split, identity motion, water's in-plane step), the determinant
  rule for `mFlipFacing`, `LightGrid` incremental rebin and its double-precision bounds, `CompositeQueue` ring and
  stale-ask handling, `Refusals` dedup without allocation, `orderLights`' total order over every `GpuLight` field,
  `Glow`, the light flicker and pulse arithmetic.
- `environment/`: `zenithShareOf`'s closed form (decomposition `α Q + β Q'` and the `tiltedLog` antiderivative
  verified by differentiation), cloud cap least squares and sheet determinant, TMA (JONSWAP σ 0.07/0.09, γ 3.3,
  Kitaigorodskii φ), Donelan–Banner spreading and its normalisation, the `dω/dk` and `1/k` Jacobians, Hs = 4σ,
  Dirichlet and bilinear power in `waveCurvature`, Rayleigh depths (`0.008569 λ⁻⁴` with the Hansen–Travis correction:
  0.0683/0.0973/0.222), Kasten–Young air mass (37.92 at the horizon), horizon dip `√(2h/R)`, full-moon irradiance
  `E p sin²t`, Allen's phase law, McEwen's lunar-Lambert weight, fog extinction matched at the half point, `FogDrift`
  integration, `fogOffsets`.
- `frame/`: `Reconstruction::resolve` (single rule, reuse gated on traced indirect light), FSR ratio and render
  extent truncation, jitter phase count, Halton jitter, cone spread angle, double-precision view inversion and the NaN
  basis test, `sampleFrame`'s single-writer asserts, void-and-cluster ranking and portable Fisher–Yates,
  `BouncePairing` symmetry on the torus, `SpecularAlbedo` VNDF estimator (`G2/G1`, Schlick split), `SpriteListSize`
  64-bit sizing.
- `renderer/`: `SceneUploader`'s three branches (revision test, drop-before-extend, place after extend, clear and
  advance in one tail), `SceneSlot`/`GuiSlot`, `FrameSpend`, `Channel` coverage, `frameImage` crop and area average,
  the PNG `iTXt` chunk, `digestShaders`.
- `view/`: `ViewScene` ownership, the `OffscreenTrace` rebuild order (clear, walk, retire, hand), `pick` posing.
- Dependencies: no `apps/` or `rtxvulkan` include anywhere in `components/rtx`. Every include in my folders respects
  `sFolderOrders`. `GLOSSARY.md`'s names all exist in the tree. The CMake rtx block lists every source.
- Frame-path allocation: per-frame lists keep their capacity, out-parameters are refilled (`describePresences`,
  `updateInstanceRecords`, `makeInstanceRecords`), `Refusals` repeats allocate nothing, and `sheetNamed` compares
  without building strings. The only exception is the one listed above.

### Review 08 — apps/openmw/mwrender (seam, GlRenderer, RtxRenderer, upstream hunks)

- **The seam (`renderer.hpp/.cpp`)**: no game code branches on the renderer kind outside
  `Engine`'s choice and `createRenderer`. `getPostProcessor`/`getCompileOperation` are the
  documented upstream exceptions, and their callers (postprocessorhud, Lua postprocessing,
  `togglePostProcessorHud`, `Scene`) test null or are unreachable without a chain.
  `support()` answers the console, Lua and the settings window. Frame clock, nested frames,
  limiter, presentation and `processChangedSettings` filtering are sound.
- **Only the chosen renderer starts**: `createRenderer` constructs one. RtxRenderer asserts no
  GL context, turns the shader visitor off, and builds no LightManager, Water, SkyManager, RTT
  or PostProcessor. GlRenderer touches nothing of rtx or Vulkan. Shared game-side objects
  (Precipitation, ObjectStorage, FrameDescriber, StableIdentity stamps) are plain OSG/CPU state.
- **Accepted-diff coverage of upstream hunks**: animation (NightDaySwitch; glow-light fade and
  source radius, read only by the tracer since GL's glow light has no LightController), groundcover
  (GroundcoverShapes), localmap (`mapDepthRange`), pingpongcanvas/postprocessor (gamma,
  presentation, `READ_DRAW_FRAMEBUFFER`), objectpaging (ObjectStorage walk, gates), objects/
  setupPlayer (StableIdentity), sky (Precipitation split, `TextureType("diffuseMap")` on unit 0,
  which the shader visitor already defaulted), skyutil (vertex rules), ripplesimulation
  (ripplerules), renderingmanager (seam calls, `getFieldOfView`, projection from the
  presentation, `Mask_GUI` out of the intersection mask for the present quad, `notifyJumped`/
  `notifyTeleport`, gates). The static_casts, `Crash::notNull` and `const_cast` hunks belong to the
  five checks. No hunk is outside the Accepted diff or integration.
- **Hooks**: `notifyCut`/`notifyWorldspaceChanged` reach the renderer between frames, from
  teleport, worldspace change, time skip, GameHour writes (`DateTimeManager::jumps`) and Lua
  `advanceTime`. `mLoss` is kept across untraced frames. Jumps go through
  `World::moveObject(jumps = movePhysics)`, and `moveObjectBy` passes false; the span is filled
  and cleared around one `renderFrame`, and nothing notes a jump between describe and render.
  The pose hook (`DrawablePoser`) poses once per frame number. FOV override and arms FOV flow
  through `EyeState`. Projection shift is in clip units, applied by `shiftPicture` with
  `rayAt`'s sign. Precipitation's `isShown`/`isOccluded` feed both renderers, and
  `setViewPoint` is set before the draw.
- **GlRenderer/GlWorld**: compared line by line with upstream's RenderingManager constructor,
  update, setSun*/setSkyEnabled/setWater*, processChangedSettings, LoadingScreen, ScreenshotManager
  and Engine paths. Order and gating match (sky setters only when outdoors and weather ran,
  occluder edge-triggered, shadow mode on the sky edge, water cull per exterior). Cover masks,
  stereo pair masks, freeze-frame and screenshot reads from the frame FBO all match too.
- **RtxRenderer**: phase machine across every entry point and early return; `traceWorld` refusals
  still present. Member destruction order (frozen texture before backend; mirror asserts empty
  after `detachWorld`). freezeFrame/capture/readFrame row order and crop. Settings application
  (upscale, reach, anisotropy, gamma, indirect). Window fit settle logic.
- **Lifetime of shared OSG objects**: SceneFrame references (SkyState, Precipitation, current
  terrain, ObjectStorage). `mWorldRoot` reset at detach. The ring thread stops before
  RenderingManager's grounds and storages go. TracedTerrain carriers keep marker pointers inside
  their own containers. TracedView and TracedOverlay leave the ViewQueue in their destructors,
  and `forget` nulls them during a flush. SharedTexture follows a replaced freeze image by
  pointer and modified count.
- **WorldMirror, SkyReader, RippleEmitters, ViewQueue, TracedView copy states, TracedGround,
  DebugWalk, FrameTimer, FrameReport, RtxRun/PlayedRun, RtxSettings derivation, classmasks,
  pixels (composite and resample against GL's blit and the land alpha), GlMapOverlay (upstream's
  path moved whole, row-origin conversions checked), GlGround, GlOffscreenView (upstream RTT
  nodes, `getCopy` frame lag), characterpreview (extent, pick, camera follow), globalmap origin
  arithmetic.**
- **Frame path**: no per-frame heap allocation found in RtxRenderer::renderFrame, FrameDescriber,
  SkyReader::read/describe, RippleEmitters, DebugWalk (lists refilled), FrameTimer (fixed title
  buffer), NoteScope (stack-formatted) or ViewQueue (swapped, kept vectors). The intersection
  hook's GL cost is one empty virtual call per visited drawable.

### 09 — Fork hunks in upstream code outside the renderer folders

- The five checks (`-Wdouble-promotion`, `-Wnull-dereference`, `-Wcast-qual`,
  `-Wsuggest-override`, `-Wzero-as-null-pointer-constant`) and their keep-clean hunks across
  opencs, essimporter, mwiniimporter, navmeshtool, the benchmark, components/esm3, esm4,
  esmterrain, nif, nifosg, resource, sceneutil, terrain, fx, mwmechanics, mwphysics, mwgui,
  mwworld and mwlua. Every promotion read leaves the value unchanged.
- `Q_MOC_INCLUDE` lines (same commit), the sol3 patch and its README note, and Boost's
  `typed_value::notify` instantiation with its single-file `-Wno-null-dereference`.
- `Crash::notNull`/`contract` replacements (`ContentModel::dropMimeData`,
  `Store<ESM4::Cell>::insert`, the moved-ref cell, `BindingsManager`).
- `Misc::StringUtils::toNumeric`/`floatPrefix`, and `Settings` reading through them: the grammar
  matches `from_chars`, there is a finite check on both paths, and no caller catches the old
  `std::system_error`.
- `Misc::Presentation` (`present`, `toFrame`, `toDrawable`, `shownScale`, `interfaceScale`,
  `cropToAspect`): odd sizes, rounding, Native, the minimised window. Also its readers: mouse
  mapping and warp (`MouseManager`), Lua `screenSize`, camera bindings, interface layout and cursor
  fit (`WindowManager::layOut`, `fitCursors`).
- The SDL3 port of the input wrapper: event coverage, pixel density, relative mouse, warp
  compensation, wheel (`integer_x/y`, `x/y`), text input start and stop per window, keycode
  semantics under SDL 3.4 (unmodified, `latin_letters`), gamepad open by instance id, sensors,
  display orientation. Also `GraphicsWindowSDL`, `imageToSurface` (RGBA8888 matches the SDL2
  masks), oics, androidmain, the launcher's `initSDL`/display count, and `ControllerManager`
  mappings and labels.
- Settings categories (`video`, `general` hang seconds, `RTX`), `settings-default.cfg` text, the
  l10n and `.ts` additions and the removed `WindowModeHint`, the RTX tab in the settings layout, the
  launcher's RTX controls, and `declineUnsupported`/`decline` with `Renderer::support`.
- `PingPongCanvas` gamma outside multiview (the extra resolve draw, `resolveStateSet`,
  `READ_DRAW_FRAMEBUFFER`) and both shader variants.
- Optimizer child order (`MergeGeometryVisitor` index-ordered groups and stable sorts,
  `MergeGroupsVisitor` vector with dedup). Saved particles' first frame (`ageSavedParticles`,
  `partsys->update` moved after the programs). `StateSetUpdater::getGeneration`, `LampBody`,
  `vertexrules.hpp`.
- Visibility gates (`build`, `mark`, `reset`, `update`, `settle`; `VisibilityRun` ways and frames,
  exception scope), and `ScriptManager` building and updating them.
- `World::moveObject` jumps (`moveObjectBy` passes `false`, actors pass `false`, outright
  placement passes `true`), `noteHourWritten` with `DateTimeManager::jumps` (held hour, midnight
  wrap, float spacing), `MoonModel::phaseEighths`, `WeatherManager::holdWeather`/`mHeld`, and the
  `SkyState` lift with `Sky::sunUp`/`sunDiscAlpha`/`sunDirection` against the code they replace.
- RT integration hooks: `EngineHost`, `FrameClock`, `scriptMessageBox`, `SceneManager::
  setShadersEnabled`, `Shader::AutoMapRules`/`MapVisitor` and the `ShaderVisitor` refactor (unit
  order as upstream), `Terrain::ObjectStorage`/`PagedCellRef`, `PaintedTexture`, `StableIdentity`,
  `OffscreenFraming`, `LightSource` controller/source radius, `RigGeometry` getters,
  `Skeleton::markReached`, `Terrain::World::getActiveGrid`, and Lua post-processing/debug
  bindings without a chain.
- Not reviewed here, because they live in `apps/openmw/mwrender`: the `NightDaySwitch` first frame,
  `GroundcoverShapes` and the map tile's land.

### 10 — apps/rtxtool (harness) and tools/omw + ./omw, omw.cmd (driver)

- **instruments/ knows no world.** No `apps/openmw` or `MWWorld`/`MWBase` header in any instrument. They read only the core's scene and texture types (`scenedigest`, `contactsheet`) and the renderer's frame result (`framehashes`, `frametimes`). `model/` includes `instruments/` and never the reverse. `openmw-rtxtool-lib` reaches only `rtxrun.hpp` and `framereport.hpp` of the game side, and both include only `components/rtx`.
- **Frame-time statistics** (`frametimes.cpp`): nearest-rank median, p95 and p99 (ceil(q·n), clamped), best and worst, and the mean, after an in-place sort that runs after `writeFrameTimes` has written the unsorted series. Zone shares are total over measured frames. Zones opened several times in one frame are summed into one sample. `Arrivals` keeps the three worst frames correctly.
- **MeasureWindow**: the opening rule, the warm-up count that skips paused frames, the stall clock, and the closed-span shift (frame N's `Frame`/`Update`/`Sleep` with frame N−1's renderer figures), applied the same way to crossings and arrivals. The prediction in `getMeasuredIndex` agrees with `take`.
- **Measurer**: results from frames before `mFirstMeasured` are excluded. The drain at `finish` brings every measured frame's answer in. Film frames are numbered at trace time. Rows are reserved for the longest stop. `reuseKeeping` empties the kept series.
- **FrameHashes `against`**: the stretch-wise lookup, unmatched counted in both directions, a view only the reference drew, the denoised-composed exception, the upscaled picture reported but not judged, configuration mismatches, and the findStillMoved gating. Duplicate views are refused upstream (`chooseViews`).
- **The repeat chain** (`repeat.py`): alternating hold, each run compared with the one before, the exit status 3 mapping (`sDifferedStatus`), and logs kept on a difference. It does what AGENTS.md says.
- **Noise maths** (`compare.cpp`): `measureError`'s p99 by histogram (ceil 99 %), the firefly ratio with its luminance floor, the separable clamped Gaussian with radius ⌈3σ⌉, `noiseBarFramesAfter` for the cut and flight legs, sample-offset alignment between sides (`stopsASide`, `skipped`), and `ReconstructionRequest::unfiltered` deciding the shared bar.
- **CardWatch, Nvml, AmdGpu**: the cursor on the process samples, the lock-free `start`, the windows between places, NVML's clock and throttle bits, and amdgpu's `pp_dpm_*` parse.
- **ThreadCounters**: group read layout, reset-then-baseline of the times, hybrid groups, and scaling for multiplexing.
- **CameraPath, Cruise, CameraTrack, SkyCrossing**: centripetal Catmull–Rom tangents and Hermite coefficients; the ease integral and its continuity at both joins; the Fritsch–Carlson limiting; crossing reversal. `Stand::approachFrom` step count against `CameraDriver::fly` stepping between measured frames.
- **Driver**: flavour grammar (closed set, own-flavour verbs, buildless verbs); configure stamp and digest; `-k 0` builds raising `CalledProcessError` into exit 1; every gate step checked for failure; `test`/`timing`; `kernels` (SPIR-V walk, tuple enumeration, `--against` keys); `perf` (fifo, task-clock, off-CPU elevation, report parse that skips the total line); downloads (HTTPS-only redirects, partial files, digests); `omw.cmd`'s launcher and exit code; `presets.test_environment` inheritance.

### Review 11: crash catcher, platform, debug, MyGUI backend, CI, presets, files/rtx

- `crashnote.cpp`: the gate (Idle/Busy/Ending), `beginReport`/`endReport`/`finalReport`/`takeForFault` across
  signal handlers and threads, slot claiming and release, the seqlock's odd/even reading, `readNotes` bounds
  (at most `sNoteThreads` copies, the size check, a kind clamped to `Crash`, a faulted dump keeping no other
  report's kind).
- `crashpadclient.cpp`/`crashpadclientposix.cpp`/`crashpadclientwin32.cpp`: install order and its rollback of
  the page, `SIGUSR2` blocked before `finalReport`, `errno` kept in both handlers, the bounded first-chance
  wait on Linux and Windows, the TLS-callback terminate hook and stack guarantee, Linux's `CRASHPAD_NOTE`
  `--undefined` and the `pthread_create` wrapper object, `-Wa,--noexecstack`.
- `crashpadmonitor.cpp`: watch/handler/main-thread split and its shutdown order, the hang watch starting at
  the first frame and reporting once a stall, `endIfStillStalled`'s recheck, the summary written from the
  snapshot (report id set before user streams on all three systems), dump paths per system (`pending`,
  `reports`).
- `crashpage.cpp`, `sharedmemory*.cpp`: lock-free words only, the path length published after the bytes and
  clamped on read, POSIX unlink on open, Windows `Local\` namespace.
- `crashmonitorarguments.cpp`, `crashsummary.cpp`, `crashpackage.cpp`: argument round trip, an allocation-free
  `terminateReason`, the zip writer (sizes, CRC patched at offset 14, Zip64 limits, UTF-8 flag,
  rename-into-place), the URL budget.
- `components/debug/debugging.cpp`: the catcher installed before the configuration, the log reopened for
  append so the monitor's lines are not overwritten, the hang watch ended once the application returns or
  throws. `debugdraw.cpp`, `gldebug.cpp`, the myguiplatform hunks: covered by the accepted diff (null-deref
  and zero-as-null checks, SDL3, the seam).
- `components/platform`: `process*.cpp` (executable path, wide command line, `isRunning` semantics,
  performance cores, huge pages), `thread*.cpp` (stop state, callbacks under the lock, join on destroy,
  terminate handler copied), `fifo*`, `library*` (search scope), `localtime*`.
- `components/myguirtx`: texture lifetime (`makeTexture` empties in place, `TextureHandle`), slot
  take/drop on resize, per-frame vertex and batch buffers cleared and refilled, `VertexBuffer` reuse,
  additive blending around one layer, L8/L8A8/RGB widening, `SharedTexture` seeing a new image or modified
  count (checked against the video player's double buffer), `PaintedMirror` sending only the painted
  rectangle.
- `.github/workflows/ci.yml`, `daily.yml`, `sanitizers.yml`, `dependabot.yml`, `actions/openmw-deps`: least
  permissions, pinned actions, ccache keys that do not prefix one another and are saved on master only,
  per-leg artifact names, the daily issue logic, `$/` local action references (CI on `xorza/openmw-rtx` is
  green with them).
- `CMakePresets.json`: inheritance order (asan/tsan's empty `OPENMW_CXX_FLAGS` beats linux's `-Werror`),
  asserts kept in `debug`/`full`, `-Z7` for ccache, the test preset's shuffle, no-tests error and timeout.
- `extern/crashpad.cmake`, `crashpadpatch.cmake`: pinned hashes, the libcurl block replaced by name or the
  configure fails.
- `files/rtx`: every suite id in `benches.cfg` exists in `views.cfg`, no duplicate view ids; `keys.lua`,
  `sky.lua`, `hour.lua` logic.
