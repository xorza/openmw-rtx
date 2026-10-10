# GPU pipeline review

Tree at `8f61a220f0`. Line numbers refer to that commit.

Scope: every shader in `components/rtx/shaders` and `components/rtxvulkan/shaders`, the host code that
records and feeds them, the shared GPU structures, and the frame and device layers around them. Six
reviewers read the code in parallel, one area each: the primary trace, the denoiser and upscaler,
media/water/sky/sprites, the scene and texture data, the device/frame/display layers, and the
GLSL↔C++ interface with the pass graph. Nothing was built or run, so every cost below is arithmetic
from formats and extents, or a figure quoted from a code comment. Each batch says how to measure it.

The findings are in **batches**. One batch is one change set that touches one area, so you can do
it in one go. The batches are in order of importance: crashes and contract guards first, then memory,
worst-frame spikes, and then throughput.

Labels on each finding: **kind** (bug, latent, perf, simplify, robustness), **severity** (H/M/L)
and **confidence** (H/M/L). The ID is the reviewer's own (TRACE, DENOISE, MEDIA, SCENE, FRAME, XCUT).
When two reviewers found the same thing, the finding has both IDs.

## Summary

| # | Batch | Findings | Why it is here |
|---|---|---|---|
| 1 | Device loss and contract guards | 6 | A crash, a wild read, or a silent drift waiting for a trigger |
| 2 | Memory budget and allocation | 6 | A throw instead of a refusal on 6–8 GB / 256 MiB BAR cards. Room lost for good |
| 3 | Worst-frame spikes at arrival and departure | 6 | One frame absorbs a sweep, a build or a rebuild |
| 4 | G-buffer and fill diet | 6 | 50–180 B/px of dead traffic on every vanilla frame |
| 5 | Denoiser pass graph | 7 | About 13 queue drains where 5 would do, a composite round trip, transients that could share memory |
| 6 | Sprites and fog | 5 | Worst-frame sprite walks, a serial shade chain, 32F fog history |
| 7 | Lamp sampling | 3 | O(lamps in cell) per water pixel, up to 3×256, and an unbudgeted darkening walk |
| 8 | Display and presentation | 6 | Two extra full-frame copies and submits, input latency, bloom order |
| 9 | Water: ripples and waves | 5 | A frame-rate-dependent wake, idle work, driver-precision twiddles |
| 10 | Scene record layout for the trace | 4 | Dependent loads and wide rows in the hottest loops. A/B first |
| 11 | Layered ground in the trace | 2 | A stage split that does not exist, and bounce hits that sum the whole stack |
| 12 | Housekeeping | 4 | Pipeline cache, capture flags, duplicate probes, stale comments |

60 findings are open: 0 high, 16 medium, 44 low. No reviewer found a GLSL/C++ layout,
binding or format mismatch. The interface checks (`pushDisagreement`, `bindingDisagreement`,
`storageformat.h`, `mayRoundTowardNought`) hold.

---

## Batch 1 — Device loss and contract guards

Each item is a crash, a wild read, or a silent drift that needs a trigger. They are small and
independent, so one pass can fix them all.

### TRACE-7: The light-grid bounds test lets a NaN position through to an out-of-bounds buffer-reference read
robustness · L · conf M — `shaders/lib/lights.glsl:88-98`

`any(lessThan(cell, 0)) || any(greaterThanEqual(cell, size))` is false for NaN. `uvec3(NaN)` is
undefined, and it then indexes `lightCellAt` and `lightListAt` through buffer references with no
robust access. One NaN position becomes a wild read, and possibly a lost device, where it would
otherwise be one counted pixel.

**Direction:** write the test so that NaN fails it: `!all(greaterThanEqual(cell, vec3(0)) && lessThan(cell, vec3(size)))`.
Apply the same form to the other float→index conversions on the path (`specularTableTaps`, `maskWeightIn`).

### FRAME-13: The pinning files FMin, FMax and FClamp as fixed, but GLSL.std.450 leaves the result undefined for a NaN operand
robustness · L · conf M — `spirv/spirvpin.cpp:148-157`, `spirv/spirvpin.hpp:9-12`

The header promises that every float result is fixed by the specification. For FMin/FMax, "which
operand is the result is undefined if one of the operands is a NaN". `SignedZeroInfNanPreserve` does
not choose an operand. So a guard such as `max(value, 0.0)`, written to absorb a NaN, gives an answer
that depends on the compile. TRACE-7 shows that such guards matter.

**Direction:** rewrite the F-variants to NMin/NMax/NClamp, which fix the NaN answer, or class them as
bounded and say so in the header.

### XCUT-8: The module-interface check covers push blocks only. Uniform-block sizes and buffer-reference array strides are not held to the C++ type
robustness · L · conf H — `spirv/spirvinterface.cpp:200-212,385-388`, `pipeline/shadercode.cpp:95-107`

`readInterface` computes a block end only for `StorageClassPushConstant`. The frame block (1624 B),
the FSR blocks, `FrameCounts` and every `buffer_reference` table's `ArrayStride` are checked only by
C++ `static_assert`s against hand-computed numbers, which pin the C++ side only. The one way the two
sides can drift in scalar layout (a nested struct whose size is not a multiple of its 8-byte
alignment, followed by another member) passes every check. No struct has that shape today.

**Direction:** report each uniform/storage block's end and each runtime array's `ArrayStride` in the
same walk, and hold them to the C++ `sizeof`, as `pushDisagreement` does.

### SCENE-13: Two sentinel and index limits are held by coincidence or a silent mask
robustness · L · conf H — `scene/scenebuffers.cpp:76-85`, `rtx/common/index.hpp:10`, `rtx/shaders/scene.h:31`, `scene/sceneacceleration.cpp:472`

`toGpu` copies `sNoIndex = ~Index{0}` into fields that the shaders test against `Shaders::NO_TEXTURE`,
with nothing to assert that the two are equal. `placeRow` writes `instanceCustomIndex = slot & 0xFFFFFFu`
with no assert, and the placement table grows without a cap.

**Direction:** `static_assert(sNoIndex == Shaders::NO_TEXTURE)` beside `toGpu`, and
`assert(slot < (1u << 24))` in `placeRow`.

### DENOISE-6: Held distances are decoded with this frame's distance scale, not the scale they were stored at
latent · L · conf H — `shaders/lib/surfacematch.glsl:24,161,174`, `trace/denoise/denoisepasses.cpp:73`

`heldSurfaceOf` stores `distance * mDistanceScale` (`RANGE / mFar` of the writing frame).
`heldSurfaceMatches` divides by this frame's scale. It is correct only because the far plane is the
constant `sFarPlane`. A per-frame far plane without a cut would scale every held distance and break
every temporal plane test, with nothing to report it.

**Direction:** store at a fixed scale, or carry the previous scale in `HistoryConstants`, or assert
that far is unchanged across frames that keep their history. DENOISE-7 (batch 5) removes the coupling
completely.

### DENOISE-4: The shadow tiles pass gathers the history even on a reset frame. In a picture chain that read races the accumulator's write
robustness · L · conf H — `shaders/trace/denoise/shadowtiles.comp:324-327`, `trace/denoise/denoisehistory.cpp:296-300`

