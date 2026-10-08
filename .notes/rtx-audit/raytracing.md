# Audit: the ray tracing core and the scene on the device

Scope: acceleration structures, instance and geometry tables, the shader binding table, ray flags,
the any-hit cutout path, rays per pixel, and lamp sampling. Everything here comes from reading the
code. Nothing was built or run. Each figure marked *est.* is derived from the code and
`measured.json`, not measured, and says what would confirm it.

Measured context (`measured.json`, seyda-neen-ship): frame 6.83 ms; trace 2.78, tlas 0.41, refit
0.17; 64,179 placed instances, 39,905 of them cutouts; structureBytes 155.6 MB (live 145.8);
tableBytes 149.2 MB; compaction finished (compactable 0). At **balmora-mages-guild (interior, 1,221
placed)**: tlas **0.25 ms**, refit 0.17, structureBytes and tableBytes the same as outside.

## What is already right (do not change)

- BLAS: `PREFER_FAST_TRACE`, compaction requested for every static mesh and copied tight under a
  fixed per-placement budget (`bottomlevelstore.cpp:31-35`, `:454-525`). The run finished
  (`compactableBytes` 0). Only deforming meshes take `ALLOW_UPDATE`, and they are refit only when
  their pose changed (`SceneDesc::pose` → `MeshTable::getDeformed`). A rota rebuilds one body whole
  per frame (`sceneacceleration.cpp:178-202`). This is NVIDIA's dynamic-BLAS guidance point by point
  ("update after limited deformation… rebuild periodically… distribute rebuilds over frames").
- TLAS: `PREFER_FAST_TRACE`, always rebuilt and never updated. NVIDIA: *"For TLAS, consider the
  PREFER_FAST_TRACE flag and perform only rebuilds."* Skipped where nothing is owed.
- Static positions are not kept: `ALLOW_DATA_ACCESS` and position fetch, with the build input staged
  and released (`bottomlevelstore.cpp:111-124`).
- Each BLAS build has its own scratch, and the builds go in one call. The structures sit in pooled
  storage blocks (NVIDIA: "pool small allocations", "unique scratch").
- SBT: 8-byte records, 30 hit records (`tracerecords.h`). The pipeline promises `SKIP_AABBS` and
  recursion depth 1. The payload is already packed (halves, rgb9e5, flags).
- Light rays use `TerminateOnFirstHit` wherever no penumbra is read. Opaque geometry reaches no
  any-hit: the geometry is built `OPAQUE` and only cutout, translucent or additive placements are
  forced non-opaque (`sceneacceleration.cpp:397-398`). This is NVIDIA's "enable any-hit only for
  geometries that need it".

Not proposed, because the field's guidance or the posture rules them out:

- TLAS update/refit. It goes against NVIDIA's advice, and frames with arrivals would still rebuild,
  so it lowers the median and leaves the worst frame unchanged.
- Two TLASes (static and dynamic). Every ray type would pay two traversals, and the worst frame (a
  cell arriving) still rebuilds the static one.
- Opacity micromaps, async compute and SER, which are on the declined list.

---

## Findings, ranked

### 1. Static normals and tangents are stored once per frame slot

- **What.** `mNormalTable` and `mTangentTable` are `SlotBlocks`, two copies. Every static mesh's
  normals and tangents are written into both (`scenebuffers.cpp:189-195`, `scenebuffers.hpp:394-402`).
  Only skinned and morphed meshes change, and their positions already live in their own per-slot
  table indexed by `mBindOffset` (`sceneacceleration.hpp:287`).
- **Fix.**
  - One copy of static normals and tangents.
  - Posed normals and tangents in per-slot blocks indexed by `GpuMesh::mBindOffset`, exactly as
    poses are.
  - `triangleNormals` / `triangleTangent` (`geometry.glsl:130`) select the block table and the base
    by `mBindOffset != NO_RUN`. This is a select, not a branch, so the shader keeps one path.
  - `SkinPass` writes the posed copy only.
- **Saving.** 16 B per static vertex. *est.* ≈ 40 MB at seyda-neen-ship, from tableBytes 149 MB at
  ~52 B/vertex across uv+colour+2×normal+2×tangent ≈ 2.6 M vertices. The harness should print the
  vertex count. Pictures are bit-exact.
