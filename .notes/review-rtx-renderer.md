# Review: the ray tracing renderer — GPU memory, compute, resource ownership

Scope: `components/rtx/`, `components/rtxvulkan/`, `components/myguirtx/`, `apps/openmw/mwrender/rtx/`.
Read only; nothing built or measured. Figures are computed from strides and extents in the code, and
each item that names a cost wants the measurement it names before it lands.

What is already right and stays: VMA sub-allocation with content pools, budget re-read and
priorities; one graveyard on one timeline; synchronization2 with same-layout transitions merged into
one memory barrier; push descriptors and an update-after-bind bindless set; a best-fit staging ring;
static BLAS FAST_TRACE + compaction, deforming BLAS refit with a rebuild rota, TLAS packed on the GPU
and skipped when still; opaque geometry built opaque; the stored-hit visibility buffer; classify,
compact and indirect dispatch for media, panes and landing; the shared-memory wide à-trous level;
the two-slot ring that waits after the walk; swapchain semaphores per image; the pipeline cache's
header and atomic write; incremental hand-over (`SceneUploader` sends what changed); map tiles that
trace against the world's own scene.

## Proposal

### Root causes

1. **Frame memory has no declared lifetime, and its sum is kept by hand.** Every per-extent image is
   its own allocation for the session, whether it lives across frames or inside one pass range. The
   stage images (104 B/px, dead after `landing`) and the denoiser's in-loop scratch (64.7 B/px,
   first written in the denoiser, last read by the composite) never overlap and never share.
   `frameAt` (`vulkanrenderer.cpp:73-92`) adds up each owner's `bytesAt` by hand, so the stage tile
   lists and the picture chain (219 MB once the inventory opens) are outside the reserve. Frame:
   552 MB at a 720p trace shown at 1080p, 2.2 GB at 4K Quality, 4.6 GB at native 4K.
2. **More than one copy of one datum.** Static positions are on the device twice
   (`BlockedBuffer mPositions` and `BottomLevelStore::mArrived`, staged twice per arrival). Tables
   that only the queue writes or only one frame reads are kept per frame slot (`mPacked`, posed
   normals and tangents, the mesh table). `BlockedBuffer` inside `JoinedBlocks` builds address tables
   nothing reads. A subject's scene (the doll, the race preview) uploads and prepares again every texture the world
   already holds.