The `RTX_HISTORY_SHARES` gather runs first, and only afterwards does `if (frame.mReset != 0u) shares = vec4(0.0)`
discard it. Every other temporal pass guards the gather. In a `TracePast::Dropped` chain,
`before() == now() == [0]`, so `mHeldSurface` is the image `accumulate.comp` writes in the same
command buffer, with no barrier that names it. No pixel is wrong, but synchronization validation
reports a hazard, and every reset frame pays 16 loads per pixel per field for nothing.

**Direction:** put the gather behind `frame.mReset == 0u`.

---

## Batch 2 — Memory budget and allocation

On the target RTX 20 cards (6–8 GB, 256 MiB BAR without Resizable BAR), these items turn a budget
refusal into a throw, or keep room lost for good.

### FRAME-8: Host-written memory is essential, never refused and unbounded, with doubling growth, on target hardware whose BAR heap is 256 MiB
robustness · L · conf L — `device/memory/buffer.cpp:27-28`, `device/memory/memory.cpp:247-273`, `device/memory/slottable.hpp:111`, `device/memory/growablebuffer.cpp:19-25`

Every `SlotTable` copy, the poses, lights, sprites, emitters, SBTs and the GUI vertices are
`DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT`, which is the BAR window. They grow ×2, with the old copy
buried for two frames (3× transiently), through `take`, which throws `DeviceError` on out-of-memory.
`mHostWrittenBytes` is only printed. To settle: read `MemoryReport::mHostWrittenLive` at its peak in
a crowded exterior.

**Direction:** report or assert against `mHostWrittenBytes`. Give the large per-frame tables a
device-local staged path (as `BlockedBuffer` does) when the BAR heap is small.

### SCENE-3: The BLAS build scratch is the sum over every mesh in one build call. It is taken as essential memory, kept at its high-water mark and not reported
robustness · M · conf M — `scene/bottomlevelstore.cpp:115-128,229-253`, `scene/devicescene.cpp:45-68`, `device/memory/growablebuffer.cpp:8-17`

`build()` places each mesh's scratch end to end and records one build over all of them. The scene
build calls it with every mesh of the world. `mScratch` and `mArrived` (all static positions) go
through `MemoryUse::Essential`, before the structures ask for content room, and they never shrink.
`getStructureBytes` and `SceneStats` do not count them. On a card near its budget, the process fails
where the device should refuse meshes.

**Direction:** build in chunks under a fixed scratch budget, with a barrier between chunks. Take the
scratch and arrival buffers as content memory, or trim them after the build, and report them.

### SCENE-4: Staging blocks are never given back, so a whole-scene upload stays pinned in host memory until exit
perf · M · conf M — `device/commands.cpp:128,223-238`, `scene/devicescene.hpp:46-52`

`takeStaging` reuses or makes `max(bytes, 8 MiB)` blocks, and nothing destroys a spare. A world build
with a texture replacer stages everything in one batch, possibly more than 1 GB, and keeps it.

**Direction:** trim spares above a fixed reserve after their batch retires, a few per frame. Or cap
a batch's staging and split a scene build across submits (this fits with SCENE-3's chunks).

### SCENE-2: The normal-spread means are a large transient. Summed across an arrival, they can hold every texture of that arrival to a smaller side for good
perf · M · conf M — `texture/texture.cpp:79-86,202-209,483-580`, `texture/texturecost.cpp:73-82`, `shaders/shared/normalspread.h:48-51`

Each normal map makes an RGBA32F image at half side with a full chain, alive until the batch runs.
For a 2048² BC5 map that is about 22 MB of means against 5.6 MB that the map keeps. `costAt` sums the
transients over the arrival, and `chooseSide` halves **every** file until that peak fits. With 40
replacer normal maps that is about 0.9 GB of transient, which downsizes colour and normal maps
permanently for memory that is freed a frame later.

**Direction:** build the spreads through one fixed-size reused means scratch (in groups, with a
barrier between groups), or fuse levels in shared memory. Price the bounded scratch once per arrival.

### FRAME-15: The content ceilings count the memory outside the allocator twice
robustness · L · conf M — `device/memory/memory.cpp:392-422`, `apps/components_tests/rtxvulkan/device/memory/memory.cpp:364-377`

`owed = usage − blockBytes` is subtracted from the budget, and then `usage + new` (which includes the
same outside memory) is compared against that ceiling. The "owed twice" rule is for uses that could
be made again while the old one stands. The swapchain is destroyed before it is made again, so the
rule does not cover it. A 7680×2160 swapchain of 3 images is about 200 MB lost from the content room.

**Direction:** owe the outside memory once, or name what is made again beside it. Update the test's
hand computation.

### SCENE-8: Completed chains stay loose RGBA8, and the stated reason ("no encoder") is out of date
perf · L · conf M — `rtx/image/texturedata.hpp:215-226`, `texture/texture.cpp:221-245`

A one-level BC1/BC3 file (vanilla ships 187, the rain among them) becomes an RGBA8 chain: 8× the BC1
bytes. `Bc7EncodePass` now exists and encodes every ground composite.

**Direction:** encode the completed chain to BC7 through `Bc7EncodePass` (`mWeighsAlpha` on), or keep
RGBA8 and replace the comment with the real reason (level 0 kept byte-exact).

---

## Batch 3 — Worst-frame spikes at arrival and departure

AGENTS.md: "no frame absorbs a … eviction sweep … or rebuild the others didn't." Measure each item
with `./omw release bench --frame-times` on a ring walk before and after. Report the p99 and the worst frame.

### FRAME-2 / SCENE-14: The graveyard frees everything the timeline has passed in one collect, so all of a crossing's burials land on one frame
perf · M · conf M — `device/graveyard.cpp:40-66`, `device/retiring.hpp:24-33`, `texture/texture.cpp:843-859`, `scene/bottomlevelstore.cpp:64-77`

`Retiring::releaseThrough` hands over every passed entry at once. A departing cell buries hundreds of
images, views, structures and ranges under one stamp. Two frames later, all of the `vkDestroy*` and
`vmaFreeMemory` calls (and possibly a `vkFreeMemory` of a 64 MiB block) run in that one frame.

**Direction:** give `freeThrough` a budget per collect (handles or bytes), and keep the remainder for
the next collect. `collectIdle` still frees everything.

### SCENE-5: All of an arrival's BLAS builds land on the arrival frame
perf · M · conf L — `scene/sceneacceleration.cpp:157-170`, `scene/bottomlevelstore.cpp:79-311`, `shaders/scene/toplevelpack.comp:65-69`

`buildArrived` builds every arrived mesh in one call, bounded by cells but not by triangles. A row
whose BLAS does not stand is already a gap that the pack leaves out, so delaying is safe by
construction. Textures and composites have a rota (`sCompositesPerFrame`). BLAS builds have none.

**Direction:** measure the `Blas` zone's p99 first. If it spikes, give the builds a per-frame triangle
budget (FIFO), as `CompositeQueue` does.

### SCENE-9: Loose formats are widened on the frame thread at the hand-over, and then stay uncompressed
perf · L · conf M — `rtx/scene/scenetextures.cpp:76-121`, `rtx/image/imagedescription.cpp:99-130,320-375`

`SceneUploader::hand` calls `describe` on the arrival frame. Each `isWidened` format (RGB8, L8, LA8,
16-bit, packed) is widened texel by texel, with a per-channel `switch` in the loop. A 2048² TGA is
about 5.6 M texels on the arrival frame, and it then stays at 22 MB.