- **Proof.** `shot --against` must report zero pixels moved; `repeat`; `check`; `kernels` names the
  hit kernels.
- **Kind.** Straightforward.

### 2. Memory the reports do not count, and held at high-water marks

`readStats` (`devicescene.cpp:194-214`) reports `mBuffers + mSkinTables`, the index blocks, and BLAS +
TLAS storage.
Not counted anywhere:

| buffer | where | sized to |
|---|---|---|
| poses, 2 copies | `sceneacceleration.hpp:287` | deforming vertices × 12 B × 2 |
| TLAS rows, 2 device copies + host vector | `sceneacceleration.cpp:48-49`, room 2^18 | **16 MiB each**, reserved up front (48 MB) |
| TLAS scratch | `sceneacceleration.cpp:469` | build scratch for 262,144 instances |
| BLAS build scratch `mScratch` | `bottomlevelstore.cpp:252` | the largest single build ever recorded; grows, never shrinks |
| staged arrival positions `mArrived` | `bottomlevelstore.cpp:127` | the largest arrival ever; never shrinks |
| refit scratch | `sceneacceleration.cpp:143` | every refittable body at once |

- **Fix, step 1 (straightforward).** Report each line above in `SceneStats`.
- **Fix, step 2.**
  - Bound `mScratch` and `mArrived`: record the whole-scene build (`DeviceScene` constructor /
    `Rebuilt`) in fixed chunks. For example, 32 MB of scratch per `vkCmdBuildAccelerationStructures`,
    with a `barrierAfterBuild` between chunks reusing the same scratch.
  - Load allocates no more than a frame does (AGENTS.md). Today the first full build decides the
    size of two device buffers for the life of the scene.
- **Saving.** Unknown until counted. The scratch alone for a whole-region `PREFER_FAST_TRACE` build
  is plausibly tens of MB.
- **Proof.** `scene` prints the new lines. Chunking must leave `shot --against` bit-exact, since
  build input and flags are unchanged.

### 3. Groundcover: ~40k cutout instances in every ray's path. Measure before choosing

- **What.**
  - The ring stands one placement per plant part (`cellplacer.hpp:99`).
  - Groundcover is `InstanceClass::Static` (`classmasks.hpp:26`), so every ray type meets it: eye,
    sun and lamp shadow, bounce, the ambient ray to `mReach`, water legs, fog columns.
  - Each meeting is an any-hit or candidate-loop invocation with a 7-deep dependent load chain
    (finding 5).
  - Upstream's rasterizer draws groundcover only for the eye: it is not in the shadow-casting mask
    (`glworld.cpp:94-112`), not in CPU raycasts, and in reflections only at detail 5.
  - It is also most of the 64k-row TLAS rebuilt every frame (0.41 ms).
- **Step 1, a diagnostic (no code).** `./omw release bench --views=seyda-neen-ship` with
  `[Groundcover] enabled = false` against the same leg with it on. That bounds what any groundcover
  change can win in `trace` and `tlas`. Everything below is worth doing only if that bound is large.
- **Option A: merge plants into BLAS buckets per cell and material.** This is NVIDIA's *"Consider
  merging BLASes when instance world-space AABBs overlap significantly"*, and RTX Remix's
  `AccelManager::mergeInstancesIntoBlas` / `BlasBucket` (dxvk-remix `rtx_accel_manager.h`) does it.
  - Implementation: one BLAS per cell with one geometry per plant, using `transformData` over the
    shared vertex blocks.
  - Benefits: the TLAS drops by ~40k instances, and a ray entering a grass patch stops paying an
    instance transition per plant.
  - **Memory cost:** no triangle sharing across plants. *est.* plants × triangles × ~30 B (the
    observed 145 MB / ~4.5 M triangles), plausibly +50–150 MB against ~15 MB of per-plant rows today.
  - The hit's identity changes: `gl_GeometryIndexEXT` plus a per-geometry row, an
    `InstanceRecord`/`GpuInstance` change.
  - **Experiment**, and only if the diagnostic says grass is a large share.
- **Option B: let some rays skip grass.** Option B needs a decision from you, so it is not a fix to
  make. The lamp shadow ray, the bounce's far-hit rays (sun, lamp, ambient) or the fog's lamp rays
  would not meet grass, which upstream's rasterizer never shadows. The obstacle: all eight
  instance-mask bits are taken (`scene.h:352-380`, `437`), so a groundcover class needs a bit freed,
  for example by folding `MASK_PARTICLE` into `MASK_EFFECT`. It also changes the look (no grass
  contact shadows from lamps).

