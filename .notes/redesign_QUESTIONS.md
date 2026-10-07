# Questions on `redesign.md`

## 3. Section 8.1: the camera that spins

**Item.** Section 8.1, the camera that sometimes turns fast in the game.

**Why it needs a call.** The diagnostic lines are in the code at `Debug::Warning`, with TODOs to remove
them. The fix waits on a log of a spin from your game. Run the game, wait for a spin, and send
`openmw.log`'s `Mouse diagnostic:` lines from around it.

**What it blocks.** Section 8.1's fix and the removal of the diagnostic lines.

## 4. Section 6.1: the constellations drawn turned and squashed

**Item.** Section 6.1's "The constellations are drawn turned and squashed". The plan's target is an
affine fit of each patch's sheet axes, as `fitSheet` does for the cloud cap.

**Why it needs a call.** The vanilla patches are not flat and their sheets are not affine. Measured
on `sky_night_02.nif` (2026-10-07), each patch lies on the dome, at a radius of 1870 to 2000. The
best affine fit of the sheet coordinates still misses a vertex by this much of the sheet:

| Patch | Vertices | Orthographic fit | Central fit |
|---|---|---|---|
| mage | 7 | 0.012 | 0.015 |
| warrior | 12 | 0.34 | 0.37 |
| thief | 6 | 0.10 | 0.11 |
| nebula 02 | 20 | 0.26 | 0.30 |
| nebula2 02 | 28 | 0.29 | 0.38 |
| nebula3 02 | 15 | 0.24 | 0.27 |

The meshes are unwrapped triangle by triangle, so only their triangles say where a sheet lands.

| Option | What it does | Cost |
|---|---|---|
| **A. Bake each patch's sheet coordinates** (my pick) | At load, the reader finds, for each texel of a small grid over the patch's disc, the triangle the direction crosses and its sheet coordinates. The shader reads that map, then the sheet. The outline is the mesh's, too. | A data texture made at load, which the texture table has no kind for yet (the sprite light bake is the nearest), and one more fetch a patch on a ray that meets one. |
| B. Affine fit (the plan's target) | Fits each patch's axes and offset, and stores them in the patch record. | Leaves the errors above: four of six patches stay visibly wrong. |
| C. The triangles in a buffer | The miss shader finds the triangle a ray crosses, per patch. | About a hundred triangles, looped over on every sky pixel a patch covers. |

**What it blocks.** Only this item.

## 5. Section 6.1: the twin fold ignores the attributes it throws away

**Item.** Section 6.1's "The twin fold ignores the attributes it throws away". The plan's target is
a twin folded only where its corners carry equal coordinates and colours, and a mesh with a kept
twin flagged so that a ray takes the face its winding faces.

**What the count found** (2026-10-07, eight places: Balmora, Vivec, the ship, Ald-ruhn, Sadrith
Mora, the guild, Arkngthand, Mournhold). 81,111 twin pairs had equal corners. 101 mesh reads held
593 pairs with a differing one: 185 differ in coordinates and 81 in colour — a bench, rocks, tents,
the skiff, railings, the Vivec waterspouts. A back face of these shows the front's mapping.

**Why the target does not hold.** A ray that carries light meets both faces of everything
(`facingFor`), so a kept twin is met twice at one depth, and a cutout's or a pane's light is taken
twice. An instance can only switch culling off (`TRIANGLE_FACING_CULL_DISABLE`), never on, so no
flag on one mesh makes every ray take one face.

| Option | What it does | Cost |
|---|---|---|
| **A. A back face per kept triangle** (my pick) | The fold keeps one triangle as now, and records the dropped twin's corners where they differ. A mesh with any such twin carries a second index buffer; a hit on the back face reads its corners from it, by a select of the address and no branch. | The fold's output, the mesh table, the device upload, the mesh record and every shader that reads a triangle's corners. A second index buffer only for the meshes that need it. |
| B. Leave it | The back face of 0.7% of twin pairs shows the front's mapping. | The defect stays. |

The pocket rule (`dropPockets`) has the same blind spot, and option A answers it the same way: the
dropped wall's corners become the kept wall's back face.

**What it blocks.** Only this item.

## 6. Section 6.2: the rasterizer's local-map tiles keep their render targets

**Item.** Section 6.2's "Rasterizer local-map tiles keep their render targets". `GlTileView` holds
its render-to-texture node, with the camera, the framebuffer and the `D24S8` buffer, for as long
as the map segment stands. Upstream's `LocalMap::cleanupCameras` took each node out of the graph
once it had drawn, and kept only the colour texture in the segment.

**Why it needs a call.** Section 6 keeps a `[perf]` item only with a measurement, and this one is
the GL path's video memory, which nothing here can draw: the harness drives the ray tracer alone,
and AGENTS.md keeps the game window out of a rendering check. The cost is one framebuffer and one
depth buffer a mapped segment, at `[Map] local map resolution` squared: a megabyte a segment at
512, and a session explores hundreds.

| Option | What it does | Cost |
|---|---|---|
| **A. Free them as upstream did, checked by you in the game** (my pick) | After its draw the tile keeps its colour texture and lets go of the node; a redraw makes a new node and points the interface's texture at its texture. You run the GL renderer once with the map open over a few cells, and send the log. | A change of ownership in `GlOffscreenView` that only a played session checks. |
| B. Leave it | The GL path keeps a framebuffer a segment. | The memory stays spent. |

**What it blocks.** Only this item.


## 7. Section 6.5: the upscaler's 64-lane subgroups

**Item.** Section 6.5's "FSR runs in full floats with the driver's wave size", the wave half. The
half-float half is done: measured and declined, and `fsrcallbacks.glsl` says why.

**Why it needs a call.** The SDK's own host asks for its 64-lane permutations on a device that runs
both 32 and 64 lanes, which RDNA does. NVIDIA runs 32 lanes only, so this card cannot take the
request, and the drm-shim device compiles every kernel and runs none. Section 6 keeps a `[perf]`
item only with a measurement, and no device here can make one.

| Option | What it does | Cost |
|---|---|---|
| **A. Wait for a measurement on RDNA** (my pick) | The item stays open until someone runs the default suite on an RDNA 2 or later card, with and without `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` at 64 on the upscaler's pipelines. | Nothing now. |
| B. Ask for 64 lanes as the SDK does, unmeasured | The upscaler's pipelines ask for 64 lanes where `subgroupSizeControl` allows it for compute. | A change no device here runs, kept on the SDK's word. |
| C. Drop the item | The driver keeps its own choice. | A possible gain on AMD goes unmeasured. |

**What it blocks.** Only this item.
