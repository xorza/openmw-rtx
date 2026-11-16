# Proposal: the content's memory — one read a texture, one copy of the geometry, a device that uses its budget

How content gets from its files to the device, audited at `4b608db89f` by reading: the game's loaders
(`components/resource`), the mirror and the cell ring, the scene's tables and the uploader, the content store and the
texel path, and the device's memory. Nothing was built or run, so a figure below is either one a dated note measured or
is marked *inferred*; the lines the plan leans on were read twice. It takes in the two open issues at the head of
`.notes/ISSUES.md` and the ones the audit added under them, and replaces `proposal-host-geometry.md` and
`proposal-handheld-memory.md`. When it lands, its decisions fold into `docs/architecture.md` ("Loading", "The content
store", "Device and memory") and this file is deleted. Written for `/execute-plan` once the entries under **Yours to
say** have their answers: an item that names one is skipped while it has none.

## What became of the two proposals

| Was | Now |
|---|---|
| host geometry: arrays held only until the hand-over stages them | kept, and widened to the reader's copy and the ground's masks (item 7) |
| host geometry: one flat arrivals store, kept to a bound | kept, with a start for each arrived slot and a bound a scene (item 7) |
| host geometry: a change of mode stands the textures again | replaced: a texture stands at a level one planner moves, so no world is rebuilt and no fourth way to hand over (item 6) |
| host geometry: each mesh's shape digest made in `MeshTable::add` | changed: made only where the host asks for digests, since only the harness reads it (item 7) |
| handheld memory: measured before changed; a refusal says how the heap stood | landed; the meters still missing are item 1 |
| handheld memory: an arrival frees what it needs; a short texture is handed over again | replaced by the planner and by room counted where it can be taken (items 3 and 6) |
| handheld memory: a short texture lets go of its texels and is read again | replaced: a move up reads the levels it adds, once, when the plan grants them (item 6) |
| handheld memory: steps 1–3 (tours, the host's mitigation, the issue log rewritten) | items 1, 5 and 12 |

## Findings

Where the memory stood when measured:

| Run | Figure | Held by |
|---|---|---|
| handheld, PBR tour (`ee7852dc50`) | 5,581 MiB anonymous, 1,376 MiB of it swapped; the heap 2,008 MiB free of 5,690 MiB taken; 46,822 image loads on 9,552 files; 994.5 s against the desk's 89.6 | the texel path and the heap, below |
| handheld, Repack tour (ahead of `3ab057cd39`) | 859 content takes refused with usage 0.9–1.7 GiB under a 6,144 MiB budget, 78–92 MiB free in pieces | the ceiling, then fragmentation, below |
| desk, Repack tour, heap peak 6.17 GiB (`2026-11-16-host-memory-holders-repack.md`) | 1,141 MiB cell stores, 943 MiB `MeshTable`, 443 MiB the mirror, 345 MiB Bullet shapes, 275 MiB the store's resident entries, 254 MiB navigation meshes, 171 MiB keyframes, 157 MiB `NifOsg::Loader` | the geometry and the game's own, below |

**The texel path reads a texture's source file to find a chain the store already holds.**

- **The store's key is a digest of the source's texels, and the host no longer keeps them.** The worker fetches them
  before any lookup (`preparedimages.cpp:246`, `chainpass.cpp:40-60`) and the uploader drops them once the device has
  its copy (`sceneuploader.cpp:375-380`), so every arrival and re-arrival decodes the source through OpenSceneGraph and
  only then reads the store. "The store is the game's input" does not hold on a hit, and `ContentStats::mKeyMs` leaves
  the read out.
- **A file is decoded whole at four separate moments**: to make its header (`imagemanager.cpp:211-232`), once for each
  fact asked (`threadcontent.hpp:106-123`), to key its chain, and at every later hand-over. On the desk the first three
  share one cached decode; on the handheld the budget stands below what is held, an unheld entry lives about a second,
  and the count is 4.9 loads a file. *Inferred*: some 200 GiB read and copied over that tour, the leading candidate for
  its 994 s.
- **A fact's first ask reads the file on the asking thread, the frame's included** (`materialresolver.cpp:542-547`),
  against "Refused: a file read on the frame's thread".
- **A chain read for a slot that left is held for the session**: `letGo` keeps an entry not yet handed out
  (`preparedimages.cpp:155-169`), its job still runs, and the sweep waits for a file object that never dies, since
  `ImageManager::mHeaders` keeps every header (`imagemanager.hpp:71`). No bound in bytes; how often is not measured.
- **Whether a stored chain is looked up depends on what making it would cost** (`preparedimages.cpp:263-270`): a 4096
  normal map needs some 150 MiB available to be allowed a 22 MiB read.
- **A "coarser" chain is two files for ever**: the store keeps levels 1 down and level 0 comes from the source
  (`chainpass.cpp:135-138`), whose own lower levels are read and unused. The replacers' `_spec` maps are of this form.
- **Every arrival's buffers are allocated on a worker and freed on the frame's thread**, and each worker frees its
  scratch whenever the queue drains (`preparedimages.cpp:279-283`).
- **A source is read 8 KiB a call and copied twice** (`constrainedfilestreambuf.cpp:26-41`, no `xsgetn`): 2,731 reads
  for a 22 MiB image.

**The geometry stands three times on the host, and the ring loads what cannot stand.**

- **Three copies**: the template's arrays, which the reader pins; the reader's unpacked copy at 32–60 B a vertex
  (`prepared.hpp:235-241`), read once at adoption (`held.cpp:54`) and kept for the model's whole lend; and the mesh
  table's at 36 B (943 MiB). *Inferred*: the reader's is 250–350 MiB of the mirror's 443 MiB. The host-geometry
  proposal removed the third and left the second.
- **Every model of every cell in the disc is read, adopted, uploaded and built**, about 450 cells, not 16
  (`cellgrid.hpp:20`, `cellring.cpp:532`); the size rule is applied only at placing (`cellplacer.cpp:740-757`). At the
  default a model under 82 units stands only within a cell of its own. How much of the ring never stands needs a
  count; it bears on the mesh table, the mirror, the pinned templates and the device's structures at once.
- **The mesh key, the packing and the bounds are made on the main thread at adoption** from arrays the reader walked
  (`mesh.cpp:7-26`, `meshtable.cpp:132,182-203`); a split normal is packed, stored, unpacked and packed again.
- **The ring's pools keep the largest tenant each object ever held** (`held.hpp:187-197`, `prepared.hpp:145,407`), and
  a `Placement` is 128 B of which 4 are its own (`held.hpp:80-103`).
- **The ground's indices and coordinates, one list for every cell, are copied and digested a cell**
  (`cellplacer.cpp:382-387`): 129 KiB a land cell on the host and again on the device.

**The device leaves its budget unused, and counts room where it cannot be taken.**

- **Content's ceiling keeps free a second copy of everything essential, and of every structure as well for textures**
  (`memory.cpp:455-469`). The stated reason, a table that grows by making itself again, holds for megabytes of tables;
  the geometry and the structures' blocks are appended, and a rebuild drains the old world first. Resting meshes'
  structures lower the ceiling too. This is the 0.9–1.7 GiB the handheld stood under its budget; the split of that
  gap by use is *inferred*, since a refusal does not log it.
- **Since `84a60139e8` room is counted across two storages**: a structure block is 64 MiB of its own, outside
  `getContentFree` (`memory.cpp:471-486`), so an evicted mesh reads as use and lowers the rest limit by nine tenths of
  what it freed (`sceneuploader.hpp:110-115`), and an arrival's structures are sized against the texture pools' room
  (`sceneuploader.cpp:93-97`).
- **A texture want never matches a resting texture of its own size**: the hole carries alignment margins the fit does
  not (`memory.hpp:118-121`, `texture.cpp:696`, `restqueue.hpp:204`). **A static mesh refused in the loose storage
  files no want** (`bottomlevelstore.cpp:359`).
- **Geometry is essential, appended and never given back, and a resting mesh pins its runs** (`blockedbuffer.cpp:37-75`,
  `meshtable.cpp:283-295`); the rest counts a mesh's structure alone. *Inferred*: 0.5–0.9 GiB under the handheld's
  7,000 resting meshes.
- **A texture's residency is its whole image, sized by the room and never by need** (`texture.cpp:358-493`). A move up
  is a new image and the whole chain read again, up to six tries (`sceneuploader.cpp:120-160`). The shaders already
  compute the level each sample reads (`texturing.glsl:275-299`); nothing records it.
- **Fixed rooms and floors**: tables for 2^18 placements against 64–80 thousand in use, ground masks as floats in
  32 MiB, 128 MiB of structure blocks and 23 MiB of stream blocks for every picture's scene, 85 MiB of spread means
  kept after one PBR load, staging unbounded within a batch (1,435 MiB at a PBR world's build).
- **A world is rebuilt for more than a change of mode**: by raw usage against the ceiling above, by a refusal flag
  never cleared (`texture.cpp:663`), and by the first large picture (`vulkanrenderer.cpp:990-1001`).
- **Every refused take walks every allocation of the content pools** (`memory.cpp:522-532`), the walk "room counted by
  the pools' ranges" was refused for.

**The heap keeps what is freed, and the budgets do not see each other.**

- **Nothing tunes the allocator in play.** Image buffers of 5–22 MiB are allocated on 25 or more threads and freed on
  another. glibc raises its mapping threshold to the first such block freed, up to 32 MiB
  ([`mallopt(3)`](https://manpages.debian.org/unstable/manpages-dev/mallopt.3.en.html)), after which they come from
  arenas that give back only their tops. *Inferred* as the cause of the 2 GiB free; the arenas' figures the tour now
  logs will say.
- **The caches' budget counts as taken what the heap holds free** (`cachebudget.cpp:9-15`), and an insertion evicts on
  its own thread, the frame's included, destroying the value inside the budget's lock (`cachebudget.cpp:75`).
- **On unified memory nothing bounds the device by the system**: RADV's budget is its heap less the GPU's use
  ([`radv_physical_device.c`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/vulkan/radv_physical_device.c))
  and does not see the process's heap.

**The game's own holders, upstream's code.**

- **Cell stores**: the tour scales `preload distance` to 4.3 cells (`framed.cpp:111-125`), so every door of every
  active cell is followed; `Scene` loads the destination's references before the preloader's budget is asked
  (`scene.cpp:1231`, `cellpreloader.cpp:265`); and a store once loaded is never let go (`worldmodel.cpp:163-168`).
- **A NIF is hashed whole before it is parsed** (`niffile.cpp:557`) for a value two tools print and the game never
  reads; Bullet's shapes hold some 66 B a triangle; the navigator's tile cache is a fixed 256 MiB and stands at it.

**A tour is not a played game**: its door reach covers about 190 times a player's area, its timer caches never expire
(`rtxrenderer.cpp:342-346`), and it runs on huge pages the game does not ask for (`rtxtool/main.cpp:638`).

**Checked and left as recorded**: defragmentation, 16-bit indices, textures shared across scenes, staged host writes,
pageable device memory, one bottom level a template.

## Yours to say

- **The level planner (items 6 and 9)** may be what "a residency manager" was refused as; `architecture.md` records no
  reason. New since: the handheld stood textures short beside 1.5 GiB of rest, and a move up costs up to six whole
  reads.
- **Texture memory by size class (item 9)** reopens "pools by size class, which … starve a class at the ceiling beside
  another's room". New since: the PBR tour's largest free range was 0.2–0.8 MiB with 136 MiB free; the ceiling that
  made the starvation bind goes in item 3; and a planner that moves levels multiplies the churn. It is taken only if a
  tour after item 3 still refuses a texture with room in pieces.
- **Your decisions this changes**: a mesh counts its geometry as well as its structure (item 7); every stored chain is
  whole, so the store grows by the finest levels of the data maps (item 4); every store is made again, once (item 4).
- **Upstream's code**, each a change under `AGENTS.md`'s first rule:

  | Change | Why | Cost to a merge |
  |---|---|---|
  | `ConstrainedFileStreamBuf::xsgetn` reads a large request straight into the caller's buffer (item 4) | one read and one copy in place of thousands | a conflict with an edit to the stream buffer, already the fork's (`UPSTREAM.md`, "How content is loaded") |
  | `ImageManager` tells one listener when a model image's texels are first read (item 4) | the digest and the facts are made there, not by reading the file again | the fork's own lines in upstream's file |
  | `Scene` asks the preloader's budget before it loads a door's destination (item 11) | a skipped preload has still loaded the cell | a conflict with an edit to `preloadTeleportDoorDestinations` |
  | `Nif::Reader::parse` no longer hashes the file; the two tools that print the hash make it (item 11) | a whole read a parse for a value the game never reads | conflicts in `niffile.cpp`, `bulletobjecttool`, `navmeshtool` |
  | the navigator's tile cache stands under `Resource::CacheBudget` (item 11) | a fixed 256 MiB, full, outside the budget every other cache yields to | a conflict with an edit to `NavMeshTilesCache`'s size |

  Not proposed, and noted only: unloading a cell store (its changes are a save's), Bullet's double precision, a NIF
  key's unused tangents.
- **No dependency is proposed.** A second allocator (mimalloc) is proposed only if item 5's tour still shows the heap
  holding what it freed.

## Decisions

- **Measured first, and an item its measurement does not bear out is declined**: carried from the handheld proposal.
  Most magnitudes above are read off code and one earlier run.
- **A file's digest is made once, where its texels are first read, and a key is made from digests**, as the Remote
  Execution API keys an action by its inputs' digests and never by their bytes again. The header keeps it for the
  session. Refused: a key remembered for each use of a file, which still hashes the file once a use; the preparation's
  stamp trusted in play, under which a lying stamp is a wrong picture where it is now a miss.
- **What the renderer derives of a file is derived at that first read**, on the thread that read it: the facts the
  store's memory does not hold are made then and filed. The frame's thread finds them on the header.
- **Every chain the store keeps is whole, with a digest a level**: one entry is everything the device takes, and a
  range of its levels can be read and checked alone, as KTX 2 indexes its levels for streaming
  ([spec](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html)). Refused: the coarser form, a second file for
  ever; one digest over the payload, which a ranged read cannot check.
- **A texture's levels are read by position, from where they stand, into a buffer the reader owns**: the store's entry
  for a made chain; the file's own bytes for a block-compressed file that stands as shipped, through the stream the
  game opens it as. The buffers are a bounded pool by size class, so a hand-over allocates nothing and a reader waits
  where the pool is empty, as DirectStorage reads into a bounded staging buffer
  ([guidance](https://github.com/microsoft/DirectStorage/blob/main/Docs/DeveloperGuidance.md)). Refused: a shipped
  file copied into the store, the disk twice; reading into staging, whose memory is write-combined and which the digest
  must read back; a map, since nothing is kept once staged.
- **A read nobody waits for is cancelled**, and what is read and not yet handed over is a pool buffer, so it is bounded
  by the pool.
- **Content's ceiling keeps what essential memory will grow by, not what it holds**: each owner states its next step
  (a table's doubling, a stream's next block), and the ceiling is the budget less the frame's gap and their sum.
  Refused: the rule as it stands, a second copy of memory that is appended and never made again; no reserve, under
  which a table's growth could end the renderer.
- **Room is counted where it can be taken**: free bytes a storage, a structure storage's own free runs among them, and
  a want is met by a resting row of its own class. Corrects "what waits is given the resting row that surely leaves it
  room", whose arithmetic predates the storages standing apart.
- **A mesh is its structure and its geometry, counted and given back together**: a resting mesh counts both against
  the limit, a stream's block no run touches goes back to the device, and geometry is taken as content, refused with
  its mesh. Changes "a mesh counts what its eviction gives back", whose reason holds for an evicted mesh and not for a
  resting one.
- **A texture stands at a level, and one planner moves it**: down by a copy on the device, up by a read of the levels
  it adds. RTX Remix makes a new image at the wanted levels and swaps the view
  ([`rtx_texture_manager.cpp`](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_texture_manager.cpp)),
  and Unreal lowers a plan a level at a time until it fits
  ([streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine)).
  It replaces the side chosen at arrival, the short list and its retries, and the world rebuilt for a reserve that
  moved. Where the plan fits, everything stands whole, as now. Refused: sparse images, whose binds cost NVIDIA's Linux
  driver a quarter of a millisecond a page; `VK_EXT_image_view_min_lod`, which clamps a view and frees nothing.
- **Under pressure the level wanted is the one rays sampled**: the trace records the finest level asked of each slot,
  read back two frames late off the frame slot's own copy, its peak held and let go only under pressure, as Remix
  holds it sixty frames. What rests goes first, then what stands above its want, then a level below want, the largest
  first. Refused: distance to the eye, which says nothing of a reflection or a bounce.
- **The geometry has one host copy, for one hand-over**: the reader makes it in the device's form with its key and
  bounds, adoption copies it into the arrivals' store, and both let go, as Unity and Unreal drop a mesh's host copy
  once uploaded (the host-geometry proposal's sources). The ground's masks follow the same rule.
- **The ring's records are flat, and a reference states once what is its own**: one buffer for the ring with a run a
  cell, a row a reference and a slot a part. Refused: pools that keep what each object grew to.
- **The ring reads what can stand at its distance**: a cell's references are read largest first as far as the size rule
  reaches one band ahead, and further as the cell nears; a model's radius is the preparation's to state. Refused:
  reading everything in the disc, most of which stands only within a cell or two.
- **A large buffer never reaches the allocator, and the mapping threshold is pinned**: pinning it turns glibc's own
  adjustment off (`mallopt(3)`). Refused for now: an arena cap, which serialises the loaders; a trim on a timer, which
  walks every arena for what the pool no longer frees.
- **The caches' budget counts what the heap holds free as room, and an evicted value dies off the lock and off the
  frame**, on the worker that derives the budget, where upstream had put its own expiry.
- **On unified memory the device's budget is the lesser of the driver's and what the system can still back**, with a
  smaller reserve than the caches', so the caches stop first, as decided. Windows says of such an adapter that it has
  one pool ([residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency)). Refused: one order
  across host and device, as before.
- **Every hand-over's device work has a stated bound**: what it stages. A world's build flushes at the bound, and an
  arrival past it waits a hand-over, as content without room already does.
- **A run that reports memory runs as the game ships**: no huge pages, the shipped expiry, the shipped door reach.
  Timing runs keep theirs.
- **Not now**: a model's parts and meshes kept in the store in the device's form, the cooked asset every engine ships,
  until item 8's count says what the reader's parse still costs; host image copy on unified memory; positions on a
  16-bit grid, which cracks where meshes meet.

## Final shape

Sketches of what stays; names are the plan's to settle.

```cpp
// components/resource: a file's identity, beside its size and format on the header.
struct ImageDigest { std::uint64_t mLow = 0; std::uint64_t mHigh = 0; };

// components/contentstore: an entry read in part. A chain's parts are its levels, finest first.
struct PartRange { std::uint32_t mFirst = 0; std::uint32_t mCount = 0; };
class Store
{
    // The parts' table and the trailer in one read; the range in a second, checked part by part.
    Result<PartTable, Missing> open(const Key& key);
    Result<std::size_t, Missing> read(
        const Key& key, const PartTable& parts, PartRange range, std::span<std::byte> into);
};

// components/rtx/preprocess: replaces PreparedImages' reading half. A miss still goes to the passes.
class BufferPool            // by power-of-two class; a stated bound in bytes; take() waits; give() from any thread
class LevelReader           // the hands, the queue, cancel(slot); hands out a pool buffer with its levels' spans
struct LevelSource          // std::variant<StoredChain{Key}, ShippedFile{header, first level's offset}>

// components/rtx/scene
struct PackedMesh           // spans in the device's form: positions, normal words, coordinates, colours, tangents,
                            // indices; with its MeshKey and bounds, as the reader made them
class MeshArrivals          // one flat buffer a stream, a start for each arrived slot, cleared after the hand-over,
                            // given back past its bound
struct TextureLevels { std::uint8_t mStands; std::uint8_t mWanted; std::uint8_t mFloor; };   // on TextureRow
class TexturePlan           // budget and wants in; a bounded list of moves out, a hand-over

// components/rtx/mirror/cells: one RunBuffer each for the ring, a run a cell.
struct HeldReference        // model, first part, place, RefNum, gate, radius, flags: what Placement repeated a part
// and std::uint32_t slots, one a part; a placement's transform is made where it stands

// components/rtxvulkan/device/memory
enum class MemoryUse : std::uint8_t { Frame, Essential, Mesh, Texture };   // Mesh: structures and geometry
class MemoryAllocator
{
    void oweGrowth(GrowthOwner owner, VkDeviceSize bytes);       // each essential owner's next step
    VkDeviceSize getFree(MemoryUse use) const;                   // the texture pools', or the structure storages'
};
```

Gone: `PreparedImages::mEntries`' holds, `ChainForm::Coarser`, `ShortTexture`, `SceneUploader::TextureWait` and
`evictForRoom`'s shared figure, `TextureArray::chooseLevels`, `rebuildsWorld` and `wasHeldBack`, `MeshTable`'s arrays,
`PreparedModel`'s arrays past adoption, `Placement`, the `Recycled` and `Spares` pools of cells.

## Plan

A change to what a frame reads owes `./omw repeat --pairs=10` and `./omw shot --views=all --map --upscale=off
--against`; a shader change owes `./omw kernels --against` on the desk and under the drm-shim; a hot path owes its
allocation test; each item ends with the tours it names. Vanilla's pictures do not move in any item.

### 1. Meters and a baseline

No behaviour changes. A stop's end logs, and the report keeps the run's peak of:

1. what the device holds by use, the frame's gap and each ceiling, at a stop and in `RefusedTake`;
2. the geometry's reach as placed, resting and holes, on the host and on the device;
3. image loads by cause (header, fact, key, retake after an eviction, a move up, a partner) with their bytes, and how
   many chain jobs found their chain whole in the store;
4. what the prepared images hold unhanded, in entries and bytes, and how many slots left while pending;
5. the ring's meshes with no placement, and its placements with no slot, by the cell's distance;
6. the reader's capacity against its content; loaded cell stores; the caches' "looks for room finding all in use".

Then, with no code: a handheld PBR tour under `glibc.malloc.hugetlb=0`, under a pinned `glibc.malloc.mmap_threshold`
at 256 KiB, 1 MiB and 4 MiB, and at the shipped `preload distance`. The three contents on the desk and the handheld go
to `docs/research/` as the baseline every later item is held against.

### 2. What is plainly wrong

Each its own commit and test, none a design change: the prepared images' orphan; `admits` asked for the make only on a
miss; the specular layout asked by the reader with the other facts; a want filed for a mesh refused in the loose
storage; a texture's fit compared as its image's requirements; the storages' free runs in `getContentFree` and
`getVideoBudget`; `evictForRoom` sized a storage; `mLargestStructures` and `mLargestArrival` coming down; the refusal
flag cleared when the short list empties; `reservePicture` no cause for a rebuild; `noteRefused`'s walk once a
hand-over; `CellPlacer::mMoved` bounded and skipped for actors; the stale comments the issue log names.

### 3. The device uses its budget

1. The ceiling from owed growth. A test: geometry and structures standing change no ceiling; a table about to double
   lowers it by its next size.
2. Rooms: the world's tables at 2^17 placements with their growth owed; masks as the source's bytes; a stream's first
   block made when a run lands; a structure storage's block sized by its scene's room, so a picture's is small.
3. Staging bounded: a batch flushes at the kept size; the spread's means given back past a bound.
4. Tours on the handheld, Repack and PBR: no refusal with room under the budget; item 1's figures again.

### 4. One read a texture

One bump of the store's version carries every format change of this plan, so the stores are made again once; a store
made on the desk is the handheld's too, the key being the content.

1. The header's digest and the listener (**yes**); every image pass keyed from it, with `RtxPassVersionsTest` holding
   the new versions.
2. Facts made at the first read; `factOf` reads no file.
3. Whole chains with a digest a level; `Store::open` and the ranged `read`; the kind in the entry's folder, so the
   start opens no file to learn it.
4. `xsgetn` (**yes**); `LevelReader` and `BufferPool`; cancel; the uploader gives buffers back.
5. Tests: a re-arrival opens one file; a level range read equals the decode's bytes for every block format; no
   allocation a hand-over once warm; a cancelled job holds nothing.
6. Handheld PBR tour: image loads at one a file, the tour's time, the heap's free share.

### 5. The heap and the budgets

1. The threshold pinned in `components/platform` at the value item 1 chose, with its reason beside it.
2. The budget counting the heap's free memory; evicted values destroyed on the deriving worker; `update`'s walk off the
   cache's lock.
3. The unified bound, asked as a capability of the device, never by vendor.
4. The harness: a tour and a `scene` run start as the game does; `bench` keeps huge pages.
5. Handheld tours of the three contents. If the heap still holds a fifth of what it took free, the allocator is
   proposed with those figures.

### 6. A texture stands at a level (**yes**)

1. A move on the device: a new image at the level, the kept levels copied, the slot's descriptors written, the old
   image buried after; a move up reads only the levels it adds.
2. `TexturePlan`, by room alone: an arrival stands whole where the plan fits, else at the plan's level; a reserve that
   moves changes the budget and the plan follows a bounded step a hand-over.
3. The short list, the waits and the rebuild go. Tests: a change of mode keeps every structure and geometry buffer; a
   texture brought down and up again reads its top levels once; two runs plan alike.
4. Tours of the three contents on the desk, the handheld and Windows.

### 7. One copy of the geometry

1. The reader packs, keys and bounds; `MeshTable::add` takes a `PackedMesh`; split normals stay words.
2. `MeshArrivals`; `MeshTable` drops its arrays; the masks' host copy goes with them; the tests that read arrays after
   a hand-over read them before it.
3. `PreparedModel` lets go of its arrays once adopted, on the reader's thread; a second adoption finds its meshes by
   key or asks the reader to fill it again.
4. The ring's flat records; the cell pools go.
5. The ground's indices as one run every cell's mesh names.
6. On the device: `MemoryUse::Mesh`; a block no run touches given back; the rest counting geometry.
7. The shape digest made where the host asks for digests; `repeat --pairs=10` agrees.
8. Tests: a crossing allocates nothing; the arrivals' bound. Tours: the host's peak against item 1's.

### 8. The ring reads what can stand

1. The preparation's manifest states each model's radius and whether it gives light.
2. A cell's references read as a prefix by radius, one band ahead of the size rule; `CellRing` asks the reader to go
   further as a cell nears.
3. Item 1's count again; `repeat` and `shot`, since a model must stand before the frame that first shows it.

### 9. Levels by need (**yes**)

1. The feedback as a meter that moves nothing: a word a slot in the frame slot's tables, the minimum of the levels
   asked. Kept in the trace's kernels only if no kernel changes tier on any of the three drivers and the benches stay
   within their noise; else made by a pass of its own over the frame's hit records.
2. Its histogram over the tours: the share of texture bytes in levels no ray read. Under a tenth on the handheld's PBR
   tour, the rest of this item is declined.
3. The plan's order under pressure, with the peak's hold; a floor every texture keeps.
4. Size classes (**yes**), if a tour after item 3 still refused a texture with room in pieces: a pool a class, a want
   met by the oldest resting row of its class.
5. `noise` and `shot` under a budget held low; tours.

### 10. Streams a mesh brings

Tangents and colours in runs of their own, a mesh without one reading a shared element through a stride of nought, so
the shader keeps one path. Kept only if `kernels` shows no tier change. A spike, not a commitment: a committed hit's
corners through position fetch, which would free the positions' 12 B a vertex.

### 11. The game's own (**yes**, each)

The three upstream rows above not yet taken, each proposed with item 1's figure for it, recorded in `UPSTREAM.md` in
the commit that makes it.

### 12. Close

`./omw release tour` of the three contents on the desk, the handheld and Windows; `.notes/ISSUES.md` rewritten from
them; the decisions folded into `docs/architecture.md`; this file deleted.

## Changed

## Declined