**Direction:** widen on the cell reader thread, or upload raw and widen in a compute pass. Optionally
encode widened colour files with `Bc7EncodePass`.

### SCENE-11: The TLAS is rebuilt a second, redundant time on the frame after any change
perf · L · conf M — `scene/sceneacceleration.cpp:337-353`, `device/memory/slottable.hpp:52-61`

`SlotTable::write` owes a changed row to both copies, and `place` rebuilds when
`mRowTable.owes(slot)`. There is one `mTopLevel`, so on the next frame the other copy's debt causes a
pack and a full TLAS build over identical rows.

**Direction:** keep a generation counter that `placeRow` bumps. Skip the build when only the copy's
debt remains, but still pay the copy, because the pack reads it.

### FRAME-3: Any change to the frame reserve clears and rebuilds the whole world synchronously, even far below the ceiling
perf · M · conf M — `vulkanrenderer.cpp:206-228,292-337`, `apps/openmw/mwrender/rtx/rtxwindow.cpp:119-146`

`rebuildsWorld = reserve != getFrameReserve() && world != nullptr`. An upscale mode change, a
resolution change or a window resize at Native rebuilds every BLAS and uploads every texture in one
flushed batch: a stall of seconds. In the common case it makes the same scene again.

**Direction:** rebuild only when the new ceiling is below what content now takes, or when the old
room refused or downsized something that the new room admits. Otherwise call `reserveFrame` only.

### MEDIA-2: A picture fills and integrates the fog volume over every column of the largest picture ever asked, not its own
perf · M · conf M — `trace/visibilitypass.cpp:620-664`, `picturetracer.cpp:42-44`, `trace/tracechain.cpp:97-103`

`TraceChain::grow` keeps the maximum extent. After the 512×1024 doll preview opens, every 256×256
local-map tile traces 43×86 instead of 23×23 columns × 64 slices: 3.5–7× the fog work. A cell
crossing traces a row of map tiles on the frame that already carries the arrival. The comment's
reason (interpolation at the edge) needs one extra column and row, not the whole image.

**Direction:** trace and integrate `min(columns, groupsFor(extent, FOG_VOLUME_SCALE) + 1)` per axis.
Keep `mFogColumns` as the image's grid, because readers normalise by it.

---

## Batch 4 — G-buffer and fill diet

Every vanilla frame writes and filters channels that nothing reads in that frame. The common blocker
is the digest: `GBuffer::begin` discards every channel from `UNDEFINED`, and `visibility.rgen:504-507`
writes every channel to keep the digest deterministic. Solve that once (clear the conditional
channels at creation and do not discard them, or hash a channel that a frame flags as constant as
its constant), and then gate the stores. Measure with `./omw release bench` and `./omw kernels`.

### TRACE-2 / XCUT-6: The lobe and pane channels are stored on every pixel, though no pass reads them when nothing is mapped or layered
perf · M · conf M — `shaders/trace/visibility.rgen:505-528`, `shaders/trace/denoise/composite.comp:104-105`, `trace/denoise/denoisepasses.cpp:45-61`, `trace/gbuffer.cpp:235-245`

`specular` and `specularAlbedo` (16 B/px) are stored unconditionally. The composite reads them only
under `mLobed` (= `mMapped`), and the glossy filter runs only when mapped. The four pane channels
(32 B/px) are stored unconditionally too, and the pane filter averages zeros where no layer stands.
That is 48 of 138 B/px constant in a vanilla frame: about 177 MB of writes per frame at 2560×1440.

**Direction:** put the two lobe stores behind `HAS_MAPS` (the tuple already has it). Add a per-frame
fact "a see-through layer can be met" (`InstanceCounts` does not count see-through instances today)
as a specialization constant. It folds `peelLayers`, gates the pane stores, and lets the pane filter skip.

### XCUT-1: The fill channel and its whole filter chain run on every frame, though the fill is multiplied by zero wherever a material's ambient equals its diffuse
perf · M · conf M — `shaders/lib/compose.glsl:12-14,29`, `shaders/lib/traversal.glsl:1297-1299`, `shaders/lib/records.glsl:31-33`, `shaders/trace/denoise/atrous.comp:290`

`(ambientAlbedo − albedo) * fill` is zero to the bit when `mAmbientColour == mDiffuseColour` and the
two vertex-tint flags agree. Despite that, the trace stores `fill` and `ambientAlbedo`, and the
accumulator, the clamp, all four wavelet levels and the composite read and write the fill. That is
about 130 B/px of traffic (about 0.45 ms of bandwidth at 1080p as an upper bound), plus 40 B/px of
history memory and shared-memory footprint in the widest wavelet level.

**Direction:** count per placement the materials whose ambient term differs (beside `mMapped`), and
specialize the fill out of the whole chain when the count is zero, as `HAS_MAPS` does for the lobe.
The wide level keeps its frame count in the fill's alpha, so move that count first (for example, into
the moments). **Settle first:** count the materials with ambient ≠ diffuse over the suite's views. If
vanilla has them everywhere, this finding is void.

### XCUT-7: The specular albedo is rgb9e5-exact but stored as four halves, and four albedo channels spend a quarter of their texel on a constant alpha
perf · L · conf H — `shaders/lib/shading.glsl:480-484`, `shaders/lib/payload.glsl:162-164,215`, `rtx/shaders/gbuffer.h:64`

The payload already carries the specular albedo as one rgb9e5 word. It is stored as `GBUFFER_ALBEDO`
(rgba16f, `a = 1`). `albedo`, `ambientAlbedo`, `paneAlbedo` and `lift` also store a constant 1 in alpha.

**Direction:** store the specular albedo as `r32ui` rgb9e5 and unpack it in the composite. After
XCUT-1 decides the fill, check whether the ambient albedo can travel as a ratio or share a texel.

### XCUT-5: The puffs composite runs, with two transitions of the shown image, on frames that have no sprite and no presence
perf · L · conf M — `display/displaychain.cpp:112-115`, `trace/spritebin.cpp:68`, `shaders/trace/spritecomposite.rgen:232-245`

`SpriteTables::mPuffs` is known on the host. The launch over the traced extent still runs, and on
arms pixels it writes back what was already there. The trace's `puffs` store (8 B/px) is also
unconditional.

**Direction:** skip `recordSpriteComposite` and its transitions when `!mPuffs`. Store `puffs` under
`frame.mPuffsInFrame != 0u`. First confirm that a shell's `mAdded` comes only through a presence.

### TRACE-5: The payload carries six radiances as full floats even where the run stores radiance as halves
perf · L · conf L — `shaders/lib/payload.glsl:10-16,146-185`, `trace/gbuffer.hpp:22-26`

18 of the 30 payload words are fp32 radiances. The comment justifies that for references, but
references run at `RadianceWidth::Summed`. A played frame stores them through RGBA16F.

**Direction:** pick the payload layout per pipeline by radiance width. Keep it only if `bench` and
`kernels` show a gain on both vendors (RADV passes the payload through registers or scratch).

### XCUT-12: A half's largest value and a truncation to half are written inline in the trace, beside the library that names both
simplify · L · conf H — `shaders/trace/visibility.rgen:484-485`, `shaders/lib/halfround.glsl:33,53-98`

`vec3(65504.0)` repeats `HALF_LARGEST`, and `& 0xffffe000u` is a third rounding to half, beside
`roundedToHalf` and `nearestHalf`.