### 4. Sky and lamp shadow rays at the eye's hit run in closest-hit mode

- **What.**
  - `gather(..., split = true)` passes `nearest = split` (`shading.glsl:200-…`, `lights.glsl:61`,
    `:678`), so both shadow rays of every primary solid pixel are traced with `gl_RayFlagsNoneEXT`,
    without `TerminateOnFirstHit` (`traversal.glsl:776`).
  - Their `RTX_RESOLVE` loop then evaluates every cutout candidate in front of the current nearest,
    which in grass and canopy is many.
  - The reason is documented and sound: the penumbra needs the nearest occluder (a roof at 500 units
    must not blur a hand's contact shadow).
  - NVIDIA and AMD both name first-hit termination as the first shadow-ray optimization.
- **Experiment (bias-free for the bit).**
  - Penumbra width scales with occluder distance, and the shadow denoiser's reach is capped
    (`ATROUS`/SIGMA levels), so past a distance `dMax` the exact occluder no longer changes the filter.
  - With `dMax = maxPenumbra(footprint) / tan(halfAngle)`, trace `[0, dMax]` nearest. Only on a miss,
    trace `[dMax, reach]` with `TerminateOnFirstHit` and report `dMax` as the occluder.
  - The bit is identical, and the penumbra is identical wherever it is below the cap.
- **Ceiling first.** One `bench` leg with `nearest` forced false. Do not ship that: it is the wrong
  penumbra.
- **Saving.** Unknown. Only shadowed pixels gain, and lit pixels traverse to the end either way.
- **Proof.** `noise` (`--still`, then the suite), `shot --against`, `kernels`.

### 5. The cutout candidate's load chain is seven dependent loads deep

`candidateStops` (`traversal.glsl:417-493`) per candidate:

1. instance row
2. mesh row and material row
3. index block address (`geometry.glsl:33`)
4. three indices
5. uv block address (`geometry.glsl:99`)
6. three uvs
7. texel count (`coneLod`), then the sample

The `GpuMaterial` row is 108 B (`scene.h:1122-1206`). The any-hit reads `mDiffuse`(0),
`mAlphaReference`(4), `mOpacity`(8), `mTextureTransform`(60-76, crossing a 64 B boundary) and
`mFlags`(104). At a 108 B stride that is up to four 32 B sectors per material, unaligned to rows.

- **5a (straightforward).**
  - Reorder `GpuMaterial` so the any-hit's 32 B (`mDiffuse, mAlphaReference, mOpacity, mFlags,
    mTextureTransform`) come first, and pad the row to 128 B (`static_assert` it).
  - The table is a few thousand rows, so the padding is nothing.
  - One sector per candidate instead of up to four.
  - **Proof:** `shot --against` bit-exact; `kernels` moves every kernel that reads a material, as
    expected.
  - Gain unknown, small: the OMM result says any-hit is not dominant.
- **5b (experiment).**
  - A per-primitive UV run for meshes whose materials are cutouts (`3 × vec2` = 24 B a triangle,
    float, so exact). Reached from the instance row (move `mVertexOffset`/`mIndexOffset`/the run's
    offset there).
  - The chain becomes instance → uvs → sample: three levels instead of seven.
  - Memory: cutout triangles × 24 B.
  - Field precedent: "minimize indirections for alpha test" (NVIDIA: "root descriptors for index and
    vertex buffers", i.e. fewer hops).
  - **Proof:** `shot --against` bit-exact; `bench` on canopy and grass places; `kernels`.
- The declined OMM measurement ("no faster") argues the any-hit is not the bottleneck. Do 5a, and
  5b only if a profile of the trace (`nsys` shows the hit-group share) says so.

### 6. Vertex and index formats

- **Indices: u32 → u16, mesh-local.**
  - NIF caps a shape at 65,535 vertices and a terrain chunk is 65×65. `MeshResolver` only refuses
    above a `VERTEX_BLOCK` (256k), so census first: the largest `MeshRange::mVertices.mCount` per
    place.
  - Above 65,536: split the mesh at load (rare) rather than keep two index paths.
  - Build: `VK_INDEX_TYPE_UINT16` (`structurebuild.cpp:68-72`).
  - Shader: either enable `storageBuffer16BitAccess`, which is not requested today, or read two u32
    words and shift. Rows start 3-index aligned, so no mixed path is needed.
  - **Saving** *est.* 25–30 MB (half the index blocks, est. 50–60 MB at ~4–5 M triangles) and half the index bytes per candidate.
  - **Proof:** `shot --against` bit-exact; `repeat`; `check`.
  - **Kind:** straightforward once the census holds.
- **Normals: float3 → octahedral 2×16 bit.**
  - Error ≤ 0.005° (Cigolle et al., JCGT 3(2) 2014). The tangents already take this route
    (`TANGENT_COORDINATE_BITS`, `scene.h`), so the representation has precedent here.
  - **Saving** 8 B a vertex in one copy after finding 1, *est.* ~20 MB.
  - It changes pictures at the 1e-5 level: `shot --against` names every view, `noise` must not move.
  - **Kind:** experiment, and a precision call that is yours.
- **Vertex colours: float3 → RGBA8, only where lossless.**
  - Terrain's VCLR is 8-bit, and NIF float colours are usually exporter `k/255`.
  - Census first: does every colour equal `k/255` as the host stored it?
  - Decode through a 256-entry constant table, not `unpackUnorm`, whose division the Vulkan spec
    allows 2.5 ULP. That keeps it exact by construction.
  - **Saving** 8 B a vertex, *est.* ~20 MB, and 24 B per hit.
  - **Kind:** experiment gated on the census. If any content colour is not `k/255`, drop it: a second
    path is not worth 20 MB.
- **UVs stay float2.** Tiled terrain coordinates exceed what a half keeps.

### 7. `sWorldPlacementRoom = 2^18` reserves for three times the largest place

`vulkanrenderer.cpp:69`. TLAS storage and scratch are made for 262,144 instances, plus 2 × 16 MiB of
device rows and a 16 MiB host vector (`mRowTable.reserve`), from the first frame, in an interior too.

- **Fix.** The TLAS builds over the placed rows alone, so size the room for that count with modest
  headroom (e.g. 2^17).
- **Saving.** *est.* half of row and TLAS reservations (≥24 MB), once counted (finding 2).
- **Proof.** `check`; `scene` bytes; a crossing in `bench` must still not grow the TLAS mid-run.
- **Kind.** Straightforward, a number.

### 8. The exterior's structures and tables stay resident inside interiors

- **What.** `dropSlots` keeps the held cells (`cellplacer.hpp:132`). The guild keeps 145.6 MB of live
  BLAS and 150 MB of tables for an interior placing 1,221 instances. It is deliberate (a fast return
  outdoors), but the cost is not stated anywhere.
- **Decision.** Keep everything (today), keep only the cells within the grass and active band, or
  release all and re-adopt. Re-adopting costs the ring's incremental arrival frames on exit.
- This is a memory-against-exit-stall trade for you to make.

### 9. Every geometry carries `NO_DUPLICATE_ANY_HIT_INVOCATION`

- **What.** `structurebuild.cpp:79` sets it on every mesh. The DXR spec says why the default allows
  duplicates: *"By default, the system is free to trigger an any hit shader more than once for a given
  ray-primitive intersection. This flexibility helps improve the traversal efficiency of acceleration
  structures in certain cases."*
- **Where duplicates matter.** Only for the order-free sums: `MEET_WALK_PAST` through translucent,
  fading or medium surfaces, and `mediumAlong`. A cutout's confirm, and `MEET_BY_CHANCE` keyed by
  instance and primitive, are idempotent under duplicates.
- **Ceiling first.** One `bench` leg with the flag off everywhere. Ship nothing from that leg: panes
  could double-count.
- **If the ceiling is real.** Set the flag only on meshes that can ever be met as see-through: actor
  meshes (fades), and materials with blend or alpha controllers.
- **Kind.** Experiment, low prior.

### 10. Measure what `ALLOW_DATA_ACCESS` costs the BLAS

- **What.** Position fetch replaced a 12 B/vertex position buffer (`bottomlevelstore.cpp:111-114`).
  Khronos says the flag makes implementations "retain vertex position data at sufficient precision",
  which may grow the structure. Nobody publishes the size.
- **Experiment.** `vkGetAccelerationStructureBuildSizesKHR` with and without the flag over one
  place's meshes, then compaction sizes both ways. That is a size query, not a build.
- **Why.** If the BLAS grows more than 12 B a vertex, the trade has turned. Expected outcome: it
  holds, but it is unverified.

### 11. Smaller items

- **`GpuInstance::mMotion` (48 of 64 B, `scene.h:629`) is the identity for nearly every row.**
  - Read by the primary hit's `motionOf` only.
  - Fix: a 16 B row plus a motion index into a sparse moving table.
  - Saves ~6 MB at 64k rows × 2 copies. The per-hit gain is nil: candidates read only the first
    16 B.
  - Straightforward, low value.
- **Actors' parts are separate BLASes and instances.**
  - A crowd is hundreds of tiny refits a frame (refit 0.17 ms in the guild and at the ship alike).
  - Merging one actor's parts into one BLAS with one geometry per part means one build per actor and
    one TLAS row. It needs the per-geometry identity of finding 3A.
  - Experiment; measure the refit zone under `nsys` first, since tiny builds are latency-bound.
- **Ray queries carry no `gl_RayFlagsSkipAABBEXT`.** The pipeline promises it, but queries cannot
  take the pipeline flag. There are no AABB geometries, so expect nothing on NVIDIA. Skip unless
  free.

---

## Rays per primary solid pixel, as the code stands

| ray | where | mode |
|---|---|---|
| primary (+ ≤4 peel layers) | `visibility.rgen:185-215` | pipeline `traceRayEXT`, back-face cull |
| sky shadow (sun or a moon) | `gather` split | **nearest** (finding 4) |
| lamp shadow (RIS-held lamp) | `gather` split | **nearest** |
| bounce | `bounceArriving` (`shading.glsl:1012`) | query, closest, full `resolve` |
| bounce hit: ambient | `surfaceAmbient` | to `mReach` outside at rate ½; 140 units inside |
| bounce hit: sun | `gather` PATH_INDIRECT | first-hit, rate ½ outside |
| bounce hit: lamp | `gather` (8 candidates) | first-hit |
| water: reflection, refraction, bed, shore | `water.glsl`, `visibilityhit.rchit:221` | where water |
| fog columns (2 per column), sprite depth | `fogdepth.rgen`, `spritecomposite.rgen` | per column / per shown pixel |

That is about 7 traversals a pixel before water and air. The rates are already measured and
documented (`look.h:440-590`). Nothing here re-proposes them. The cheap levers left are the mode of
the two eye shadow rays (4), what grass is met by (3B), and fewer indirections per candidate (5).

## Suggested order

1. Finding 2 step 1 (count), plus the row count beside `mPlaced`. Also the groundcover-off `bench`
   leg (3) and the ceilings for 4 and 9. All measurement, no picture change.
2. Finding 1. Structural, bit-exact, ~40 MB.
3. Finding 6 indices (after census), finding 5a, finding 7.
4. Whichever of 3A, 4, 5b and 9 the step-1 numbers justify. Finding 8 and 3B are your decisions.

## Sources

- NVIDIA, *Best Practices for Using NVIDIA RTX Ray Tracing (Updated)*:
  https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/
- AMD GPUOpen, *RDNA Performance Guide*, ray tracing section ("Trace as few rays as possible",
  first-hit for shadows, rebuild the TLAS every frame): https://gpuopen.com/learn/rdna-performance-guide/
- NVIDIA dxvk-remix, `AccelManager::mergeInstancesIntoBlas` / `BlasBucket`:
  https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_accel_manager.h
- Microsoft DirectX Raytracing spec, geometry flags (`NO_DUPLICATE_ANYHIT_INVOCATION`):
  https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html
- Khronos, *Introducing Vulkan Ray Tracing Position Fetch Extension*:
  https://www.khronos.org/blog/introducing-vulkan-ray-tracing-position-fetch-extension
- Cigolle et al., *A Survey of Efficient Representations for Independent Unit Vectors*, JCGT 3(2)
  2014: https://jcgt.org/published/0003/02/01/
- Vulkan spec, precision of `OpFDiv` (2.5 ULP) and inactive instances (reference 0):
  https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