3. **Host-written (BAR) memory has no policy.** Every `SlotTable` copy lives in BAR, sized at the
   world's room, doubled per slot, doubled again on growth with the old copy buried, never shrunk,
   never refused, with no other path. The instance rows alone are 160 B × 2^18 = 40 MiB a copy
   (`sceneroom.hpp` says 16 MiB, which is the row table's); world tables reach 112 MiB of a 246 MiB
   window on an RTX 20 card without resizable BAR, where a full window ends the renderer.
4. **Arrival work has no per-frame budget.** A cell's adoption puts all of its BLAS builds, texture
   uploads, mip chains, BC7 encodes, shading maps, table growths (re-staged whole from the host) and
   VMA block allocations into one frame, and a cell leaving puts every destroy into one collect. The
   project's rule is incremental, never batched behind a threshold.
5. **Batch rebuilds where bookkeeping can be incremental.** Lights are cleared, re-made and sorted
   every walk, and the light grid keys lamps by their sorted index, so a change in the count
   rebuilds the grid whole. The sweep scans whole identity maps when one entry is missed. Host tables
   grow by `std::vector` doubling at points of their own, apart from the device tables' room.
6. **The cell reader forgets what it read.** A cell is dropped at the radius it is asked at, a
   model or texture no cell holds is cleared at once, and a worldspace change resets the reader, so
   the resource cache's expiry decides when a file is read again.
7. **Shader state carried past where it is read.** `GatherRun::mLampRay` (a whole `RayAsk`) and the
   whole `Gloss` live across shadow traversals; `storeBounced` loads nine channels for a mask that is
   nought where no layer stands; the reprojection match runs three to four times a pixel; a
   candidate's cone is computed before the early-outs that discard it.

### Target design

- **One declaration per frame image, and everything derived from it.** Each owner (`GBuffer`,
  `DenoiseHistory`, `FogVolume`, `Upscaler`, `DisplayChain`, `PresentTarget`) declares its images in one
  table — format, grid, and the first and last pass of the fixed order that touch it, or *history*
  for an image read across frames, or *kept* for one a read-back names after the frame (`readChannel`,
  the digest). The same table makes the images, binds them, measures them, and is what `frameAt` sums,
  so the reserve cannot miss what is made. Images whose pass ranges do not overlap share memory: one
  VMA allocation per chain, sized to the largest overlapping set, images placed by
  `vmaCreateAliasingImage`, and at each alias's first write a barrier from the other alias's last
  writer (`SHADER_WRITE` → `SHADER_WRITE`), since `Use::sUndefined` sources no access. This is the one
  gain a frame graph would bring here, taken without one: its other gains are cross-queue scheduling
  (async compute is declined) and reordering (the order is fixed by design). Practice: Frostbite
  FrameGraph (O'Donnell, GDC 2017), VMA "Resource aliasing", Vulkan spec "Memory Aliasing".
  - **What the sharing rests on, stated where it is made**: one queue, and the full barrier at the
    head of every command buffer (`commands.cpp:483-492`), so frame N+1's first pass follows frame N's
    last. A second queue would need an arena per frame slot.
  - **Each chain owns its own arena.** The world's `TraceChain` and the picture chain do not share one,
    so nothing couples their order inside a frame.
- **Memory is taken once and kept, not made on the frame that needs it.** The picture chain is sized at
  `createTargets` to the largest picture the game asks for, counted, and kept for the session. The
  glossy channels are made or not at `createTargets`, from whether the content holds any companion map.
  Neither grows on a frame.
- **One device copy per datum.** A rule written at `bufferusage.hpp`: a buffer is per frame slot only
  where a frame behind reads a row the host rewrites (poses for motion vectors, rows rewritten each
  frame); what only arrives is written once, on the queue. BLAS inputs read the scene's own buffers.
- **Where a table lives is decided once, at device creation.** Rows rewritten every frame go in BAR
  where the window holds every such table at its room; otherwise every table is device-local and
  written through the staging ring with `SlotTable`'s per-row debt as the copy list (VMA "Advanced data
  uploading"). Tables written only on arrival are always device-local. No path switches at run time,
  and a harness switch forces the staged path, so the card here tests the path an RTX 20 card takes.
- **An arrival scheduler with a per-frame budget** in counted units — staged bytes, BLAS triangles,
  encoded texels, never measured time, so two runs schedule alike and `repeat` holds. Geometry goes
  first, and a placement enters the top level only once its BLAS is built; textures draw their
  stand-in until their turn; a grown table is copied old→new on the GPU; VMA blocks are made ahead by
  a worker; the graveyard's released prefix is destroyed off the frame thread, in burial order.
  Practice: nvvk `BlasBuilder::cmdCreateParallelBlas` budget loop, Unreal's texture streaming upload
  budget, VMA/D3D12MA block-creation advice.
- **Shared textures, one residency manager.** Textures (and static meshes' BLAS) are refcounted assets
  keyed by what names them, held beside `SceneSlots`; a scene owns its rows, its TLAS and descriptor
  sets that name shared images. A deforming mesh's BLAS stays its scene's, since it is built over that
  scene's pose. One residency manager sets every texture's first level by a priority read from
  deterministic inputs.
- **RAM before the disk.** No memory saving in this plan reads or writes a file again: what a
  re-stand, a rebuild or a re-adoption needs stays in host memory, and an item that can save memory
  only by a second read is dropped. No item makes the reader or the resource system read a file more
  often than they do today, or makes the pipeline cache save more often than its one save a start.
- **Slotted lights.** A light table keyed by stable identity (`SlotRows`, lowest free slot, so order is
  deterministic without a sort); per frame only changed intensities are written, and an arrival or
  departure enters or leaves only its own grid cells — the `PlacementTable` pattern.
- **One room per table, host and device.** A host table reserves the same `SceneRoom` its device table
  is made at, so both grow at the same, logarithmically few points.

## Plan

Each batch leaves the build and tests green. Verify each by `AGENTS.md`: build the touched targets,
`./omw test <binary> --gtest_filter=...` for the covering tests, `./omw repeat --pairs=10` after
anything a frame reads, and `./omw gate` once at the end. Whoever addresses an item deletes it.

Bugs, by batch: `readComposite`'s sprites (5), the indirect dispatch past 65535 groups (12), shutdown
waiting out the tuple compile (15), a moved-from `Buffer`'s stale address (1), the picture chain and
the tile lists outside the reserve (2), a full BAR window ending the renderer (4).

Needless reads, by batch: the cell ring reads a cell again at its band's edge, after the resource
cache let a given-back model go, and after every interior (9). Nothing else in scope reads or writes a
file more than once: shader modules are read once a start (`ShaderCode`, the trace's kept until every
tuple is made), the pipeline cache is read once and written only where it changed, the sky's sheets are
read once and held, and the logs write nothing per frame.

### Batch 1 — Device memory API

Files: `device/memory/{buffer,image,memory}.*`. First, because Batch 2 adds aliased images through it.

- [ ] BUG `buffer.hpp:93` — the defaulted move leaves `mSize` and `mAddress` in the source; a moved-from
  buffer's `getDeviceAddress()` returns a stale address and its assert passes. Reset both.
- [ ] `buffer.hpp` / `image.hpp:61-78` — `Buffer` is static factories, `Image` a positional constructor,
  a description constructor and a positional `tryMake`; `bytesFor` takes size and usage for one and a
  description for the other. A `BufferDescription` beside `ImageDescription`, `make` and
  `tryMake(use, device, description, name)` on both, the positional forms gone. An image placed in an
  arena is made from the same description (`vmaCreateAliasingImage`), so Batch 2 adds no third form.
- [ ] `memory.cpp:29-40, 252-256` — buffer requirements are made by hand for a coarser alignment, so VMA
  treats every buffer as an unknown kind (buffer-image granularity padding, no dedicated preference).
  `vmaCreateBufferWithAlignment` (VMA 3.4, in tree), adopted into `Owned<VkBuffer>`.
- [ ] `buffer.cpp:63` `alignmentOwedBy` — every buffer with storage and address usage is treated as
  scratch and over-aligned. Ask for scratch alignment by its own usage.

### Batch 2 — Frame memory: one declaration, a derived reserve, then aliasing

Files: `trace/gbuffer.{hpp,cpp}`, `trace/denoise/denoisehistory.{hpp,cpp}`, `trace/fogvolume.*`,
`trace/tracechain.{hpp,cpp}`, `upscale/upscaler.{hpp,cpp}`, `display/displaychain.*`,
`present/presenttarget.*`, `picturetracer.{hpp,cpp}`, `vulkanrenderer.cpp`, `device/memory/imageuse.*`.

- [ ] `gbuffer.cpp:204-210, 236-242, 255-260, 321-327` — the seven stage images are listed by hand in
  four places. One array indexed by `STAGE_*` with one declaration table that makes, binds, measures and
  begins them, as `DenoiseHistory::sDeclared` and `FogVolume::sFogImages` do.
- [ ] Every owner's declaration carries its images' pass range (first and last pass of the fixed order),
  *history* or *kept*. List every reader: the stages, the denoiser, the composite, FSR, the display, the
  digest, `readChannel`, `readComposite`, and any debug view the tone pass shows.
- [ ] BUG `vulkanrenderer.cpp:73-92` `frameAt` — a sum kept by hand: `GBuffer::tileBytesAt`
  (`gbuffer.cpp:264`) has no caller, so the stage tile lists (~4 B/px) are outside the reserve. Derive
  `frameAt` from the declarations, and delete `tileBytesAt`.
- [ ] BUG `vulkanrenderer.cpp:869`, `picturetracer.cpp:100,108-113` — the picture chain grows to the
  largest picture ever asked (the doll is 512×1024; a map tile is bounded only by `sLargestFrameSide`,
  `tracedview.cpp:43`), is essential memory outside the reserve, and ~219 MB lands over a budget the
  textures already filled the first time the inventory opens. Size it at `createTargets` to the largest
  picture the game asks for, count it in the reserve, keep it.
- [ ] `fogvolume.cpp:270` — a picture chain's growth builds a `FogVolume` whose constructor calls
  `submitAndWait`, stalling the host behind both frames in flight. Record the clear into the caller's
  batch.
- [ ] The arena in each `TraceChain`: the images whose pass ranges do not overlap share one allocation,
  laid out from the declarations at `createTargets`, with an aliasing barrier at each alias's first
  write. Today's ranges put the stage images (but `STAGE_RECORD`, if Batch 11's first item binds it in
  the composing passes) beside the denoiser's in-loop and scratch images (`Blended`, `Narrow`,
  `FillBlended`, `FillNarrow`, `FastBlended`, the shadow fields' `Scratch`/`Visibility`/`Tiles`/
  `Penumbra`/`Mask`, the glossy and pane `FastBlended`): ~65 B/px, 60 MB at a 720p trace, 238 MB at
  1440p, 537 MB at native 4K, 34 MB on the picture chain.
- [ ] Widen the world chain's arena to the frame: FSR's transients and output (`upscaler.cpp:333-353`),
  whose range is the upscale to the tone pass, share the stage images' memory. Another 35 MB at 1080p
  output, 140 MB at 4K.
- [ ] A test that holds the declarations to the shaders: every module a pass dispatches binds only
  images whose range covers that pass (`spirvinterface` already reads each module's bindings), so a new
  reader of an aliased image fails the build's tests rather than reading another image's bits.
- [ ] A GPU test of the reserve and of leaks: after `createTargets`, the essential pool's bytes equal
  `frameAt`'s; after a world is attached, walked over a crossing, detached and the graveyard collected,
  VMA's allocation count and bytes are back at their baseline.
- [ ] `gbuffer.cpp:347-352` — `Specular`, `SpecularAlbedo` and the glossy filter's images (40 B/px, 37 MB
  at 720p) exist on a vanilla install. Make them at `createTargets` only where the content holds a
  companion map (`_n`, `_nh`, `_spec`), read from the VFS's index, which is in memory; never on a
  frame.

### Batch 3 — One device copy per datum (scene)

Files: `scene/bottomlevelstore.*`, `scene/sceneacceleration.*`, `scene/scenebuffers.*`,
`device/memory/{blockedbuffer,joinedblocks,bufferusage}.*`.

- [ ] `bottomlevelstore.cpp:144-145, 211-212, 258, 271, 283-291, 326` — every static mesh's positions
  are staged into `mArrived` and the BLAS built from that copy, though `scenebuffers.cpp:216` already
  wrote the same bytes into `mPositions`, which `committedHit` reads for the scene's life. 12 B per
  static vertex of staging and PCIe traffic per arrival, plus up to 64 MiB device-local for life.
  Build from `mPositions.addressOf(mesh.mVertices.mOffset)` (a run never straddles a block), add
  `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY` to `mPositions`' usage, delete `mArrived`,
  `BuildRow::mPositions`/`mArrivedAt` and the staging loop. The structure is then built over the very
  bytes a hit reads its corners from. Fix the stale comments that say a hit reads static positions
  back through position fetch: `bottomlevelstore.cpp:279-281`, `sceneacceleration.hpp:206-208`,
  `sceneacceleration.cpp:126-129`, `scenebuffers.hpp:32-33`.
- [ ] `sceneacceleration.cpp:89-98` — `mPacked` is one device-local buffer per slot, written by the pack
  pass and read only by the build in the same command buffer; the head barrier orders it against the
  frame before. One copy; saves 16 MiB at the room.
- [ ] `scenebuffers.hpp:192-200` — posed normals and tangents per slot; only poses are read across
  copies (`previousPoseBlockOf`). One copy; 16 B per bind vertex per extra slot and one address table.
- [ ] `scenebuffers.hpp:166-170` — the mesh table is a host-written `SlotTable` per slot, defended by
  "the frame behind reads the old row", while layers and masks (`scenebuffers.cpp:290-318`) solve the
  same problem with one copy written on the queue. The mesh table changes only on arrival: one copy,
  device-local, written on the queue.
- [ ] `blockedbuffer.cpp:75-77` with `joinedblocks.hpp:58-59` — each growth remakes a host-written
  address table inside `JoinedBlocks`' `mShared` and `mOwn` that nothing reads (only `join()`'s
  per-slot tables are read). `BlockedBuffer` keeps blocks and addresses; the table becomes a small
  owner used by a standalone `BlockedBuffer`, `SlotBlocks` and `JoinedBlocks::join`.
  `JoinedBlocks::getBytes` then counts what exists.