**Direction:** move the truncation into `halfround.glsl` as a named function that states its range
precondition, and use `HALF_LARGEST`.

---

## Batch 5 — Denoiser pass graph

All of these items touch `trace/denoise/*` and the denoise shaders, so do them in one go. Measure
the `denoise` zones from `--json`: medians and p99. When the families overlap, the per-family zones
merge, so add per-stage timestamps before the change.

### DENOISE-2 / XCUT-2: The independent denoiser families are recorded one after another, so their internal barriers drain the queue about 13 times. The sky and lamp shadow fields repeat all their geometry work
perf · M · conf M — `trace/denoise/denoisepasses.cpp:79-115`, `trace/denoise/shadowpass.cpp:41-47,79-134`, `trace/denoise/historyclamppass.cpp:34-46`, `trace/wavepass.cpp:131-133` (the pattern done right)

Order today: accumulate │ clamp; sky mask │ tiles │ f0 │ f1 │ f2; lamp mask │ … │ f2; specular │
clamp; pane │ clamp; `ready`. Each `│` is a stage-wide COMPUTE→COMPUTE barrier, so it waits on every
earlier dispatch. The sky field, the lamp field, the glossy filter and the pane filter read nothing
that the others write (the comment at `denoisepasses.cpp:100-102` says so). The two shadow fields
also read the same surface square, rebuild the same positions and compute the same
`coplanarWeight`/`facingWeights`. Only the bits, the brightness test and the tile class differ.

**Direction:** record in stages with one barrier each:
{accumulate, both masks, specular, pane} │ {clamp, both tiles, glossy clamp, pane clamp} │ {both f0} │ {both f1} │ {both f2} │ wavelet.
Fuse the two shadow fields into one pipeline per step, with a specialization constant for the field
set. That gives about 5 drains instead of 13, and half the shadow dispatches.

### DENOISE-3 / XCUT-4: The composite is a separate full-resolution pass that reads back what the last wavelet level just wrote
perf · M · conf M — `trace/denoise/atrouspass.cpp:109-165`, `trace/tracechain.cpp:199-224`, `shaders/trace/denoise/composite.comp`

Only the composite reads `Denoised::mIndirect`/`mFill`, at the same pixel. Every other composite
input is final before the cascade (`ready`). That is 32 B/px plus one dispatch and one drain per
denoised frame: about 66 MB at 1080p and 236 MB at 5120×1440.

**Direction:** give the last narrow level a specialization that composes and stores `direct` (about
20 push descriptors, under 32). Keep `composite.comp` for unfiltered and summed frames. Measure the
register pressure first.

### DENOISE-8 / XCUT-11: Transients with lifetimes that do not overlap each hold their own memory for the life of the chain
perf · L · conf M — `trace/denoise/denoisehistory.cpp:98-167`, `trace/gbuffer.cpp:66-86`, `upscale/upscaler.cpp:321-360`, `display/bloompass.cpp:75-88`

The denoiser keeps about 209 B/px. `FastBlended`, the shadow scratches and the two `*FastBlended`
are dead before the cascade starts, and `Narrow`/`FillNarrow` live only inside it. After the
composite, 12 G-buffer channels (96 B/px) have no reader, while the upscaler's transients (about
16 B/px) and the bloom pyramid each have their own allocation. The upscaler already aliases its
`Intermediate`, so the technique is already in the tree.

**Direction:** alias the cascade's scratch onto the pre-cascade scratch, and the upscaler's and bloom's
transients onto the channels that are dead after the composite. Turn the aliasing off for runs that
read a channel back after the composite. **Interaction:** DENOISE-2's field fusion removes the serial
reuse of the shadow scratch, so plan the aliasing after the fusion.

### DENOISE-7: The held surfaces are a re-encoding of last frame's surface channels, written every frame, and they lose the arm flag
simplify · L · conf M — `shaders/trace/denoise/accumulate.comp:223,329`, `shaders/trace/denoise/pane.comp:115`, `shaders/lib/surfacematch.glsl:21-25,154,174`

`heldSurfaceOf` is a pure function of `CHANNEL_SURFACE` and a constant, written whole by two passes
every frame (2 × 8 B/px written, 32 B/px allocated). Its `abs` drops the arm flag that
`packSurfaceDistance` keeps in the sign, so `heldSurfaceMatches` rebuilds every history texel through
the centre pixel's eye.

**Direction:** keep the previous frame's surface channels instead (paired in `GBuffer`), and decode
them with `unpackSurfaceNormal`. That restores the flag and removes DENOISE-6. Weigh it against the
octahedral decode at 4 taps in each of the four temporal passes.

### DENOISE-10: The same texel is loaded twice in one invocation in several passes
perf · L · conf L — `specular.comp:143-150`, `pane.comp:94-100`, `shadowtiles.comp:324-333`, `accumulateclamp.comp:143,159,265`, `atrous.comp:265,289,313,331`

The `holds` predicate of `RTX_HISTORY_SHARES` loads each tap, and the sum loop loads it again. The
clamp loads `surfaceChannel` three times. The narrow wavelet fetches the centre as the centre and
again as tap (0,0).

**Direction:** keep the first load in a local. Check the ISA first, because the driver may already merge them.

### DENOISE-5: A dead barrier entry, and two wrong comments: the cascade reads no moments and no puffs
simplify · L · conf H — `trace/denoise/denoisepasses.cpp:110-115`, `trace/denoise/atrouspass.hpp:43-44`, `shaders/shared/atrous.h:41-47`

`ready` transitions `mMoments` "for the history fix", but `atrous.comp` binds no moments image (the
count comes from `fillSource.a`). `AtrousPass::record`'s doc says that it reads the puffs, but it binds none.

**Direction:** remove `mMoments` from the list and correct both comments.

### DENOISE-9: The stated reason that narrow wavelet levels get no shared-memory tile is wrong for steps 2 and 4
robustness · L · conf M — `trace/denoise/atrouspass.cpp:66-69`

"At their strides no texel is reached twice." An 8×8 group's 3×3 taps read each texel 4× at step 2
and 2.25× at step 4. The measurement can still be right (L1 catches the reuse), but the wrong reason
hides an untried variant.

**Direction:** correct the comment. Measure a 12×12 tile for step 2 only, once.

---

## Batch 6 — Sprites and fog

### MEDIA-4: An unbinned tile walks every sprite one load at a time, though a missed emitter's sprites are contiguous
perf · M · conf H — `shaders/lib/sprites.glsl:640-670`, `shaders/lib/spritelist.glsl:94-101`, `rtx/shaders/scene.h:1058-1059`

On the unbinned frame (which `scene.h` names "the worst frame of a run": a storm that doubled, a first
step into rain), each pixel past the overflow loads every sprite in the scene only to read
`mEmitter`, and then `continue`s while `missed`. In an unbinned walk, the slot is the sprite index.

**Direction:** when an emitter is missed in the unbinned case, set `slot = emitter.mFirst + emitter.mCount − 1`.
For a storm with 20 emitters, that is about 20 loads plus hits instead of about 2000.

### MEDIA-5: `spriteruns.comp` walks every sprite for every tile, including tiles whose run is empty or full
perf · L · conf M — `shaders/scene/spriteruns.comp:43-104`

