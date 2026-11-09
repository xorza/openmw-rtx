# Review: the shaders and their use, and the content preprocessing suite

Scope: `components/rtxvulkan/shaders/`, `components/rtx/shaders/`, the passes and pipelines that dispatch them;
`components/rtx/preprocess/`, `components/rtx/prepare/`, `components/rtx/image/`, the mesh table and the made-image
hand-over; the two open issues (the water kernel's AMD register ceiling, the streaming frames on the Fullrest Repack);
and the CI failure of run 42553703270. Measured at `4ec617cb00` on the desk (high-end NVIDIA laptop GPU), the Steam
Deck (RADV, Mesa 25.2.8 in the build container) and the laptop with a low-end AMD integrated GPU (AMD's Windows
driver 26.8.1). Scratch measurements: `build-release/issue-stream2/`, `build-release/prep-memory/`; the tried water
patch: `build-release/review/water-media-registers.patch`.

## Part 1: proposal

### Root causes

1. **State carried across a traversal loop that only the code after the loop reads.** The water walk carries the
   water's fully resolved `Surface` (about 20–24 scalars) and some 35 pass-through values through the loop that holds
   its shore and path traversals, and resolves the path's hit fully inside the loop where the walk reads two fields
   (`visibility.comp:107,208-218`, `water.glsl:342`); media does the same (`media.comp:73,132`). Ray records are
   wider than a ray (`SceneQuery` 14 words, `RayAsk` 16, per-site constants made per-lane by `noRay`), and the
   fixed-order turn loops keep every ask live through every traversal. The kernel's own rule ("what the turn met leaves
   the loop only as it ends it") is broken by the walks.
2. **What a stage can do is decided at run time where it is known at compile time.** `resolve` compiles the relief
   march into the stored-solid stages that can only read the stored answer; every stage is compiled for all 48
   tuples though `bounce`/`unbounced` declare neither sun nor moons and the trace's dark tuples fold; 96 of 340 trace
   pipelines repeat another byte for byte.
3. **The last commit put a decode on the resolve path of every stage.** Packing the normal into the octahedral word
   (`4ec617cb00`) raised `direct`, `landing` and `bounce` by 16 VGPRs on AMD's Windows driver; `bounce`'s no-dark
   tuple now holds 144 against its ceiling of 128, a new gate failure. The three-corner decode sits in
   `committedHit`/`resolve` (`traversal.glsl:756-761`).
4. **The spill gate reads one driver.** `device.cpp:319` maps only AMD's Windows statistic names, so
   `noKernelSpillsPastItsCeiling` checks nothing on NVIDIA or RADV; NVIDIA's "Shared Memory Size" on stages that declare
   no `shared` (water and media 5.5–6.3 KB, panes 4.5–6.5 KB, landing 3.4 KB) is spill the gate never sees.
5. **Payloads are copied and buffers kept at their high water.** A chain hit passes over its bytes five times
   (zero-fill, read, header `erase`, digest, codec `assign`); a write copies twice, once under the lock the frame reads
   under; every thread keeps its largest payload and its largest chain-build scratch for the session (normal-map means
   at 12 B a texel: up to 268 MB a thread at 4096²); glibc keeps what 16 workers freed. Measured: the preparation's
   hands hold 2.66 GiB until it ends, and a fixed mmap threshold gives back 0.58 GiB of the peak and 0.81 GiB of the
   steady state.
6. **The preparation does play's work again and forgets what it did.** The manifest keeps only the current content
   list, so a profile switch is a cold walk; each store hit is read whole and dropped; facts are keyed per hand through
   a second preprocessor (19,803 specular asks for 3,118 entries, 5.95 GiB hashed).
7. **An entry's integrity is checked, its fit to the input is not.** A stored shape is used without checking its
   counts and indices against the mesh (a heap overflow in a release build); a manifest's count is trusted (a 128 GiB
   allocation at every start).