- [ ] `bufferusage.hpp` — write the rule this batch applies: per slot only where a frame behind reads a
  row the host rewrites.

### Batch 4 — Where each table lives

Files: `device/memory/{memory,slottable,growablebuffer}.*`, `device/physicaldevice.cpp`,
`scene/scenebuffers.*`, `scene/sceneroom.hpp`. After Batch 3.

- [ ] Measure: read `mHostWrittenLive` at the heaviest place in `files/rtx/views.cfg` and after a long
  walk, beside the 246 MiB window of a card without resizable BAR.
- [ ] `sceneroom.hpp:22-28`, `scenebuffers.cpp:147-148` — the comment prices the world's placement room
  at 16 MiB a copy; the instance rows are 160 B × 2^18 = 40 MiB a copy beside the row table's 16 MiB.
  State the real figure per table.
- [ ] BUG `memory.cpp:247-260`, `buffer.cpp:31`, `slottable.hpp:269`, `growablebuffer.cpp:75` — a
  `HostWritten` buffer is never refused and has no other path: a full window is a `DeviceError` that
  ends the renderer. Decide each table's place at device creation from the window's size and the
  tables' rooms: BAR where every per-frame table fits at its room, device-local and staged by
  `SlotTable`'s per-row debt otherwise. Tables written only on arrival are device-local on every
  device. No switch at run time.