A tile with a count of zero still loads every rect, and the workgroup runs every stride with a barrier.
The cost is tiles × sprites (32 400 tiles at a 3840×2160 traced frame).

**Direction:** treat `slot == end` as not present. Keep a shared count of tiles that are still
filling, and `break` uniformly when it reaches zero.

### MEDIA-8: The `SpriteListSize` floor is applied on every frame, not only before the first report as documented
robustness · L · conf H — `rtx/frame/spritelistsize.cpp:15-18`, `rtx/frame/spritelistsize.hpp:21`

`max(mCapacity, floor, 2·reported)` on every call means a known report never lowers the target below
`sprites × tiles / 64`. A storm of 20 000 drops at a 3840×2160 traced frame sizes about 40 MB per bin
where about 40 k entries are used, and a rise is a grow on the frame path.

**Direction:** apply the floor only while no report has landed, or correct the header to match the code.

### MEDIA-3: The sprite self-shadow pass is a serial chain of two dependent global loads and two barriers per sprite, on two workgroups per emitter
perf · M · conf L — `shaders/scene/spriteshade.comp:141-164,365-394`, `trace/spritepasses.cpp:174`

Per sprite in depth order, every lane loads `run.at[...]` from a `coherent` buffer, then the sprite
that it names, then waits at two barriers. Weather billboards (snow, ash, blight) qualify, and only
`2 × emitters` workgroups are alive.

**Direction:** stage the sorted run 1024 at a time into shared memory in parallel. The serial loop
then reads only shared memory, with the same order and the same answer. Measure the `shade` zone at
a snow or ash place and at Vivec first.

### MEDIA-1: The fog volume's two history images are RGBA32F, though the denoiser solves the same truncating-store problem in halves with stochastic rounding
perf · M · conf M — `shaders/shared/fogvolume.h:22-30`, `trace/fogvolume.cpp:63-67,127-131`, `shaders/trace/fogintegrate.comp:97-108`, `trace/denoise/denoisehistory.cpp:176-187`

The header's reason is a measured truncation of a half store. `loopsKeepTheirPrecision` already
permits a feedback image in such a format when its store is `Store::RoundedAtRandom` (`roundedToHalf`).
Four full-grid volumes at 16 B: about 47 MB at 1707×960 and 236 MB at a 3840×2160 traced frame.
`fogintegrate` makes 18 RGBA32F fetches per froxel per slice from them.

**Direction:** store RGBA16F through `roundedToHalf` with a fog seed, and use the denoiser's rule in
the static_assert. Check with `./omw release noise` at the fog places and an A/B on `air`/`column`.

---

## Batch 7 — Lamp sampling

### TRACE-1: Every split hit walks every lamp in its cell, up to 256, with the full glossy lobe per lamp. The comment says the water legs do not
perf · M · conf H (behaviour), M (cost) — `shaders/lib/shading.glsl:372-375,199-204`, `shaders/lib/water.glsl:107`, `shaders/lib/lights.glsl:131,444-458,566-618`

`weighLamps(..., split ? 0u : frame.mLampCandidates)`, where 0 means every lamp. The comment above
says that "a water leg" takes the fixed count, but `waterRay` shades each leg with
`shadeAtPathEnd(..., true, ...)`, so `split` is true. A shoreline pixel adds the bed. One water pixel
can walk the cell list three times (reflection, refraction, bed), up to 3×256 evaluations, each with
a 40-byte load, `lampAt`, a PCG step and, on a glossy surface, the full GGX `reflectionAt`. Every
other path end pays 8. Canals at night are the worst case.

**Direction:** keep the exact unshadowed sum over all lamps, but with only falloff, cosine and
Fresnel. Draw the reservoir from `LAMP_CANDIDATES` uniform candidates weighed with the full lobe (RIS
stays unbiased). At minimum, correct the comment. Measure on a lamp-dense water view, and check
`./omw noise` for the variance cost.

### TRACE-8: `darkeningAt` walks every darkening lamp at every gather, with no budget, even when no lamp was held
perf · L · conf H — `shaders/lib/lights.glsl:626-638`, `shaders/lib/shading.glsl:398-402`

Its result only multiplies `lampsArriving`, which is zero when `kept.mWeight == 0`. Negligible in
vanilla. Unbounded with mods that place many negative lights.

**Direction:** skip the walk when `kept.mWeight == 0`. Give the far-hit, pane and leg calls the same
candidate budget as the positive lamps.

### TRACE-9: The blue-noise comment says that the R2 pairs are never read together, but the bounce pair and the sun-disc pair are read at the same hit
robustness · L · conf H — `shaders/lib/bluenoise.glsl:25-36`, `rtx/shaders/scene.h:159,178`, `shaders/lib/shading.glsl:253,953`

`STREAM_BOUNCE` and `STREAM_SUN_DISC` both turn by R2, and `shadeSolid` reads both at the same pixel,
so their offset never changes over time. This is harmless today (separate terms, separate filters),
but the stated invariant is false.

**Direction:** give the sun pair its own irrational step (as the lamp pair has), or correct the comment.

---

## Batch 8 — Display and presentation

All of these items touch `display/`, `gui/` and `present/`.

**Interaction to decide first:** XCUT-3 computes the histogram inside the bloom's first halving.
FRAME-7 wants the exposure recorded *before* the bloom, so that the bloom's Karis weight uses this
frame's exposure. You cannot have both. Either keep a one-frame-late Karis weight on purpose (and
correct the comment's reason), or keep the histogram separate and apply FRAME-14's 2×2 subsample to it.

### FRAME-6: After the tone curve, every frame makes two more full-frame copies in two more submits
perf · L · conf M — `gui/guidrawer.cpp:46-90`, `present/presenter.cpp:179-254`, `vulkanrenderer.cpp:558-580`

`GuiDrawer::draw` always draws the picture whole into `shown` and submits. `Presenter::present` then
blits `shown` to the swapchain in a second submit, and each command buffer opens with a full head
barrier. At 7680×2160 that is about 265 MB per frame, two extra `vkQueueSubmit2` calls and two drains,
in menus too. Estimated 0.3–0.6 ms at the user's resolution.

**Direction:** record the GUI into the present's command buffer. At Native with no letterbox, draw the
picture and interface directly into the swapchain image (`COLOR_ATTACHMENT`), and skip `shown` and the
blit. At minimum, merge the GUI and blit submits.

### FRAME-7: The bloom's Karis weight reads the previous frame's exposure, though nothing prevents recording the exposure first
bug · L · conf H — `display/displaychain.cpp:117-147`, `shaders/display/bloomdown.comp:25-31`

`mBloom.record(..., mExposure.getExposure(), ...)` runs before the exposure is recorded. Both read only
`shown`. Under `FixedExposure`, frame 0 of a reference run weighs the pyramid with what the buffer held
before, so frames 0 and 1 differ on the same input.

**Direction:** record the exposure (fixed or measured) before the pyramid, and delete the lag comment.
See the interaction note above.

### XCUT-3: The exposure histogram reads the whole shown frame again, directly after the bloom's first halving read every pixel of it
perf · L · conf M — `display/displaychain.cpp:118-141`, `shaders/display/bloomdown.comp:42-50`, `shaders/display/histogram.comp:38-54`

An 8×8 level-0 bloom group covers exactly the 16×16 pixels of one histogram group. That is one extra
read of the frame (66 MB at 3840×2160), one dispatch and one barrier.

