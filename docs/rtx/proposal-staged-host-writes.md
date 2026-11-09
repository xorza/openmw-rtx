# Proposal: staged host writes

Temporary. When it lands, each lasting decision goes into `architecture.md` ("Memory"), the
"Refused: a staged path for those rows" clause there is replaced, and this file is deleted.

## The problem

Everything the host rewrites — the instance, top-level and material rows, the lights, sprites,
emitters, presences, poses, texel words, block starts, block address tables, FSR's constants, the
ripple impulses, the GUI and debug vertices, the shader binding tables and two constant floats — is
made as `BufferKind::HostWritten`, video memory the host maps. Without resizable BAR that memory is a
window of about 256 MiB, and under Windows the driver can give it a budget of nought: the game then
ends at start (`research/2026-11-09-host-written-window-on-windows.md`, issue #49). Turing has no
resizable BAR, so every RTX 20 under Windows is at risk, and an RTX 20 under Linux is held to 246 MiB
with the world's tables in 184 MiB of blocks (`research/2026-10-22-host-written-window.md`).

## Established practice

- **VMA, "Advanced data uploading"**: for data the CPU writes often and the GPU reads often, ask for
  host-visible video memory, and where the allocation is not host-visible, keep a staging copy and
  record the transfer. The fall back is the application's to detect and handle.
- **D3D12 GPU upload heaps**: the same split as an API feature — supported only where the whole frame
  buffer is CPU-visible (`GPUUploadHeapSupported`), with the upload heap and a copy otherwise.
- **FFmpeg's Vulkan hwcontext** turns its host transfers off where the largest host-visible
  device-local heap is smaller than the largest device-local heap: the window is not the VRAM.
- **Scattered rows**: one copy region per run is what Vulkan offers directly; engines with very many
  scattered rows upload them packed and scatter them with a compute pass (Unreal's
  `FScatterUploadBuffer`). The fork's rows changed per frame are hundreds, so regions come first.
- **Shader binding tables**: NVIDIA's tutorial maps a buffer it asks to prefer video memory and copies
  the handles in, which VMA places in host memory where no window is left. Nothing in the
  specification asks for mapped memory: a table is read by address, wherever it stands.

## Decisions

- **Two transports, one writing API.** `HostWrites::Mapped` copies into mapped video memory, as now.
  `HostWrites::Staged` places the same buffers in video memory alone and copies into them from the
  pool's staging ring, on the queue, ahead of what reads them. Callers write through one object
  (`Uploads`) and never ask which transport runs. Refused: a staged path for the per-frame tables alone,
  which leaves the shader binding tables and the address tables in the window that has no room.
- **The path is a `constexpr` constant alone**, as the user decided:
  `inline constexpr HostWrites sHostWrites = HostWrites::Staged;`, read with `if constexpr`, one
  transport on every device and no detection. `Staged` ships, as the user decided, because it starts
  on every card; `Mapped` stays for the A/B that decides whether it stays at all. Both transports
  compile in every build; another transport is another build, which is what an A/B and a picture
  comparison take. Refused: the device choosing (`ByDevice`), and a `Device` constructor parameter for
  tests, a second route to the same choice.
- **`Mapped` on a device whose window is not the video heap is refused at selection**, as the user
  decided, with the
  obstacle "host-written memory is a window, not the video memory: turn on resizable BAR". The window
  is the video heap where a host-written type stands on the heap content stands on
  (`ContentTypes::mHeap`): resizable BAR on NVIDIA and AMD, and every integrated GPU. Exact, where
  FFmpeg compares sizes with a tolerance. Refused: a budget threshold, since the window's budget moves
  with what other processes hold. Refused: VMA's per-buffer fall back
  (`HOST_ACCESS_ALLOW_TRANSFER_INSTEAD`), which is detection by another name and runs both transports
  at once. Under `Staged` the obstacle "no memory type the host writes into and the device reads"
  goes: the transport needs no such type.
- **One device copy a frame slot in both transports**, as the user decided: the per-frame-in-flight
  resources of the Khronos tutorial and Arm's best practices. The staged copies keep `SlotTable`'s
  and `PerSlot`'s accounting as it is, a frame still reads the previous pose copy for motion, and the
  head barrier every command buffer opens with (`CommandPool::open`) already orders a copy after the
  last read of its destination. The cost is the tables' 123 MiB in video memory, where one copy would
  be half. Refused: one shared copy updated by a copy and a barrier each frame (Unreal's GPUScene),
  which changes every debt and makes each copy wait out the frame before it on the queue.
- **The copies ride a deferred batch on the one queue**, as the user decided. Each writer of host
  tables holds a `Batch`; staged writes are recorded into it, it ends in `orderStagedWrites`, and it is
  deferred ahead of the submit that reads: the placement's, the trace's, the present's. The pool
  already submits deferred batches first in the same submit, and its ring already retires staging under
  that submit: no new staging machinery, and one barrier a batch. The Khronos tutorial names the one
  queue the simple choice for small per-frame updates. Refused: a copy inline before each reader, a
  barrier between every copy and its reader inside the frame; and a transfer queue, which the
  tutorial keeps for large streaming and which costs ownership transfers and a semaphore, where a
  second queue already bought 0.1–0.2 ms.
- **Runs coalesced into one copy command a buffer**, as the user decided. A `SlotTable` sync stages
  its owed rows packed and records one `vkCmdCopyBuffer` with a region per run of adjacent rows, from a
  region scratch the table keeps: many tiny regions slowed one engine's uploads (lava #22). Refused
  for now: a compute scatter (Unreal's `FScatterUploadBuffer`), a shader, a pass and a dispatch a
  frame, which pays past thousands of scattered rows; and the whole table a frame, tens of MiB on a
  large world, which the debts exist to avoid.
- **What is written once leaves the window in both transports**, as the user decided: data written
  rarely and read often belongs in device-local memory behind a staging copy. The two constant
  floats and the block address tables (`BlockedBuffer`, `JoinedBlocks`) become device-local buffers
  uploaded through a batch; the address tables are written inside one already. Refused: following
  the constant, two routes for data that never earned a mapped window.
- **A shader binding table is uploaded by its first launch**, as the user decided. Its compile thread
  writes the handle into the table object, since that thread records nothing; the first `traceRays`
  records `vkCmdUpdateBuffer`, which is made for small one-off writes, and a barrier to
  `SHADER_BINDING_TABLE_READ`, once a table. `Buffer::unnamed` then has no caller and goes. Refused:
  an upload at hand-over, a step that must know every pipeline of a set, and one buffer for every
  table, sized before the variants exist or moved by its growth.

## Implementation plan

Each step builds and its tests pass before the next.

1. **The constant and its obstacle.** `HostWrites` and `sHostWrites` in
   `device/memory/hostwrites.hpp`; `profileOf` refuses a window that is not the video heap under
   `Mapped`, and drops the host-written-type obstacle under `Staged`; `describe` prints the transport,
   and the memory report and the bench record carry it. Tests: the window rule on issue #49's heaps
   (not the video heap), on a resizable-BAR NVIDIA's and on an integrated GPU's one heap (both the video
   heap), exactly; and the obstacle the build's transport gives on #49's heaps.
2. **The memory.** `propertiesOf(BufferKind::HostWritten)` reads the constant: `sHostWritten` under
   `Mapped`, `DEVICE_LOCAL` under `Staged`, with `TRANSFER_DST` added to a host-written buffer's usage
   there.
3. **`Uploads`.** Over a `Batch&` and the device's transport: `write(buffer, offset, bytes)`,
   `lend(buffer, offset, size)` for a caller that produces its bytes in place (the debug vertices),
   `writeRuns(buffer, stride, rows, source)` for the coalesced rows, and `finish()`, which records
   `orderStagedWrites` where anything was staged. `Buffer::write`, `writeAt` and `writable` stop
   accepting a host-written buffer: one way to write one. Tests: the regions `writeRuns` records for
   given rows, exactly, and that a mapped write records none.
4. **The scene's tables.** `SlotTable::sync`, `SceneBuffers::place`, `SceneAcceleration::prepareTopLevel`,
   `SkinTables`' pose writes and `TextureArray::sync` write through `Uploads`. `Placing` carries it.
   `VulkanRenderer::placeScene` makes the batch, finishes it and defers it ahead of the placement's
   submit, or ahead of the picture placement's own batch; `DeviceScene`'s build and extension write
   through their own batch, ordered before `bakeGround` reads.
5. **The frame's writes.** FSR's constants, the ripple impulses and the debug vertices through a batch
   the trace's recording holds, deferred ahead of its submit; the GUI's vertices through one deferred
   ahead of the present's or the GUI's own submit.
6. **Written once.** The constant floats, the address tables and the binding tables, as decided above.
7. **The record.** `architecture.md` "Memory" takes the decisions; `sceneroom.hpp`'s "held to the
   host-written window" says what holds the room now; the 2026-11-09 research note links here.

## Verification

- `./omw test` in a build of each transport.
- `./omw shot --views=all --map --upscale=off` under `Mapped`, then `--against` under a build with
  `sHostWrites = Staged`: no picture may move, since a copy is exact.
- `./omw repeat --pairs=10` under `Staged`.
- `./omw release bench`, `Mapped` against `Staged` on a resizable-BAR card, medians, p99 and the worst
  frame, the `streaming` suite included for a crossing's whole-table restage. **If `Staged` costs
  nothing measurable, `Mapped` goes and the constant with it**: one transport on every device.
- The reporter of #49 runs the release with resizable BAR off again.

## Sources

- [VMA: recommended usage patterns, "Advanced data uploading"](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html)
- [D3D12 GPU upload heaps](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html)
- [FFmpeg: disable host transfers if ReBAR is disabled](https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-June/149030.html)
- [Khronos Vulkan tutorial: frames in flight](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/03_Frames_in_flight.html)
- [Arm: Vulkan best practice FAQ, part 2](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/vulkan-best-practice-frequently-asked-questions-part-2)
- [Khronos Vulkan tutorial: non-blocking uploads on a transfer queue](https://docs.vulkan.org/tutorial/latest/Synchronization/Transfer_Queues_Streaming/02_non_blocking_uploads.html)
- [lava #22: many small copy regions slowed uploads](https://github.com/osanj/lava/issues/22)
- [Unreal: FScatterUploadBuffer](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/RenderCore/FScatterUploadBuffer)
- [vkCmdUpdateBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdUpdateBuffer.html)
- [nvpro-samples ray tracing tutorial: shader binding table](https://nvpro-samples.github.io/vk_raytracing_tutorial_KHR/shader_binding_table.html)
- [GPUOpen: Using Vulkan device memory](https://gpuopen.com/learn/vulkan-device-memory/)
- [vkCmdCopyBuffer](https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdCopyBuffer.html)