- [ ] A harness switch that forces the staged path, run by the GPU tests, so the RTX 20 card's path is
  tested on this card.

### Batch 5 — Arrival budget and off-frame frees

Files: `vulkanrenderer.cpp:380-386`, `scene/bottomlevelstore.cpp:277-355`, `scene/sceneacceleration.*`,
`scene/scenebuffers.cpp:299-318`, `scene/skintables.hpp`, `texture/{texture,texturecost,texturearrival}.*`,
`device/graveyard.cpp`, `device/commands.cpp`, `device/memory/{growablebuffer,blockedbuffer}.cpp`,
`display/displaychain.cpp`.

- [ ] BUG `vulkanrenderer.cpp:925-929` with `displaychain.cpp:115-118` — with upscale off the sprite
  composite writes in place into `TraceChain::mComposed`, so `readComposite` returns smoke drawn in
  under `off` and not under any upscale mode, and disagrees with the digest's composed slot, against
  `bloompass.hpp:24-25`. The sprite composite writes into an image the display owns (declared in
  Batch 2's table).
- [ ] Measure first: `./omw release bench --frame-times=<dir>` across a region crossing; read the worst
  frame and the p99 beside the median.
- [ ] `vulkanrenderer.cpp:380-386` — a cell's whole arrival rides one submit. An arrival scheduler with a
  per-frame budget in counted units (staged bytes, BLAS triangles, encoded texels): BLAS first, a
  placement into the top level only once its BLAS stands, textures on their stand-in until their turn.
- [ ] `scenebuffers.cpp:299-318`, `skintables.hpp:21-28` — a grown table is re-staged whole from the
  host. Copy old→new on the GPU and stage only the new rows.
- [ ] `texture.cpp:224-252`, `texturecost.cpp:78-85` — every single-level file past 512 texels holds its
  own loose RGBA8 chain until the batch ends (89 MB for 4096², plus 22 MB of blocks), and `costAt`
  sums them, so a cell with N such files asks for N×111 MB and `chooseSide` holds the whole arrival
  to a smaller side. One loose-chain canvas encoded in groups under a room, priced once a group, as
  `texturearrival.hpp:169-173` already does for the spread means.
- [ ] `graveyard.cpp:58, 145-154` — every handle the wait released is destroyed on the frame thread
  under `mLock`, which also blocks compile threads burying pipelines. A destroyer thread takes the
  released prefix and destroys it in burial order (views before their image, the top level before its
  storage); `vkDestroy*` of distinct handles needs no external synchronization, and VMA locks inside.
  It is joined before `Device::~Device` idles.
- [ ] `GrowableBuffer::growTo` via `SlotTable::sync`, `blockedbuffer.cpp` reserve, `commands.cpp:347`
  `takeStaging` — a miss in VMA's blocks is a `vkAllocateMemory` on the frame thread. Keep one spare
  block ahead per pool, made by a worker.

### Batch 6 — Shared textures across scenes

Files: `scene/{devicescene,sceneslots,bottomlevelstore}.*`, `texture/texture.*`, `picturetracer.*`,
`vulkanrenderer.*`. After Batch 5, so a shared asset arrives through one scheduler.

- [ ] `devicescene.hpp:181-191` — each `DeviceScene` owns its `TextureArray` and `BottomLevelStore`, so
  opening the inventory uploads, chains, shades and spreads again every texture the wearer shows in the
  world, on the frame it opens. Textures become refcounted assets owned beside `SceneSlots`, keyed by
  what names them (path, encoding and wrap; `SceneUtil::EmbeddedImage`'s stamp for an embedded
  image); a scene owns its rows, its TLAS and descriptor sets that name shared images. An asset goes to
  the graveyard when its last scene lets it go.
- [ ] Static meshes' BLAS shared the same way (a doll's held weapon or shield, a preview's static
  parts). A deforming mesh's BLAS stays its scene's: it is built over that scene's pose.