**Direction:** let the `KARIS` halving also bin its 2×2 into a shared histogram. Keep `histogram.comp`
for frames too small to have a pyramid. See the interaction note above.

### FRAME-14: The exposure histogram bins every output pixel
perf · L · conf M — `display/exposurepass.cpp:67-117`, `shaders/display/histogram.comp:39-55`

16.6 M loads and up to 256 global atomics per group at 7680×2160. For a trimmed log-luminance mean, a
regular 2×2 subsample is an unbiased estimate (a box-filtered level is not).

**Direction:** bin one pixel per 2×2, with a fixed offset or an offset that turns with the frame. This
is the alternative to XCUT-3.

### FRAME-12: Nothing paces the CPU to the display, so input-to-photon latency under FIFO is about 3 vblanks plus the ring
perf · L · conf M — `present/swapchain.cpp:133-135`, `present/presenter.cpp:150-262`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:838-864`

`minImageCount + 1` images, and the only pacing is the ring's wait for frame N−2 and the blocking
acquire. `present_wait` and the present fence (already owned) are not used to hold the next frame's
input sampling.

**Direction:** before the next frame's update, wait for the present of frame N−1 (present-wait or the
present fence), or keep at most one present queued.

### FRAME-5: A full-size screenshot goes through the area-resampling path and allocates about 400 MB of doubles at 7680×2160
perf · M · conf H — `rtx/renderer/frameimage.cpp:55-102`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:548`

The fast path needs `Channels::Rgba`, and `saveScreenshot` asks for `Rgb` at the frame's own size. It
takes the general path: `std::vector<double>(width × height × 3)` = 398 MB, and about 50 M lambda calls
whose weights are all 1.

**Direction:** extend the fast path to any channel set at the identity size (copy rows, drop alpha).

---

## Batch 9 — Water: ripples and waves

### MEDIA-6: Ripple impulses press a fixed 20% once per frame while the field steps by real time, so the wake's strength depends on the frame rate
bug · L · conf M — `apps/openmw/mwrender/rtx/rippleemitters.cpp:59-71`, `trace/ripplepass.cpp:122-124,152-185`, `shaders/trace/ripplestep.comp:255-264,294-298`

Propagation is time-correct (`mCarry`, `mScale` per sixtieth), but the press is per frame:
`kept = 0.2·|away−1| + 0.8` whatever the step is. At 144 fps a wake presses about 41% per sixtieth
against 20% at 60 fps, and half that at 30 fps. The harness's fixed step hides it.

**Direction:** weight each press by the time it covers (`1 − kept^s`, s = sixtieths since the last
press), or accumulate presses on the water's clock at 60 Hz on the host.

### MEDIA-11: The ripple field is stepped, composed and mip-chained every frame, even when nothing disturbed it for seconds
perf · L · conf H — `trace/ripplepass.cpp:130-212`

1–4 steps over 1024² RG32F, a compose into two RGBA16F tiles and an 11-level blit chain, on every
frame with a sea. A press decays below 1e-6 within about 6 s.

**Direction:** track the time since the last impulse on the host. After the decay bound, clear the
tiles once and skip the pass until the next impulse.

### MEDIA-13: Pending ripple impulses are capped at 128 silently, and the newest are dropped
robustness · L · conf H — `trace/ripplepass.cpp:119-124`

While the water's clock is held, footfalls accumulate and everything past 128 is dropped with no count.

**Direction:** merge an actor's repeated impulses before the cap, or count the drops into the report.

### MEDIA-9: The sea's FFT takes its twiddles and time phase from `sin`/`cos` of float angles, and the phase falls outside the range that Vulkan bounds
robustness · L · conf M — `rtx/shaders/wave.h:80-86`, `shaders/lib/wavelines.glsl:271-279`, `shaders/trace/waverows.comp:82-84`

The angle is in [0, 2π). Vulkan bounds `sin`/`cos` to 2⁻¹¹ absolute only inside [−π, π]. This is the
one part of the pinned arithmetic whose precision belongs to the driver, and the error compounds over
nine stages into the caustic's curvature. RDNA under Mesa is the exposure.

**Direction:** pass a host-computed (double) twiddle table of `WAVE_GRID/2` entries. Reduce the phase
to [−½, ½) turns before multiplying by TAU.

### MEDIA-10: The 128-point cascade runs on 256-lane workgroups sized for the 512 grid
perf · L · conf H — `shaders/shared/wavetransform.h:122-127`, `rtx/environment/wavecascade.hpp:40-43`

Three quarters of the lanes are idle in every butterfly stage of the second cascade, but they still
take every barrier.

**Direction:** make the workgroup size a specialization constant per cascade, or run two rows per
workgroup on the narrow grid.

(Checked and sound: the FFT's normalisation, centre shift, conjugate pairing and Nyquist exclusion.
Rows and columns cannot fuse, because a 512² grid does not fit in shared memory.)

---

## Batch 10 — Scene record layout for the trace

Each item changes what the hottest loops load. The gain is unmeasured, so A/B each item on an
interior and a foliage exterior, and keep only what measures.

### SCENE-6: The candidate loop's rows are read with a 4-byte alignment claim, and the hot material fields are spread across 108 bytes
perf · L · conf M — `shaders/shared/tables.h:14-26`, `shaders/lib/bindings.glsl:296-309`, `rtx/shaders/scene.h:648-675,772-776,1142-1227`, `shaders/lib/traversal.glsl:424-470`

`InstanceTable` and `LightCellTable` claim `TABLE_ALIGN_ROWS` (4), but their rows are 64 and 16 bytes
on a 16-aligned base. `GpuLightCell`'s doc ("both runs in one fetch") does not hold under a 4-byte
claim. `candidateStops` reads `GpuMaterial` fields at offsets 0, 4, 8, 60–76 and 104, which is up to
four sectors per candidate on a cutout.

**Direction:** claim 16 for both tables, and correct the `tables.h` comment. Move the candidate's
fields to the front of `GpuMaterial`, pad it to 112, and claim 16.

### SCENE-7: Every attribute fetch goes through a block-table load that the mesh row could carry already resolved
perf · L · conf L — `shaders/lib/bindings.glsl:236-283`, `shaders/lib/geometry.glsl:33-40`, `rtx/shaders/scene.h:587-620`, `scene/scenebuffers.cpp:216-233`

instance → mesh → `BlockTable` → indices → `BlockTable` → uvs → `TexelTable` → sample. A mesh's runs
never cross a block (`checkFits`), so each run's address is a per-mesh constant. `mMeshTable` is
already one copy per slot, so the posed streams fit as well.

**Direction:** prototype `GpuMesh` with run addresses (about +40 B per row). That removes
`POSED_FIRST_BLOCK`, `mNormalShift` and the `JoinedBlocks` joining.

### SCENE-12: The vertex streams are wider than the data needs
perf · L · conf L — `rtx/scene/meshtable.hpp:116-122`, `shaders/lib/bindings.glsl:207-222`, `shaders/lib/geometry.glsl:133-200`

Normal and colour are each a 12-byte vec3 (the colour is white when the mesh has none). The tangent is
already 4-byte octahedral. A committed hit reads 72 of its approximately 108 bytes from these two streams.