8. **Lifetimes follow the wrong key.** `PreparedImages` keys by image address and sweeps only entries nobody holds, so a
   chain made for a slot freed before it collected the chain is held for the session; a widened file's RGBA8 laid copy
   (22.4 MB at 2048²) stays with three owners after its BC7 chain replaced it, and each thread can lay its own.
9. **Failures are silent.** A failed store write is counted and never logged (the design says "logged once");
   `ContentStoreStats` is read by no report; a write dropped at the queue's bound leaves the model marked prepared.
10. **The streaming issue was misdiagnosed.** The spike is upstream's synchronous cell change (actors 36%, unload,
    navigator and scripts 19%, door markers 11%; the fork's code 6.7%, Bullet 0.7%), about 50 ms; the 100–130 ms
    frames are the kernel compacting memory under the harness's own huge-page tunable
    (`Platform::Process::restartOnHugePages`), which the game never sets. The fork's steady cost is
    `TracedTerrain::meetStatics` walking every land record (`getBounds`) on every focus ray, 0.13 ms a frame.
11. **CI downloads its Linux packages from a mirror at every run.** Run 42553703270's `linux-clang` and `linux-tsan`
    timed out at the 15-minute limit: `archive.ubuntu.com` answered the index and sent nothing for 13 minutes. The code
    passed on the other three build jobs.

### Target design

- **Kernels keep across a traversal only what the traversal's own loop updates.** A walk is handed geometric hits
  (`committedHit`'s position, rounding, footprint, distance) and carries the path's answer; the stage resolves once
  after the loop and re-reads what only the code after it needs from the stage record and images. Measured on the
  tried patch: water 248 → 193 on AMD (no-dark 218 → 177), 256 → 160 on RADV (4 → 6 waves); media 248 → 220 on AMD;
  the deck's water zone 1.98 → 1.56 ms; every channel's frame hash identical at seven views.
- **Compile-time facts are compile-time arguments.** `resolve` takes its relief source (march, stored, shift); a
  stage's pipeline table is keyed on the constants its module declares (`ModuleInterface::mSpecConstants`) and aliases
  the duplicates.
- **A passage ray is a record of what varies per lane** (`from`, `along`, `reach`, with reach 0 for no ray); flags
  and masks come from the site. One fixed-order helper returns the passage, from which the other answers derive.
- **The spill gate reads every driver's statistics** (NVIDIA's register count and its shared memory on stages that
  declare none, RADV's VGPRs and spilled VGPRs/SGPRs, AMD's as now).
- **A store read scatters into the header and a payload vector it moves to the consumer; a write moves its encoded
  vector into the queue outside the lock.** Large chain buffers come from one pool bounded by the builds running at
  once and are reused, never freed and allocated again; one `malloc_trim` when the preparation ends returns what its
  threads freed; the chain build's means are kept for levels one and down, level nought streamed through a three-row
  ring.
- **One preprocessor a preparation hand**, its facts and chains claimed once a run; the preparation probes the store
  (`holds`) and reads nothing it will not use; the manifest keeps every content list's entries, bounded by a last-seen
  run count, its paths in a string table stamped once each.
- **Every stored entry is checked against its input after the digest**, and a misfit is a counted miss and a discard.
- **A made chain is keyed by its file's name, and dropped when its file is gone**; widening goes into the chain pass's
  own scratch, and a pending widened slot draws the stand-in, so `layImage`, `LaidImageCache` and the three `mLaid`
  fields go.
- **Store failures are logged once and reported**; a write at `Disk` reach waits for room, one at `Memory` reach
  drops, and an empty queue admits any write.
- **CI's Linux jobs run in an image that holds their packages**, built from the release-base container the packaging
  already makes, so no job downloads from a mirror.

### Ownership and shared state

- **`ContentStore::mMonitor`**: kept. Many threads read and write one process-wide store; no single owner exists.
  The fix is what it guards (no copies under it), not the lock.