- [ ] The bindless slots then name shared images: one slot space for every scene, or each scene's own
  set written from the shared table. Choose by the descriptor-pool cost at the texture cap.

### Batch 7 — Texture residency

Files: `texture/{texture,texturecost,texturearrival}.*`, `scene/texturetable.hpp`, `vulkanrenderer.cpp:224-226`.
After Batch 5, whose scheduler carries the re-stands and whose single canvas prices the transients,
and Batch 6, so one manager holds every scene's textures.

- [ ] `texture.cpp:449` — a texture's side is chosen from the room left when its cell arrives, a standing
  texture is never lowered, and a lowered one is restored only on a mode change: far cells loaded first
  keep full size and near cells arriving later are cut. A residency manager: each texture's first level
  is state, its priority from the distance to its nearest placement and its texel density, and a
  budgeted number of re-stands a frame (a new image, the shared levels copied on the GPU, the
  descriptor swapped through the owed-slot debts) moves every texture toward the side the budget gives
  its priority. The inputs and the budget are counted, never timed, so two runs stand the same sides.
  A raised side reads the slot's own host image (`TextureRow::mImage`, or `mLaid` where a reader laid
  it), which stays for the slot's life: the manager evicts device levels only, never reopens a file,
  and never drops the host image to make room. Unreal's streamer reads mips from disk because its
  content does not fit in memory; Morrowind's does. Practice: Unreal texture streaming, RTX Remix's
  texture budget. Check with `./omw shot --against` after a walk, and `--frame-times` across a crossing
  for the re-stands' cost.
- [ ] `texturetable.hpp:109` — a scene holds at most 4095 textures, which diffuse, normal and specular
  maps and ground composites near in a 9×9 exterior with PBR packs. Raise it; it costs descriptor-pool
  memory only.

### Batch 8 — Host core: incremental lights, sweep and tables

Files: `components/rtx/scene/{scenedesc,lightgrid}.*`, `components/rtx/mirror/cells/{cellplacer,cellring,held,cellreader}.*`,
`components/rtx/mirror/mirroridentity.hpp`, `components/rtx/common/{runs,slots}.hpp`,
`components/rtx/mirror/sceneextractor.*`, `components/rtx/view/offscreentrace.cpp`,
`apps/openmw/mwrender/rtx/{worldmirror,rtxrenderer,tracedterrain}.*`.