**Direction:** octahedral normals (reuse `octahedral.h`), and a 16-bit colour or a `NO_RUN` colour
stream like the second uv set. The picture moves slightly, so keep it only on a measured gain.

### SCENE-10 / XCUT-9: Skinning and morphing record one dispatch and one push per deformed mesh
perf · L · conf L — `scene/skinpass.cpp:49-147`

A street of 30 NPCs is 100–200 small dispatches per frame, with tails smaller than a wave and a host push each.

**Direction:** write per-mesh constants into a per-slot table with a vertex-count prefix, and run one
dispatch per kind (a binary search over the prefix, or indirect). Measure the `skin` zone first.

---

## Batch 11 — Layered ground in the trace

### TRACE-3: `LAYERED` does not remove the layer-stack loop from the surface and water stages
simplify · L · conf H — `shaders/trace/visibilityhit.rchit:15-18`, `shaders/lib/traversal.glsl:1055-1059,1409-1412`

The rchit header says that `LAYERED` folds the loop out of the two stages that no terrain reaches. But
every inline trace in those stages (bounce, shoreline bed, both water legs) goes through `resolve`,
which hard-codes `layered = true`. Only the primary hit's detailed copy is removed.

**Direction:** correct both comments. Measure the stage with `LAYERED` forced true. If nothing
changes, remove `SPEC_LAYERED` and read `mGround` from the material row.

### TRACE-4: A diffuse bounce's far hit on near ground sums the whole layer stack
perf · L · conf L — `shaders/lib/traversal.glsl:1060-1065,1216-1271`, `shaders/lib/ground.glsl:134-158`

A non-detailed hit on a chunk that kept its stack loops over every layer: a 64-byte row, four mask
loads, and a diffuse plus delight fetch per showing layer. The `resolveFor` doc already concludes that
detail at a bounce's far hit is invisible. Composites exist only for chunks past `wantsFlattening`.

**Direction:** bake a composite for near chunks too, at load, through `groundcomposite.comp`. Resolve
non-detailed hits from it. Keep the live stack for the eye, reflections and the bed.

---

## Batch 12 — Housekeeping

### FRAME-11: The pipeline cache is written only by a clean destructor
robustness · L · conf H — `device/pipelinecache.cpp:253-262`, `trace/visibilitypass.hpp:159`

Every pipeline is made at start, but a crash or a kill later in the session discards the whole compile.

**Direction:** write the cache when `awaitKernels` reports done (the write is already crash-safe).
Write it again at exit only if the blob changed.

### FRAME-16: Every pipeline in every build is created with `CAPTURE_STATISTICS`, and its statistics are queried and formatted at creation
simplify · L · conf L — `pipeline/pipeline.hpp:80-83`, `pipeline/pipeline.cpp:61-71`, `device/device.cpp:291-372`

`VK_KHR_pipeline_executable_properties` is a required extension for this alone. A `std::string` is
built per executable even with Verbose off, and capture flags can change driver cache keys.

**Direction:** capture only when a harness verb asks, and make the extension optional. Time a cold and
a warm start with and without the flag first.

### XCUT-10: The four probe kernels are two kernels written twice
simplify · L · conf H — `shaders/probes/{halfmean,unormmean,halfstore,unormround}.comp`, `shaders/shared/{halfmean,unormmean}.h`

`HalfMeanConstants` and `UnormMeanConstants` are the same five fields. The `main`s differ only in the
store. `halfstore` and `unormround` have the same shape. (These are device-behaviour tests, compiled
only under `BUILD_COMPONENTS_TESTS`. The renderer has no light probes.)

**Direction:** one running-mean probe and one rounding probe, each with a specialization for the store
kind and one constants header.

### MEDIA-12: Two comments describe code that changed
simplify · L · conf H — `shaders/lib/sprites.glsl:825-837` (`mergedPuffs`), `device/memory/image.cpp:396-397` (`buildMips`)

`mergedPuffs` says that each walk reports an alpha-weighted mean colour, but `spritesAlong` now
composites its nearest four in depth order. `buildMips` says that the fog samples the wave tiles "as a
dispatch", but the fog samples them in ray-tracing launches.

**Direction:** restate both comments.

Comment corrections that belong to a batch above stay with their batch: DENOISE-5, DENOISE-9 (batch 5),
TRACE-1 and TRACE-9 (batch 7), TRACE-3 (batch 11).

---

## Checked and sound

The reviewers checked these items and found no fault:

- **History validity.** The first frame, a resize, a cut, an unfiltered frame and a mode change are
  handled through one table (`TemporalTurns`, `DenoiseHistory::discard`, `FramePast::mPastLost`,
  `Upscaler::reset`). Every barrier between accumulator, clamp, shadow, glossy, pane, cascade,
  composite and FSR was traced, including the ping-pong write-after-read cases. The only hazard found
  is DENOISE-4.
- **FSR port.** The host constants, formats, dispatch sizes, clears, parity and SPD setup match the
  3.1.4 SDK as far as the vendored headers allow a check.
- **GLSL/C++ interface.** All shared structs are scalar layout, and no struct has the one shape where
  scalar GLSL and C++ differ. Push ranges, bindings, descriptor types and specialization IDs are
  checked against the module. No helper is written twice under two names.
- **Frame-slot lifetime and present sync.** Per-slot host writes assert idleness. Graveyard stamps are
  monotonic. The acquire and render semaphores are guarded correctly. Swapchain recreation waits idle.
  sRGB is encoded once.
- **Tone and exposure.** Khronos Neutral with the ramped offset is continuous at the knee. The TPDF
  dither is placed correctly. The exposure bin centres and the trimmed mean are right, and the float
  sums are exact below 2²⁴ pixels.
- **SPIR-V pinning.** Operand classification, single-use fusion, `precise` propagation and float
  widths hold within the stated contract (apart from FRAME-13).
- **Pipelines.** All are compiled at start, with no compile during play. `SET_PASS` is pushed per
  dispatch, with no descriptor pool on the frame path.
- **Acceleration structures.** A full TLAS rebuild with `PREFER_FAST_TRACE` when something moves, a
  deforming-BLAS refit with a rebuild on a rota (the flags match), compaction without waits, and
  `NO_DUPLICATE_ANY_HIT` on every BLAS.
- **Texture kernels.** The BC7 mode-6 layout, the anchor swap and the endpoint quantisation are
  correct. The mip chain is alpha-weighted and sRGB-correct, with correct odd-extent taps. Ground
  composites are capped at 2 per frame.
- **Trace.** The payload pack/unpack (30 words), the rgb9e5 range, the `HitRecord`/SBT order against
  `MaterialKind`, push-descriptor persistence across binds, `tmin ≤ tmax` at the clip, and RIS and
  sky-pick unbiasedness.