- **The preparation's monitor**: kept; it guards the claim set alone.
- **`PreparedImages`' entries, held by the slot and watched by the cache**: kept as a shared hand-over (the made
  image's lifetime is the slot's), but keyed and swept by name (root cause 8).
- **The laid copy held by the slot, the surface description and the prepared layer**: removed with the laid copy.
- **`osg::ref_ptr` images pinned by `FinestTexels` on 16 threads after a store hit**: the trade-off `FinestTexels`
  documents stands, but the pin is released when the pass is answered from the store, so the entry it keys can be
  swept.
- No `shared_ptr` is in the preprocessing scope.

## Part 2: plan

Batches, most impactful first; each leaves the build and the tests green. Whoever addresses an item deletes it.
Items marked **(decision)** wait for the user's answer; the decisions taken are recorded at their items.

### Batch 1: the water and media walks (fixes the AMD water failure)

Files: `components/rtxvulkan/shaders/trace/visibility.comp`, `trace/media.comp`, `lib/water.glsl`,
`lib/traversal.glsl` (`resolveAsked`'s comment), `apps/components_tests/rtxvulkan/trace/visibility/kernels.cpp`.

- [ ] `visibility.comp:107,208-218`: the walk carries the water's resolved `Surface` and resolves the path's hit in
      the loop → hand `stepWater` a geometric hit, carry the path's `SceneAnswer`, resolve once after the loop, then
      `storeStageSolid` and `stepEye` (tried patch E1).
- [ ] `visibility.comp`: arms, layer, tmin, the through-shares, origin and the water hit carried through the walk →
      re-read them after the walk from the stage record and images, `motionOf` after it (E3).
- [ ] `media.comp:73,132`: the same in media's walk; media resolves nothing it does not read (E2).
- [ ] `water.glsl:135`: `WaterRun::mPathAsk` is written and never read → delete; `mPlane` derives from `mFromBelow`.
- [ ] `kernels.cpp`: lower the ceilings to what lands (water 193/177, media 220/192 on AMD), so the saving is held.
- [ ] Tests: frame hashes of every channel at the sea views (`seyda-neen-ship`, `-underwater`, `-shore`, `-pier`,
      `-pond`, `vivec`, `-dawn`) identical before and after; `shot --against`, `kernels --against`.

### Batch 2: the packed normal's decode on the resolve path (fixes the new AMD bounce failure)

Files: `components/rtx/shaders/tangent.h`, `octahedral.h`, `components/rtxvulkan/shaders/lib/traversal.glsl`,
`lib/geometry.glsl`, `kernels.cpp`.

- [ ] `traversal.glsl:756-761`: three corner normals decoded in `committedHit`/`resolve` raised direct, landing and
      bounce by 16 VGPRs on AMD (bounce no-dark 144 against 128) → measure on AMD and RADV: (a) decode the corners into
      the interpolation one at a time so one decoded normal is live, not three; (b) interpolate the three words'
      squares where they share a face of the octahedron and decode once, falling back across the fold; (c) store the
      normal as three halves (8 B), one `unpackHalf2x16` pair a corner **(decision: reverses the user's format choice;
      only if (a) and (b) fail)**.
- [ ] `kernels.cpp`: bounce's ceiling held at 128 once the decode is fixed.

### Batch 3: a spill gate on every driver

Files: `components/rtxvulkan/device/device.cpp`, `device.hpp`, `apps/components_tests/rtxvulkan/trace/visibility/kernels.cpp`.

- [ ] `device.cpp:319`: only `numUsedVgprs` and `scratchMemUsageInBytes` are read → map NVIDIA's "Register Count" and
      "Shared Memory Size", RADV's "VGPRs", "Spilled VGPRs" and "Spilled SGPRs"; `CompiledUse` gains a shared-memory and
      a spill field.
- [ ] `kernels.cpp`: ceilings per driver; on NVIDIA, shared memory bounded at the declared size for kernels that
      declare it and at nought for those that declare none (water, media, panes, landing, bounce fail today: they are
      the gate's first findings, not regressions).

### Batch 4: the store's copies and the preparation's held memory

Files: `components/platform/wholefile.*`, `components/rtx/preprocess/contentstore.*`, `contentcache.hpp`,
`contentcodec.hpp`, `components/rtx/image/chainbuild.*`, `components/rtx/preprocess/texture/chainpass.*`,
`conversionpass.*`, `contentpreprocessor.*`.

- [ ] `wholefile.cpp:17`: `resize` zero-fills before the read → a buffer the read overwrites (a default-initialising
      allocator); a `readWhole` that scatters into a header span and a payload vector, and takes a size cap so one
      `fstat` refuses the oversized file (the store's `file_size` call goes).
- [ ] `contentstore.cpp:400` (header `erase`) and `contentcodec.hpp:160` (`assign`): two whole copies per hit → the
      payload vector moved into `MadeChain`; the chain form's byte moves into the header.
- [ ] `contentcache.hpp:58-60`, `contentstore.cpp:244-255`: a write is encoded into a scratch, copied into the task,
      under the lock the frame reads under → encode into a fresh vector, move it into the queue, push under the lock.
- [ ] `ContentCache::mPayload` keeps its largest capacity per thread (16 × up to 21.3 MB) → no retained payload buffer.
- [ ] `chainbuild.hpp:81`, `chainbuild.cpp:240`: normal-map means at 12 B a texel over every level, zero-filled → keep
      means for levels one and down, stream level nought into level one through a three-row ring (saves 50 MB a thread
      at 2048², 200 MB at 4096²); bump `ChainPass::sVersion` if the summation order moves.
- [ ] `chainpass.hpp:112`, `conversionpass.hpp:91,99`: three `ChainBuild`s and their scratch per preprocessor → one
      build scratch owned by the preprocessor (read `holdsCut` before the second build).
- [ ] `chainbuild.cpp:368,425`: `encode`/`encodeBc7` grow the output a level at a time and keep 2× capacity →
      reserve the known total.
- [ ] Large chain buffers held one a thread for the session (32 threads, each at its largest texture) → one pool shared
      by the preparation's and the chain threads, bounded by the builds running at once; a worker reuses what it takes,
      so no buffer is freed and allocated again (decided: reuse, pool, trim once).
- [ ] One `malloc_trim(0)` when the preparation ends, behind a `Platform` call that is a no-op off glibc: returns what
      its threads freed (0.81 GiB of steady state measured with a fixed threshold). glibc's thresholds stay at their
      defaults: a small fixed threshold would map and unmap every large buffer at each build (decided against).
- [ ] Tests: a hit reads the payload once (counted), a write moves its buffer (no copy), the chain build's output is
      byte-identical, `RtxPassVersionsTest` unchanged or bumped.

### Batch 5: the preparation does each thing once

Files: `components/rtx/prepare/contentpreparation.*`, `preparationmanifest.*`, `filestamps.*`,
`components/rtx/preprocess/contentstore.*`, `threadcontent.hpp`.

- [ ] `contentpreparation.cpp:272-289`: `finish` keeps only this run's entries → keep every list's entries, bounded by
      a last-seen run count; check "nothing moved" before copying (`:284`, `:289`).
- [ ] `contentpreparation.cpp:38`: chains asked at `StoreReach::Disk` are read whole and dropped → `ContentStore::holds`
      (one `stat`, the session's stamp queued); play still verifies on use.
- [ ] `contentpreparation.cpp:34-48,168`: two preprocessors a hand, the specular layout keyed per part →
      one `ThreadContent` a hand, its facts and chains claimed once a run.
- [ ] `preparationmanifest.*`: 484,046 names for 102,145 distinct, each its own allocation and its own stamp → a string
      table and flat index runs, each distinct path stamped once.
- [ ] `contentpreparation.cpp` `prepare()` waits by spinning on `yield` → a wait on the workers' completion.
- [ ] Tests: the manifest has none → a round trip, a corrupt file read as empty, a profile switch's entries kept, a
      re-walk that finds its chains in the store without reading them.

### Batch 6: entries checked against their input, and the made-chain leak

Files: `components/rtx/preprocess/contentcodec.hpp`, `contentpreprocessor.cpp`, `components/rtx/mirror/meshreader.cpp`,
`components/rtx/scene/meshtable.cpp`, `components/rtx/prepare/preparationmanifest.cpp`,
`components/rtx/preprocess/preparedimages.*`, `components/rtx/renderer/sceneuploader.cpp`.

- [ ] Bug, `ContentCodec<ShapePass::Output>::decode`: lengths only; a normal count past the positions writes past the
      mesh's run in release (`meshtable.cpp:177`), a source past the vertices reads out of bounds (`meshreader.cpp:64`)
      → after `find`, check the kept count is a multiple of three, every index is below vertices + sources, every source
      below vertices, the normal count nought or vertices + sources; a misfit is a miss and a discard.
- [ ] Bug, `preparationmanifest.cpp:98`: `resize(files)` from the file → bound by the bytes left over two.
- [ ] Bug, `preparedimages.cpp:132-143`: an entry whose slot was freed before it collected the chain keeps `mHeld`
      forever → drop an entry once its file (and partner) is gone, whatever `mHeld` holds.
- [ ] Tests: a shape payload with each misfit is a miss; a manifest with a huge count reads as empty; a chain made and
      never collected is dropped once its file is released.

### Batch 7: the laid copy goes

Files: `components/rtx/preprocess/preparedimages.*`, `laidimagecache.*`, `components/rtx/image/imagedescription.*`,
`components/rtx/scene/texturetable.hpp`, `scenetextures.*`, `components/rtx/mirror/surfacedescription.hpp`,
`components/rtx/mirror/cells/prepared.hpp`, `components/rtx/preprocess/texture/chainpass.*`.

- [ ] `PreparedImages` keyed by image address → by normalised file name, as the fact and laid caches are; one entry
      and one chain a file across threads.
- [ ] A widened file's RGBA8 copy kept by three owners after its chain landed (22.4 MB at 2048²) → widen into
      `ChainPass::describe`'s own scratch; a pending widened slot draws the stand-in (`mStandsIn`); delete `layImage`,
      `LaidImageCache` and the three `mLaid` fields.
- [ ] Dead with it: `layImage`'s `texels` parameter (`imagedescription.cpp:405`), `MadeImage::laidLayout`'s widened case
      (`madeimage.cpp:27`), `ChainBuild::encode`'s loose branch (`chainbuild.cpp:411`) and the test that keeps it alive.
- [ ] Measure: host memory at the ship and Balmora before and after.

### Batch 8: the texture passes do their work once

Files: `components/rtx/image/shadingmap.*`, `chainbuild.cpp`, `components/rtx/preprocess/texture/conversionpass.*`,
`chainpass.cpp`.

- [ ] `shadingmap.cpp:81`: decodes every level's alpha again, uses level nought → `ShadingMap(const TextureData&,
      const AlphaImage&)` from `ChainBuild::mAlpha`.
- [ ] `conversionpass.cpp:137,143`: the whole delit chain built to read level nought → `mDelit.build(diffuse, 1)`;
      key only the finest levels (version bump).
- [ ] The classic pair converted twice, once an encoding, `mHighlights` in floats → one run emitting both chains,
      highlights in bytes (14 pairs on the Fullrest Repack: small, but two routes to one result).
- [ ] `chainbuild.cpp:331`: `layOwnLevels` copies a loose chain to read it as RGBA → encode from the file's levels with
      a swizzle for BGR.
- [ ] A store hit releases the image `FinestTexels` pinned (`chainpass.cpp:106`, `conversionpass.cpp:219`).

### Batch 9: shapes held where the frame reads them

Files: `components/rtx/preprocess/contentcodec.hpp`, `contentstore.*`, `contentpass.hpp`, `apps/rtxtool/stopwriter.cpp`.

- [ ] `contentcodec.hpp:167-196`: indices as u32 and normals as floats → indices as u16 where the vertex count allows
      (widened on decode), normals as the table's own word (`Shaders::packNormal`); measured 69.3 → 29.6 MiB of shape
      payload on `openmw-pbr`, entries within 4 KiB 77% → 92%; version bump.
- [ ] `contentstore.cpp:282`: residency by size alone, so the frame recomputes about 140 large shapes a session (58 ms
      at the ship) → shapes resident at any size, within the bound.
- [ ] `contentstore.hpp:99`: the bound counts payload bytes only and its comment says facts → count each entry's node
      and allocation; make `ContentKeyHash` `noexcept`; correct the comment.
- [ ] `mResident`, `mStamped` grow and rehash on the frame's path → reserve from the start's count of small files;
      bound `mStamped`.
- [ ] `contentstore.cpp:207`: `pathOf` before the reach test, three allocations on every memory-only miss → inside the
      `Disk` branch.
- [ ] `contentpreprocessor.cpp:31-35`: a hit's read and decode are not counted → count them.
- [ ] `stopwriter.cpp:250`: colours counted at `sizeof(osg::Vec3f)` after the packing → `GpuVertexColour`.

### Batch 10: compile-time stages and narrower rays

Files: `components/rtxvulkan/shaders/lib/traversal.glsl`, `lib/stage.glsl`, `lib/shading.glsl`, the stage kernels,
`components/rtxvulkan/trace/visibilitypass.cpp`, `components/rtxvulkan/pipeline/*`.

- [ ] `traversal.glsl:1229,1327`: the relief march compiled into stages that read only the stored answer (NVIDIA
      unbounced 78 → 94, bounce 119 → 127 registers with relief) → the relief source a compile-time argument of
      `resolve`.
- [ ] `visibilitypass.cpp:344-379,546`: every stage compiled for 48 tuples, 96 of 340 pipelines byte-identical → key
      each stage's table on its declared constants and alias the duplicates; make the trace's meet visible at compile
      time so its dark tuples fold. About 28% fewer pipelines in a first start's compile (61 s on AMD).
- [ ] `traversal.glsl:548`, `shading.glsl:724`, `direct.comp:71-87`: wide asks, per-site constants per lane, two
      fixed-order helpers → a passage record `{from, along, reach}` and one helper; measure (8–12 VGPRs estimated in
      landing, panes and direct).
- [ ] `traversal.glsl:377`: the dark alpha read before the cut test → read it only for walk-past or by-chance rays
      after the test (measured small: media 220 → 218 on AMD; land with the batch, not alone).
- [ ] `eyeshading.glsl:93` `EyeRun::mOrigin` never read; the water-pending record packed by hand in `visibility.comp`
      → delete; pack it in `lib/stagerecord.glsl` beside `packStageRecord`.
- [ ] `eyeshading.glsl:184`: four debug views in the shipping eye shading → drawn by the composite from the channels it
      already holds.
- [ ] Spike (decided): the isotropic specular read on every vendor (`traversal.glsl:1453,1464`) and the two descriptor
      arrays over one set of images (`texturearray.glsl:28,46`) → one sampled-image array and a small sampler array,
      combined at the read with `nonuniformEXT` on the combined sampler (Vulkan's separate-image-sampler sample, D3D12
      SM6.6 heaps), `textureGrad` restored for specular maps; run the gloss tests and the hang's reproduction on the AMD
      laptop; adopt only if the hang is gone and the frames hold, the moved pictures back to the user with the
      measurement.
- Decided, no change: `HAS_DARK_ALPHA` stays, all its tuples compiled at start; the shore ray stays, the floor traced
  against the real geometry rather than a baked depth cache.

### Batch 11: store failures heard

Files: `components/rtx/preprocess/contentstore.*`, `contentstats.*`, the stats row in `apps/rtxtool/` and the game's log.

- [ ] `contentstore.cpp:449-453`: a failed write counted, never logged → logged once, as the design states.
- [ ] `ContentStoreStats` read by no report → drops, refusals and evictions in the harness's report and the game's log.
- [ ] A write at the queue's bound dropped and its model marked prepared → `Disk` writers wait for room, `Memory` writers
      drop, an empty queue admits any write.
- [ ] Tests: a failed write logged once; the preparation's write never dropped.

### Batch 12: the streaming frames

Files: `components/platform/process*.cpp`, `apps/rtxtool/`, `apps/openmw/mwrender/rtx/tracedterrain.cpp`,
`tracedview.cpp`, `components/myguirtx/`.

- [ ] The harness's huge-page tunable turns a fragmented heap into 50–60 ms of compaction inside a crossing → leave it
      off for the streaming suite, or keep it and flag a run whose `compact_stall` rose.
- [ ] `tracedterrain.cpp:168`: `getBounds` walks every land record on every focus ray (0.13 ms a frame) → read once a
      worldspace and keep the box.
- [ ] `TracedView::keepCopy`'s 256 KiB tile image allocated each time; MyGUIRtx's vertex vectors grown in the frame →
      one image a view; vectors reserved.
- [ ] Upstream fault fix, approved: preload what insertion builds — `Npc::getModelsToPreload` lists the race's body
      parts and the record's inventory parts without custom data; `CreatureLevList::getModelsToPreload` lists every
      candidate, bounded by the preloader's cache (up to 57 of 293 ms of the slow frames); listed in AGENTS.md's
      approved fixes; a test that an unvisited cell's NPC lists its worn parts.

### Batch 13: CI without a mirror

Files: `.github/workflows/*.yml`, `.github/actions/openmw-deps/action.yml`, `CI/install_ubuntu_deps.sh`, the
release-base container's definition.

- [ ] The Linux jobs download their packages from `archive.ubuntu.com` at every run, and a mirror that stalls between
      files ends a job at the 15-minute limit (run 42553703270) → the Linux jobs run in an image built from the
      release-base container with the packages installed, published to the repository's registry and pinned by digest;
      the install script remains the image's recipe.
- [ ] Until then: cache the downloaded `.deb` files keyed by the package list, so a run reaches the mirror only when the
      list changes.

### Batch 14: comments and tests left behind

Files: `components/rtx/shaders/scene.h:58`, `components/rtxvulkan/shaders/shared/sets.h:29,41`,
`components/rtx/scene/materialtable.cpp:56`, `components/rtxvulkan/texture/groundcompositepass.hpp:31`,
`components/rtxvulkan/device/memory/memory.hpp:116`, `scenetextures.cpp:78`, `texture.hpp:393`,
`components/rtx/image/shadingmap.cpp:113`, `apps/components_tests/rtx/support/mipchain.*`, the image tests.

- [ ] Comments that name a shading map as a texture's companion → the normal spread.
- [ ] `ShadingMap()` used by tests alone, its comment describing a device array → delete or move to the test support.
- [ ] `PreparedImages`' doc says normal and data maps → every made chain.
- [ ] `Testing::MipChain` and `RtxMipChainTest` stand for the removed `MipChainPass` and overlap `RtxChainBuildTest` →
      fold their cases into it.
- [ ] Test gaps: the `sLargestBc7ChainSide` boundary, a painted colour with a cut to BC7, a data BC1 with a chain and a
      cut standing as it is, a conversion between files of two sizes, the resident bound and `keepResident`'s accounting
      on a replaced entry.