- [ ] `scenedesc.cpp:275-298`, `lightgrid.cpp:103`, `cellplacer.cpp:679` — lights cleared, re-made and
  sorted every walk; the grid keys lamps by sorted index, so a count change (a spell light, a torch
  bearer entering reach, a ring cell's lamps) rebuilds the grid whole, and a carried torch renumbers
  every lamp between its old and new rank. A slotted light table keyed by stable identity (the walk's
  by `LightSource` through a `Kept` map, the ring's by held cell and lamp index), intensity written per
  frame, grid cells entered and left per lamp. Check with `./omw repeat` and `shot --against`.
- [ ] `mirroridentity.hpp:233` — when `whole()` fails, `retire` iterates every entry of the map
  (reserved at 65k placements), frozen and ring entries included. Keep held entries out of the sweep
  domain: an intrusive list ordered by epoch, so a sweep costs what it drops.
- [ ] `runs.hpp:215` `RunBuffer`, `slots.hpp:265,324` `SlotRows` — host tables grow by vector doubling at
  points of their own, copying whole tables on a crossing frame (~5.5 MB of placement rows at 32k).
  Reserve each at the `SceneRoom` its device table is made at, so host and device grow together and
  logarithmically rarely, and spans over contiguous rows stay as they are.
- [ ] `worldmirror.cpp:203` — `std::move` into `make_shared` leaves `Released` at zero capacity every
  frame that releases, against its comment ("grown to the busiest crossing so far"). Reserve the
  replacement to the last high-water mark.
- [ ] `offscreentrace.cpp:160` — a picture's sweep drops released objects on the frame thread, against
  the seam's rule. Hand them through the world mirror's `UnrefQueue` path, and let one helper own the
  sequence (clear placement, clock, eye, extract, retire, release, hand) the world and picture walks
  both repeat.
- [ ] `sceneextractor.cpp:622-628` — every picture's extractor reserves the world's budgets (65k
  placements, 16k meshes, 16k materials, 8k frozen roots). The caller passes the budget.
- [ ] `SceneExtractor`, `WorldMirror`, `RtxRenderer` — wired by reference to siblings, non-copyable only
  because a member is. Delete copy and move explicitly (`AGENTS.md`, Conventions).
- [ ] `tracedterrain.cpp` `TracedTerrain::meet` refills a 65×65 grid from `fillVertexBuffers` for each
  distant cell a CPU ray crosses; the ring's `GroundReader` built the same positions
  (`HeldGround::mStood.mMesh`). Read the held mesh.

### Batch 9 — Reads the cell reader keeps

Files: `components/rtx/mirror/cells/{cellring,cellsupply,cellreader,readermemory}.*`,
`apps/openmw/mwrender/rtx/rtxsettings.*`. Each: `./omw release bench --frame-times=<dir>` over a walk that crosses a band's edge
twice and over a door in and out, reading the worst frame, the reader's reads and `ReaderMemory`.

- [ ] `cellring.cpp:497-511` — a held cell is dropped (`dropUnless`), and a handed one given back
  (`sift`), at the same `band` `ask` reads at, so a player who walks to and fro across a band's edge
  drops and reads the same cells each time: the template and its images again through the resource
  system, the template's walk, the preprocessing, the upload and the BLAS builds. Keep a cell until it
  is one cell past the band it was asked at (streaming hysteresis: a load range and a wider unload
  range, as `uGridsToLoad` with its buffer and every open-world streamer keep). What stands is still
  the reach; only what is held is wider. The radii are fixed numbers, so two runs hold the same cells.
- [ ] `cellreader.cpp:392-447` `giveBack` — a model or texture no cell holds is `reuse`d at once: its
  arrays cleared and its template's or image's reference dropped. Upstream's resource cache then lets
  the template go after `[Cells] cache expiry delay` (5 s, and 1 s during a cell change,
  `mwworld/scene.cpp:730, 796`), so the next cell that names it reads the file again, parses it, and
  walks and preprocesses it again. Keep a given-back model or texture filed and idle, its reference
  held, in a least-recently-given-back order under a byte budget; a read finds it filed and lends it
  again, and only a reader past its budget lets the oldest go. **The budget comes from the machine's
  RAM**, by the user's decision: a share of the physical memory by default, which a `[RTX]` setting in
  MB replaces, both resolved once in `RtxSettings::derive`. Physical memory is a fact the systems spell
  differently, so it is a `Platform::` call in a `…posix.cpp` and `…win32.cpp` pair behind one header
  (`AGENTS.md`, Conventions). The harness states the budget in MB on its command line, so a run's
  evictions do not follow the machine it ran on. Pick the default share after the measurement:
  `ReaderMemory`'s idle bytes after one walk through every place of the suites, vanilla and with a
  replacer pack. `ReaderMemory` gains the idle count and bytes, and the reads saved. The data a
  read gives is the same either way, so no picture moves; only when a cell is handed does. An idle
  model keeps its vertex arrays, which its next adoption reads (`CellHolds::adoptParts`).
- [ ] `cellsupply.cpp:46-68` `CellSupply::follow` — a worldspace change stops the worker and resets the
  reader, so every model and texture of the exterior's ring is read again on the way out of a shop. Keep
  the last exterior's reader, with its filed and idle sets, parked while the game is in an interior
  (one parked reader, its idle bytes counted in the same budget), and resume it when the game returns to that worldspace.
  The frame's rows still go with the frame's holds, as `CellRing::forget` says; what is kept is the
  host's read content, so the return costs uploads and builds but no file.

### Batch 10 — Shaders, bit-identical

Files: `shaders/lib/{shading,traversal,bounced,surfacematch,stage}.glsl`,
`shaders/trace/{visibility,direct,bounce,landing}.comp`, `shaders/trace/denoise/{accumulate,shadowtiles,specular}.comp`.
Each: `./omw kernels --against`, `./omw repeat --pairs=10`, and `./omw release bench` A/B for anything
that adds a branch (`AGENTS.md`, "One path through a shader").

- [ ] `lib/shading.glsl:232` (set :434, read :481, :503) — `GatherRun::mLampRay`, a whole `RayAsk`
  (~16 words), is loop-carried through the sky ray's traversal. Carry `vec2 mLampDraw` and rebuild with
  `lampAsk(run.mKept, stepOf(surface), run.mLampDraw, run.mSplit)` at both sites. `direct` and
  `landing` sit at 168 registers; read their counts in the spill gate after.
- [ ] `lib/shading.glsl:186, 455, 1225` — `direct` carries the whole `Gloss` (~22 words) through both
  shadow traversals; only `mGlossy` is read after `START`. Recompute `glossOf(surface)` in
  `stepSolid`, as `startBounce` does.
- [ ] `visibility.comp:340-352` writes "nothing" into `CHANNEL_SHADOWED`, `CHANNEL_LAMPED` (and
  `CHANNEL_SPECULAR` with maps) at every pixel, and `direct.comp:98-101` writes them again at every
  solid. `direct.comp` already runs over every pixel and returns early at no-solid and cut pixels
  (`:47-48, :86-89`): it writes the nothing there (`SPECULAR_NO_LOBE` as `answer.glsl:86` gives), and
  the visibility stores go. One writer per channel, no new branch, 24 B/px.
- [ ] `bounced.glsl:47, 60` — `CHANNEL_SPECULAR` is loaded, stored, and loaded again; the sum is in hand.
  (Gone with Batch 11's first item if that lands first.)
- [ ] `accumulate.comp:235-247`, `shadowtiles.comp:290-322` (twice, one per field),
  `specular.comp:140-146` — the same `RTX_HISTORY_SHARES` geometric match (footprint, plane, four held
  surface fetches, four plane tests) a pixel three to four times. `accumulate` writes the four match
  bits to an R8 image, declared in Batch 2's table with its range from `accumulate` to `specular`; the
  others read 1 B and AND in their own predicate. The assert in `DenoisePasses::record` (fresh
  accumulator ⇒ every history fresh) makes the bits valid.
- [ ] `traversal.glsl:306-315, 539` — every non-opaque candidate fetches corners, transforms them and
  computes `surfaceConeAt` before the medium and `!hasMask` early-outs; `passageAsk` rays have no cone
  and `coneBaseOf` returns before reading it. Read instance, primitive, barycentrics and t first; fetch
  corners only on the path that samples, and only where `coneWidth > 0`.
- [ ] `shading.glsl:464`, `bounce.comp:94-107` — the landing gather is skipped per lane
  (`INDIRECT_LIGHT_RATE` 0.5), so skipped lanes wait anyway (`landing` keeps 9.5 of 32 lanes busy).
  `bounce` knows the skip; append into two runs (gathering, not) with an indirect dispatch each.
  Same rays, new order.
- [ ] `bounce.comp:94-107`, `visibility.comp:400-418` — a workgroup is one subgroup (8×4, required 32):
  replace shared `atomicOr` and barriers with `subgroupBallot`/`subgroupBallotExclusiveBitCount`/
  `subgroupElect`; `visibility.comp:305-307` sun-glare counters through `subgroupAdd` first.

### Batch 11 — Shaders that move pictures

Each: `./omw shot --views=all --map --upscale=off --against=<dir>` and `./omw noise`, with the upscaled
pictures read where named.

- [ ] `storeBounced` (`bounced.glsl:54-68`) loads direct, both albedos, shadowed, lamped, specular,
  specular albedo, pane and pane albedo (56–72 B/px, ~37–57 MB a frame at 720p) to weigh the pane mask,
  at every pixel in `bounce` or `landing`; the composite or `atrouscompose` loads the same channels
  again. Weigh it in the composing passes (`compose.glsl`'s `composedLight` is shared) with
  `STAGE_RECORD` bound there; its range in Batch 2's table grows to the composite. The mask then
  divides the filtered light the upscaler reconstructs, which FSR's documentation asks to be stable.
  Moves every upscaled picture: read `./omw noise --upscale=native` and the pane and water places.
  Where the pictures refuse it, the bit-identical form stays open: skip the loads where
  `record.mPaneApart == 0` (the product is exactly nought there, since `pixelLit` is floored at 1e-6)
  and in a frame with no upscaler, measured as a branch.

### Batch 12 — Dispatch limits and vertex formats

Files: `shaders/lib/stage.glsl`, `trace/visibilitypass.cpp`, `device/requirements.cpp`,
`scene/structurebuild.cpp`, `components/rtx/scene/meshtable.hpp`, the shaders that read colours and
indices.

- [ ] BUG `stage.glsl:76-83`, `visibilitypass.cpp:174-179` — indirect X grows to pixels/32 or tiles,
  past Vulkan's guaranteed `maxComputeWorkGroupCount[0]` of 65535 at a 2560×1440 native trace (115,200)
  and 4K (259,200); nothing reads the limit. Read what NVIDIA, RADV under the shim and AMD's Windows
  driver report: where every target reports enough, require it in `requirements.cpp`; otherwise lay
  the lists over a Y dimension.
- [ ] `meshtable.hpp:126-129` — vertex colours are float3 and written white for every vertex of meshes
  that bring none. A run only where the mesh brings them, `NO_RUN` and a select as
  `mSecondTexCoordOffset` does. Exact.
- [ ] `structurebuild.cpp:23` — indices always u32; NIF shapes stay under 65,536 vertices. u16 per mesh
  where it fits halves those meshes' index blocks, one select in `committedHit`. Measure the index
  blocks' share of `ContentMemory` first; it is not worth a select if small.

### Batch 13 — Texture encoding

Files: `texture/{texture,bc7encodepass}.*`, `image/texturedata.hpp`, `shaders/texture/*`.

- [ ] `texture.cpp` `encodesChain`, `texturedata.hpp:236-241` — a file past `sLargestLooseChainSide`
  with no levels has its chain completed in BC7, its own level re-encoded with it (an image has one
  format), which moves that level by mode 6's shared low bit and doubles a BC1 source (10.7 → 22 MB for
  a 4096² chain). Keep the source's format for the chain: level 0 copied as the file's own blocks, exact,
  and only levels 1 and below encoded, a third of the texels, by a BC1, BC3 (BC1 colour, BC4 alpha) or
  BC5 kernel beside `Bc7EncodePass`. Vanilla files stand under the side and do not move.
- [ ] Normal maps without height to BC5 rather than BC7 mode 6 (Unreal `TC_Normalmap`, RTX Remix).
  Moves the mapped pictures.

### Batch 14 — Present and barriers

Files: `vulkanrenderer.cpp`, `present/{presenter,swapchain}.cpp`, `gui/guidrawer.cpp`,
`display/displaychain.cpp`, `trace/tracechain.cpp`, `framering.*`.

- [ ] `vulkanrenderer.cpp:553-556`, `presenter.cpp:182`, `swapchain.cpp:173` — a swapchain remake drains
  the device twice and destroys the old swapchain before making the new one, so a window resize idles
  the device at each configure event and a compositor shows a frame with no buffer. Where
  `VK_EXT_swapchain_maintenance1` is present (`PresentFences`): pass `oldSwapchain`, retire the old
  set until its present fences and blit values pass, no idle. Keep the idle path without it
  (Vulkan-Samples `swapchain_recreation`).
- [ ] `presenter.cpp` `take()`, `framering.cpp` `makeRoom` — under FIFO nothing bounds queued presents:
  `minImageCount + 1` images plus two frames in flight. Where `VK_KHR_present_wait` is offered, wait
  for present N−2 before the frame's input (DXVK's maximum frame latency); read the p99 with it.
- [ ] `guidrawer.cpp:90-97`, `presenter.cpp:250, 300` — the shown image (no storage usage) goes
  UNDEFINED → attachment → GENERAL → transfer source → GENERAL in three barrier commands. Leave it in
  transfer source after the GUI and batch the swapchain image's barrier with it. Merge the
  back-to-back `transition` calls at `displaychain.cpp:105/117/122` and `tracechain.cpp:202/219` into
  one `Barriers` each.

### Batch 15 — The kernel compile and harness-only work

Files: `trace/visibilitypass.cpp`, `components/rtx/common/parallel.hpp`, `framering.hpp`,
`pipeline/pipeline.hpp`, `device/device.cpp`.

- [ ] BUG `visibilitypass.cpp:334-344`, `parallel.hpp` — a work item is a whole tuple (6–7 kernel
  compiles) and the stop token is read between items: quitting, a throw after `mTracePasses`, or a
  renderer swap in the first minute of a cold start waits seconds (about a minute on AMD). One
  (tuple, stage) pair a work item, a tuple made when its last stage lands (an atomic count a tuple).
- [ ] `visibilitypass.cpp:319-325, 470` — tuples are queued `sun=false` first, so a daylight exterior's
  tuple compiles in the second half. With per-kernel items, `variantFor` publishes the tuple it wanted
  and the hands take its kernels first (Dolphin's hybrid ubershaders, Godot 4.4). A harness run waits
  for every tuple as before, so its order changes no frame.
- [ ] `framering.hpp:220` `DigestPass` — a pipeline, two images and a buffer made at every game start
  for read-back runs only. Make it where the run asks for read-back, at the run's start.
- [ ] `pipeline.hpp:83` `CAPTURE_STATISTICS`, `device.cpp:398` `reportPipeline` — every pipeline's
  statistics queried and formatted, background tuples included; only the harness and the spill gate
  read them. Gate both on a run option.

## Measurements before a decision

Each goes to `docs/rtx/research/`, dated, with the figure that decides it.

- `NO_DUPLICATE_ANY_HIT` on every geometry (`structurebuild.cpp:32`): the DXR spec says allowing
  duplicates helps traversal; set it only on meshes some placement may blend, A/B.
- One BLAS per template model with a geometry per shape (placements reach 80,324 at the largest
  place): fewer instances and less overlap (NVIDIA RT best practices), against per-shape gates and
  `NightDaySwitch` branches through the geometry index.
- Content pool fragmentation after a long walk (reserved against live bytes) before any VMA
  defragmentation or `vkCmdCopyAccelerationStructureKHR` relocation.
- The pinned arithmetic's cost (`spirvpin.hpp`): one `./omw release bench` A/B of pinned modules
  against contraction allowed, the stages' and the denoiser's medians and p99. A record of what the
  convention costs, not a proposal to change it.

## Dropped

- Releasing a `PreparedModel`'s vertex arrays (~68 B a vertex) after its first adoption, beside
  `SceneDesc`'s copy: the frame adopts a model again whenever it let the model go while the reader
  still files it (`CellHolds::release`, then `adoptParts`), and Batch 9's idle models are that case by
  design. Without the arrays, that adoption must walk the template or read the file again, which
  "RAM before the disk" refuses. The user's earlier choice to release the reader's copy gives way to
  that rule; the arrays stay for as long as the reader files the model.

- `ContentCache` (`preprocess/contentcache.hpp:22`), which holds nothing and reports nought in
  `mHits`, `mKeyMs` and `mKeyBytes`: kept as a staged design, by the user's decision.
- A frame graph: see Batch 2's target, which takes its one gain here.
- A command pool per frame slot (`commands.cpp:71`): microseconds, and it conflicts with deferred
  batches riding any submit.
- ReSTIR DI: at seen pixels only the held lamp's visibility is noisy, which the shadow denoiser filters;
  at landings it is ReSTIR GI's ground, declined.
- RIS candidates drawn from a per-cell power table (RTXDI's local-light PDF) instead of uniformly: it
  matters only in cells mixing candles with braziers, and moves pictures.
- f16 arithmetic: the pinning refuses a module's float environment, and every converted value moves
  pictures for stages bound by ray tracing and memory.
- Octahedral snorm16 normals (Cigolle et al. 2014), 8 B a vertex less: approximate, and every normal
  moves; exact float3 stays.
- The top level and its scratch made at the world's room (`sceneacceleration.cpp:406-407`): a stated
  trade of tens of MiB against a structure made on a crossing frame.
- A helper shared by `VulkanRenderer::renderFrame` and `PictureTracer::trace`: the differences
  outnumber the six shared calls.
- Folding `STAGE_BOUNCE`'s weight into `STAGE_THROUGHPUT` (stage-memory's third item): `bounce.comp:60-62`
  reads the four neighbours' throughput (`shownSurfaceLift` → `surfaceLiftAt`, `stage.glsl:253-279`) in
  the same dispatch, so writing it there races. The modulation word would fit `STAGE_RECORD.y` (its
  `mTmin` is read only by `media.comp:74`), but the weight's 12 B have no home; Batch 2's arena
  recovers the image's 16 B instead.