- **Fog and waves.** Fog reprojection and history rejection are correct (apart from MEDIA-1's format).
  The FFT is correct.

- **TRACE-6, withdrawn.** It said that an opaque texel of a see-through caster never stops a
  shadow ray. A see-through surface has no opaque texel: `Material::isTranslucent` needs an opacity
  under 1 or a texture whose alpha never reaches 255 (`reachesSolid`), and a fade under 1 keeps
  `sampledOpacity` under 1 as well. Filtering cannot go above the largest texel.

- **FRAME-1, withdrawn by measurement.** It said that the harness's counting kernel, one atomic per
  missed primary ray into host memory, makes `bench` slower than the game. Three release legs back to
  back, counting on, off and on again, at seyda-neen-ship, seyda-neen-ship-dawn and balmora (84–88%
  of the primary rays hit): the trace median with counting off fell between the two legs with it,
  2.638 / 2.682 / 2.698, 3.183 / 3.225 / 3.230 and 1.590 / 1.610 / 1.609 ms. The atomics go to one
  address, which the compiler joins per subgroup; the digest's slow atomics went to many.

- **FRAME-4, not reproduced.** It said that a timeout from acquire or the present fence, treated as
  device loss, ends a game whose window the compositor stopped showing. Minimized through KWin
  (Plasma 6, Wayland, NVIDIA) for about 45 s with a FIFO swapchain, the game kept presenting at
  about 31 frames a second and nothing waited near its 10 s patience. A window on another virtual
  desktop, and other compositors, were not tried.

## Not reached

- `rtx/preprocess/shape/*`, `rtx/image/{alphaimage,colour,colourblock,texels,formatcensus}` in depth,
  `rtx/scene/{materialtable,placementtable,scenedesc}` in depth, `mirror/*` beyond the pose path.
- `environment/{atmospheremesh,cloudmesh,moonfaces,skymesh,skysheet,wavespectrum}.cpp`.
- The bodies of `spirvpin.cpp` (beyond its classification), `spirvinterface.cpp`, `spirvdigest.cpp`.
- `lib/water.glsl` and `lib/medium.glsl` were skimmed. The fixed-point share accumulation in
  `medium.glsl` is not proved free of overflow for unlit sheets.
- The vendored FidelityFX host source (`ffx_fsr3upscaler.cpp`) is not in the tree, so the reset clears
  were checked against the port's comments only.

## Frame pass table

World frame, denoised and upscaled, in record order. **T** = traced extent, **O** = output extent,
**C** = fog columns (T/12 per axis), **G** = 8×8 groups. `│` = a compute→compute drain after the pass.

| # | Pass (module) | Dispatch shape | Reads | Writes | Barrier |
|---|---|---|---|---|---|
| — | skin / morph (`skin.comp`, `morph.comp`), at placement | one dispatch per deformed mesh, 64-lane groups | bind streams, influences, bones/weights | pose blocks (slot) | │ |
| — | TLAS pack (`toplevelpack.comp`) + BLAS refit / TLAS build, at placement | rows/256 | instance rows, starts | packed instances, AS | │ |
| 1 | glare clear | fill | — | sun-glare counts | — |
| 2 | ripple step ×1–4 (`ripplestep.comp`) | 1024²/16² | field[before], impulses | field[after] | │ each |
| 3 | ripple compose (`ripplecompose.comp`) + blit mips | 1024²/16² | field | ripple surface, curvature (+mips) | │ |
| 4 | waves: `waverows.comp` ×2 │ `wavecolumns.comp` ×2 + blit mips (sea, clock moved) | one group per row/column | amplitudes, turn rates | wave surface, curvature (+mips) | │ │ |
| 5 | frame block update (`vkCmdUpdateBuffer`, 1624 B) | — | — | `VisibilityConstants` | │ |
| 6 | sprite shelter (`spriteshelter.rgen`), when shelter > 0 | sprites × 1 | TLAS, frame, sprites | sprite copy | │ |
| 7 | sprite emitters (`spriteemitters.rgen`) | emitters × 1 | frame, emitters, fog | emitter frames | │ |
| 8 | sprite shade (`spriteshade.comp`) | one group per emitter per light | sprites, emitters | sprite light/order | │ |
| 9 | sprite bin: clear │ `spriterects` │ `spritestarts` │ `spriteruns` | various | sprites, emitters, presences, camera | rects, presence words, starts, runs, report | │ │ │ │ |
| 10 | channels begin (UNDEFINED → write, ×19) | — | — | — | │ |
| 11 | fog depth (`fogdepth.rgen`) | C.x × C.y | TLAS, frame | column depth, column moons | │ |
| 12 | fog scatter (`fogscatter.rgen`) | C.x × C.y × 64 | column depth/moons, fog history, field, lights | scatter, sunward, lamps (3D) | │ |
| 13 | fog integrate (`fogintegrate.comp`) | C/8² | scatter, sunward, lamps | air, slices, seeing (3D) | │ |
| 14 | trace (`visibility.rgen` + rchit×3, rahit, rmiss×2) | T.x × T.y | TLAS, frame, tables, textures, waves, ripples, fog, sprite list | 19 channels, counts, glare | │ |
| 15 | digest (`digest.comp`), harness read-back only | T/16² | all 19 channels | digest lanes | │ |
| 16 | accumulate (`accumulate.comp`) | T/G | indirect, fill, motion, surface, histories | surface out, moments, blended, fill blended, fast blended | │ |
| 17 | accumulate clamp (`accumulateclamp.comp`) | T/G | surface, fast blended, indirect, fill, moments | blended, fill blended (in place), fast | — |
| 18 | sky shadow: mask │ tiles │ filter L0 │ L1 │ L2 | T/(8×8), T/G | shadowed, surface, motion, held surface, history, moments | mask, tiles, moments, scratch, visibility | │ ×5 |
| 19 | lamp shadow: the same 5 dispatches | as 18 | lamped, … | lamp images | │ ×5 |
| 20 | glossy (`specular.comp`) │ clamp (`historyclamp.comp`), when mapped | T/G each | specular, surface, motion, held surface, means | mean, fast | │ |
| 21 | pane (`pane.comp`) │ clamp (`historyclamp.comp`) | T/G each | pane channels, held/mean/fast | held, mean, fast | │ (`ready`) |
| 22 | wavelet ×4 (`atrous.comp`, level 0 `ATROUS_WIDE`) | T/G each | blended, fill, surface, moments | ping-pong narrow; level 0 histories | │ each |
| 23 | composite (`composite.comp`) | T/G | direct, filtered indirect/fill, 4 albedos, shadows, specular, pane | direct, sum | │ |
| 24 | FSR: clears │ inputs │ luma pyramid │ change pyramid │ change │ reactivity │ instability │ accumulate | T/8², SPD, (T/2)/8², O/8² | colour, surface, motion, masks, histories | FSR transients, history, output (O) | │ each |
| 25 | puffs composite (`spritecomposite.rgen`) | T.x × T.y | surface, backdrop, puffs, sprite list, fog | shown (in place) | │ |
| 26 | bloom down ×≤6 │ bloom up ×≤5 | (O/2ᵏ)/8² | shown, exposure (last frame) | bloom levels | │ each |
| 27 | histogram │ reduce (`histogram.comp`, `exposure.comp`) | O/16², 1 group | shown | bins → exposure | │ │ |
| 28 | sun glare ease (`sunglare.comp`) | 1 group | glare counts | glare share | │ |
| 29 | tone (`tone.comp`) | O/8² | shown, bloom, exposure, glare, backdrop, surface, lift, stars, blue noise, sprite list | target (rgba8) | │ |
| 30 | debug lines, when debug is on | draw | surface | target | │ |
| 31 | read-back / stress hold, harness only | copy / 1 group | target / counts | host buffer | — |
| 32 | GUI (`gui.vert`/`gui.frag`), its own submit | draws | GUI textures, picture | shown | │ |
| 33 | present: clear when letterboxed, then blit, its own submit | transfer | shown | swapchain image | — |
