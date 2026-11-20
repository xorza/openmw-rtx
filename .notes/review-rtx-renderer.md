# Review: the ray tracing renderer and the fork's diff

Written 2026-11-20 at `7e6bec917f` with the working tree's uncommitted allocator change (`LargestOwed`). Scope:
`components/rtx`, `components/rtxvulkan`, `components/myguirtx`, `components/contentstore`,
`apps/openmw/mwrender/rtx`, `apps/rtxtool`, their tests, and the fork's diff against upstream `eb2d2e7961`, with the
weight on content loading and memory. The history of `.notes/ISSUES.md` (649 commits), the proposals deleted as their
work landed and `docs/research/` from 2026-11-09 were read for what each mechanism answered. Nothing was built or run:
every finding is read from the code, and says so where it is a reading and not a measurement. Test times are last
night's `ctest` log's: no test over one second, the largest suite a GPU shard at 22 s.

Part one is the target design. Part two is the plan, in batches. The streaming proposal ("the world is whole before
it is shown") is carried here whole, rewritten to fit, and its own file is gone.

## Decisions

**Taken on 2026-11-20, by the user**, each asked with its options and what it reopens:

- **Content stands in slots by size class**, a free slab belonging to no class. Reopens "pools by size class, which
  reduce fragmentation and starve a class at the ceiling beside another's room" (`docs/architecture.md`, refused
  2026-11-17 on an argument, never flown). New facts: the 2026-11-20 flights (251 block refusals, 102 of them beside
  3.4–4.3 GiB of resting content; a stand-in under the budget), and the remedy for the objection itself, which is to
  move slabs between classes, as memcached's slab automover does.
- **One owner of residency, in the core** (`Rtx::Residency`). Reopens "a residency manager", refused with no reason
  recorded, and the device-residency plan's single owner, cut down on 2026-11-18 as "a refactor of mechanisms that
  work". New facts: the mechanisms do not agree (findings below), and their seam loses transitions. Refused: the one
  owner on the device, where the policy is reachable only through a card.
- **Device memory is acquired off the frame and ahead of need, and arrivals are paced.** Reopens "an arrival
  scheduler, a destroyer thread, blocks made ahead", refused on the desk's vanilla crossing
  (`research/2026-10-22-crossing-worst-frames.md`, "a cost the worst frame does not have here"). The handheld has it:
  `vkAllocateMemory` on the frame took 1.9 s of a PBR tour and a pool's new block led 926 and 445 ms frames
  (`research/2026-11-17-longest-frames.md`). No destroyer thread: the graveyard's destroys were measured cheap.
- **A texture file is found by its stamp, and looked up by its content.** The preparation's manifest keeps
  (path, stamp) → file key and facts; every store lookup stays keyed by content. A stamp that lies draws a stale chain
  until it moves, where today it costs a miss.
- **A mesh's fold is derived where its template is loaded, and kept on the template's drawable**, as a texture's key
  is on its header. Upstream's `SceneManager` gains a model deriver beside `ImageManager`'s header deriver. Refused:
  every miss of the walk a request to a worker, under which every interior's content and every actor and item stood
  late; a second cache of folds; the pass on the frame.
- **The world's wholeness is asked at three places in upstream's `Scene`**: after the grid change a walk requested
  (`Scene::update`), after `changeToExteriorCell`'s grid change, and at the end of `changeToInteriorCell`. The
  streaming proposal's "no upstream code changes" is reversed. Upstream's `changeEvent` does not tell a walk from a
  teleport (`Scene::playerMoved` asks with it true), so the kind is stated at each site.
- **The store's unreachable entries are swept in the background by their trailers**, once after the passes' versions
  change. Refused: the version in the path, a format change that prepares every store again.
- **The budgeted cache is a class of the fork's own**, and `objectcache.hpp` returns to upstream's text.
- **Upstream changes approved**: the terrain's own expiry in `CellPreloader::isTerrainLoaded`;
  `NavMeshTilesCache::releaseItem`'s count under its lock; the budget's tick off the preloader's queue, which returns
  `UpdateCacheItem` and `ResourceSystem::updateCache` to upstream's text. **Not approved**: exempting upstream's
  files from `misc-use-anonymous-namespace`; the wraps stay.
- **`GroundcoverShapes` goes**, with its two hunks in upstream's `groundcover.cpp`: only the unstarted rasterizer
  calls it.
- **The seam: its defects now, its structure a proposal of its own**, after the memory work lands.

**Taken on 2026-11-20, while the plan was worked through**, each where the plan left a choice open:

- **A pipeline cache that grows while it is read is kept as the driver's short answer.** `vkGetPipelineCacheData`
  answers `VK_INCOMPLETE` with as much as fits where the cache outgrew the size it gave, and "any data written to
  `pData` is valid and can be provided as the `pInitialData` member" of a new cache
  ([Vulkan reference](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html)). The
  read asks four times for the whole and keeps the last short blob, so the start's save always writes. Refused:
  `save` answering whether it wrote, for the start's save to try again, which under a disk that refuses writes a
  blob every 8 ms of the loading screen.
- **A cgroup's pages of files are room, active and inactive both, under the lower of `memory.high` and
  `memory.max`.** The figure this is the least with is the kernel's `MemAvailable`, which counts both lists as the
  system's to give, and past `memory.high` the kernel reclaims before it throttles
  ([cgroup v2](https://docs.kernel.org/admin-guide/cgroup-v2.html)). Refused: `inactive_file` alone, as Docker's
  `stats` subtracts ([docker container stats](https://docs.docker.com/reference/cli/docker/container/stats/)): it
  reports what a container uses, where this asks what a process may still take.
- **A quit asked under the content's preparation leaves the wait and stops the preparation, and the engine's start
  runs on to its loop**, which ends before a frame. Refused: ending `Engine::go` early, a change to upstream's start
  nobody approved, which upstream's own data loading does not make either.

- **A sprite's light bake is planned as one unit with its source**, counted, lowered and starved together, its
  stride added to the source's: it is made from the source's slot at the source's level, so a cap that holds one and
  not both must lower both (`SpriteLightPass` reads the source's slot). Refused: the bake planned after its source, which starved it beside a source a level could have spared.
- **The transit chunk is asked only where an operation found no room, no drain could make it, and the arena holds
  every chunk the plan stands in**, as "Progress is guaranteed" places a lowering's destination: the slabs held are
  of the classes the plan leaves. Refused: asking it ahead of every lowering, a chunk held past the plan for nothing
  most frames.

- **A frame draws the whole world where no demanded resident stands nothing** (`ResidencyReport::mMissingDemanded`,
  `FrameReport::mImagesPending`), not where every one stands at its plan: a raise on its way draws what stands below
  it, as the image still being made did before residency, and a handheld flight under a tight cap counted every
  cell outrun while a raise was always waiting somewhere. Batch 7's wholeness replaces it with its two predicates.
- **A texture's row that fails says why, as the upload's own check words it** (`checkUploadable`): a BC5 normal map
  read as a colour is a known format in the wrong slot, which the handheld's PBR flight's expectations name.

**Superseded by the first decision**, all "by the user's decision" in `docs/architecture.md`: "the limit is derived
from the budget" (the nine tenths stay, as the cache's share); "what waits is given the resting row that surely
leaves it room"; "both storages take blocks of content's block size … and keep their last"; "content leaves room for
one growth" for content's own growth (it stays for essential tables).

**Settled by a measurement before the code, not by a choice** ("Before anything is built"): which memory types each
driver gives an image and a structure's buffer; what each driver charges a small image, which decides whether a floor
is affordable; the slab sizes and the ladder; whether the geometry's runs join the arena.

**Kept as decided**: one file a store entry, format 3 (the research recommended append-only segments; nothing
measured here argues against the decision); no store size cap; no SQLite; the scene graph the renderer's one input;
one residency policy for textures and meshes; a texture stands from the level its nearest user asks, the most bytes
lowered first; the split of a unified device's memory by `SystemShare`; nothing pops; behind on the way, the loading
screen; ahead by upstream's predictions; frame times uniform. Not proposed: sparse residency (64 KiB pages round
every small texture up, and bind cost is erratic on NVIDIA and unbatched on AMD under Linux), VMA's defragmentation,
a bias below every want, opacity micromaps.

# Part one: the proposal

## Root causes

1. **One memory in three units.** Content is budgeted in 64 MiB blocks, counted in bytes and taken as contiguous
   runs, in three kinds of block (texture pools, kept and loose structure storages) that cannot use each other's free
   space. Every mechanism since 2026-11-16 converts between two of the three: the rest limit, eviction by fit, moves
   down, moves out, blocks coming, the waits. The rest limit works in range bytes against the budget and a take in
   blocks against a ceiling, so they cannot both hold: at a budget of 6144 MiB with 3000 in use the tenth is 314 MiB
   against a measured block slack of 250–1049 MiB.
2. **No one resolves a request.** "N bytes of this kind: take, evict that, wait until then, or refuse" is decided in
   pieces by `SceneDesc`, `SceneUploader`, `TextureArray`, `BottomLevelStore`, `MemoryAllocator` and `whenRoomless`,
   in different orders (oldest, largest, emptiest, above want), each with part of the facts and its own "held" and
   "room". Seven owners decide about one heap and see each other only through the driver's usage a refresh later.
3. **What stands is recorded twice.** The core's rows and the device's slots each say what a slot stands, joined by
   seven lists of `DeviceAnswer` copied field by field and by inference ("any refusal of kind texture"). A field is
   lost in the copy, a refusal names no slot, a slot index has no tenant.
4. **Waiting is a count and a list membership, never a state.** Two frames in flight are written as six placements
   and three hand-overs while the stamp that says when a range returns sits in the storage. A wait ends by a count of
   frames; nothing is tried again because room arrived; a refusal has no way back.
5. **The frame may compute, read and wait.** A fold the store's memory lacks is made on the frame; an arrival that
   stages 64 MiB waits out both frames in flight; a settled walk waits for the reader; an inserting thread, the
   frame's among them, evicts the caches' whole excess; a loader on the main thread reaches the disk to derive.
6. **A file is identified again from its texels each session, and the reader is never told the level wanted.** Every
   source texture is read whole and hashed to find the key of a chain the store holds; the whole chain is then read
   and digested whatever the want (16 times the bytes uploaded at a want of level 2), and again as the want moves
   finer.
7. **A cell has no record of its own state, its adoption is counted, and held means live.** The frame and the reader
   exchange lists; a cell's state is which of seven containers on two threads names it. One cell a frame costs the
   same for the sea and for a town. What the ring holds the device cannot evict: indoors, past the reach, and (as
   proposed) hidden bands all stand at full price.
8. **The budgets have no driver.** The caches' tick is a queue item behind the preloader's work; the device's share
   of a unified machine is sampled only when content arrives; the system's memory is read from five places on three
   threads.
9. **The harness reports memory and judges none of it.** No finding exists for over budget, a stand-in drawn, content
   short or waiting, a counter that drifted or a largest free run; one sample a leg end; a frame at each leg's
   boundary reaches no sample. Device-side policy is reachable only through a GPU.
10. **The seam grew by caller, and a renderer's life is a protocol of calls.** Fifty-five virtuals in seven
    responsibilities, ten ordered start calls guarded by asserts, a loading screen carrying obligations it does not
    own.

The one fix in the history that removed a cause changed a unit: structures out of the texture pools (`84a60139e8`).
The design below changes the unit once, for everything.

## The design in one page

- **One unit.** Content stands in *slots*; a slot belongs to a *size class*; a class's slots stand in *slabs*; slabs
  are cut from *chunks* of device memory. Within a class every slot is the same size, so a free slot serves any
  request of its class and "does it fit" is a comparison of counters. This is the slab allocator (Bonwick 1994) over
  a page allocator, as the kernel, jemalloc and memcached run it.
- **One ledger, in the core.** `Rtx::SlotLedger` owns which slot of which slab is free, reserved, live or retiring.
  The backend maps a slot to bytes and counts nothing. The driver's budget feeds one number, the arena's cap.
- **One owner plans, then commits.** `Rtx::Residency` turns what is wanted into a plan that fits, by lowering the
  least protected a level at a time, and only then issues operations, each with its destination reserved, under a
  fixed share a frame. This is the shape of Unreal's texture streamer and Unity's
  ([texture streaming overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine)).
  Nothing waits for memory, and nothing is refused for room but a cell at its admission.
- **One contract.** The core hands the backend operations that name slots; the backend returns the epoch they ride
  and, later, what finished. A null device keeps the same contract, so the policy is tested with no card.
- **One state a thing.** A texture, a structure, a slab, a block and a cell each carry one sum type that says where
  it is. "Waiting" is the derived predicate *stands ≠ planned*.
- **The frame issues and collects.** It reads no file, runs no pass and waits on no thread; every queue has a stated
  bound and every bound a handled case.
- **Whole is a predicate that ends.** Every resident reaches *stands as planned* or *failed*, the plan fits by
  construction, so "the band is whole" always becomes true. The loading screen is held on it and on nothing else.

## Device memory: blocks, slabs, slots

`components/rtxvulkan/device/memory/contentarena.{hpp,cpp}` (new) holds the memory; the core's ledger says what
stands where.

- **An arena is one memory the device takes content from**: a Vulkan memory type, which the core knows as a small
  index (`ArenaIndex`). The backend says at start which arena an image stands in and which a structure's buffer
  (`SceneHolder::arenaOf`). Where one type takes both, as RADV's first device-local type is read to, there is one
  arena and a slab moves freely between kinds; where a driver gives them two, a kind's free *block* goes back to the
  device and comes to the other, on the worker. Refused: assuming one type, which the allocator today does not
  (`contentTypesOf` keeps a pool a type) and which batch 2 measures on the three machines.
- **A block is device memory of one arena, acquired and released by a worker** (`ChunkSupply`), the frame asking by
  name and told by an event. A *chunk* is a block of 64 MiB; a *large* block is the size of the one resource it
  holds.
- **A slab is 4 MiB of a chunk, sixteen a chunk, or a whole chunk, and holds one class.** A slab of a structure
  class is one `VkBuffer` bound to its range, made as the slab joins the class; a slab of an image class is a range
  images bind into. Images and buffers never share a slab, which keeps `bufferImageGranularity` out of the
  arithmetic.
- **Three tiers, by a slot's stride.** *Small*, at most 512 KiB: eight or more to a 4 MiB slab. *Medium*, at most
  8 MiB: the slab is a whole chunk. *Large*, past that: a block of its own, one slot, which is a 4096² chain's level 0
  and little else. One path serves all three: a class has room where it has a free slot, a free slab, a free chunk,
  or its block has arrived.
- **Two sizes of slab and no more**, so that emptying has a proof: a free small slab takes any small slab's content,
  so a chunk of small slabs can always be emptied into as many free small slabs elsewhere, and a medium class wants
  only whole chunks. A third size makes the emptying recursive, as the compaction of Linux's buddy allocator is, and
  with as little guarantee. If replay shows the medium tier strands too much (a medium class of one resource holds a
  chunk), the remedies in order are a coarser ladder for medium structures, then a medium resource standing in a free
  slot of the next class up before its own class takes a chunk; a third slab size is last.
- **A class is a kind and a stride.** The stride is the price rounded up a ladder of eight steps an octave
  (`2^k·(8+i)/8`) in units of 256 bytes, an acceleration structure's alignment, then up to the resource's own
  alignment. A class is made when first asked. A block-compressed chain of power-of-two sides comes to just over
  `(4/3)·2^k`, which the ladder's `11/8` holds at 3% over; the ladder's worst is 11%, its average about 6%. Refused:
  powers of two (up to 100% over, as [OffsetAllocator](https://github.com/sebbbi/OffsetAllocator) measures them);
  the driver's exact requirement as the key, which leaves the class count to the content.
- **A free slab belongs to no class**, and a free chunk to no tier. Any class takes one; a class gives its slab back
  as its last slot goes.
- **A slab is drained when a slab is wanted and none is free, or the arena stands past its cap**
  (`SlotLedger::drainFor`): of the slabs whose content has free slots of its class or free slabs elsewhere, the one
  with the fewest live bytes is marked *draining*, takes no new slot, and its content moves out under the frame's
  copy share; emptied, it returns to the pool. A chunk is drained the same way, slab by slab. Driven by demand, so
  nothing moves while everything fits. This is the evacuation of a region with the least live data, as a
  garbage-first collector chooses it, and what `TextureArray::moveOut` and `BottomLevelStore::moveOut` do today with a
  destination that can fail.
- **What cannot be used is bounded by the layout**: less than a slab's worth of free slots in each class in use, and
  one chunk kept for moves in transit. Estimated at 100–350 MiB on the handheld's Repack from class counts guessed
  off the content's sizes; replay measures it before the backend is touched. Today's slack is measured at
  250–1049 MiB and has no bound.
- **A chunk is asked one ahead of need and released only while the arena stands past its cap.** A chunk given back
  on a unified machine is refilled by the caches and bought back with a stall, which is how the defragmentation
  flights failed. A block the device refuses holds the arena at what it has: it is asked again only once the budget
  has risen by a block or a block has gone back, a state and not a count of tries.
- **A structure's loose build and its tight copy are two slots of two classes of the one arena.** The loose and the
  kept storage, apart so that short-lived builds cut no holes between long-lived ranges, are one: a class has no
  holes of another size.

`MemoryAllocator` keeps the library's default pools for the frame's targets and what is essential, the driver's
budget, the frame's reserve and `LargestOwed` for essential tables. It loses content's pools, both `tryTake`s,
`tryTakeOutside`, the ceilings a use, `getRoom`, `getContentOver`, `getBlocksOver`, `asksForBlock`, the three
registries other owners push into (`mStructureFree`, `mBlocksComing`, content's owed growth) and `RefusedTake`.
`MemoryUse` becomes `EssentialUse { Frame, Essential }`, the only argument `take` and `make` accept, so memory that is
never refused is a type and not a value an assert checks.

### The arena's cap

One figure, read once a frame:

```
capBytes = max(0, B − (U − A) − gap − owed − transit)
  B       min(the driver's budget, the run's limit, the system's bound)
  U       the process's usage of the heap as the driver states it
  A       the arena's blocks, whole
  gap     what the frame's reserve has beyond what its targets hold
  owed    the largest growth one essential table owes (`LargestOwed`)
  transit one chunk
```

- **Everything the process holds outside the arena is subtracted as it stands**, so there is no ceiling a use and no
  order between structures and textures but the plan's. The system's bound is `SystemShare`'s, sampled by the arbiter
  ("Host memory") on its own period, and never on a hand-over.
- **A cap of nought is a handled case**: the plan lowers to floors, then starves what is furthest, and the reach
  shrinks; nothing throws.
- **A change of mode is a cut for memory.** The reserve for the new mode is stated first; content settles under the
  new cap in covered frames ("The world is whole before it is shown") and gives its chunks back; then the targets
  are made. In place of `makeTextureRoom`'s one blocking batch, and of NGX's feature made before the room for it.

## The ledger

`components/rtx/common/slotledger.{hpp,cpp}` (new). No scene, no API, plain values in and out.

```cpp
namespace Rtx
{
    enum class SlotKind : std::uint8_t { Image, Buffer };
    inline constexpr std::size_t sSlotKindCount = 2;

    enum class SlabTier : std::uint8_t { Small, Medium, Large };

    struct SizeClassRows;
    struct SlabRows;
    struct BlockRows;
    using ClassIndex = RowIndex<SizeClassRows>;
    using SlabIndex = RowIndex<SlabRows>;
    using BlockIndex = RowIndex<BlockRows>;

    /// One memory of the device that content stands in, as the backend numbers them (`SceneHolder::arenaOf`).
    enum class ArenaIndex : std::uint8_t {};

    /// Device bytes by name: `stride * mAt` into slab `mSlab`. None where `mSlab` is `sNoRow`.
    struct Slot
    {
        SlabIndex mSlab = sNoRow;
        std::uint16_t mAt = 0;

        friend bool operator==(const Slot&, const Slot&) = default;
    };

    /// Whose a slot is, as its owner packs it: what a drain hands back for the owner to move.
    enum class SlotOwner : std::uint32_t {};

    /// When a submit is done with what it read: opaque and never falling, the backend's timeline in the core's words.
    struct DeviceEpoch
    {
        std::uint64_t mValue = 0;

        friend auto operator<=>(const DeviceEpoch&, const DeviceEpoch&) = default;
    };

    struct SlotLayout
    {
        std::uint64_t mChunkBytes = std::uint64_t{ 64 } << 20;
        std::uint64_t mSlabBytes = std::uint64_t{ 4 } << 20;
        std::uint64_t mSmallStrideBytesMost = std::uint64_t{ 512 } << 10;
        std::uint64_t mMediumStrideBytesMost = std::uint64_t{ 8 } << 20;
        std::uint32_t mLadderStepCount = 8;
        std::uint32_t mUnitBytes = 256;
        std::array<ArenaIndex, sSlotKindCount> mArenaOf{};
    };

    /// What a drain is of: one slab, or every slab of a chunk.
    struct Drain
    {
        BlockIndex mBlock = sNoRow;
        SlabIndex mSlab = sNoRow;       // none where the whole block drains
        std::uint64_t mLiveBytes = 0;
    };

    class SlotLedger
    {
    public:
        /// Asserts the layout: a chunk a whole number of slabs, both powers of two, the tiers rising.
        explicit SlotLedger(const SlotLayout& layout);

        /// The class a resource of `bytes` at `alignment` stands in, made where new.
        ClassIndex classOf(SlotKind kind, std::uint64_t bytes, std::uint64_t alignment);
        std::uint64_t getStrideBytes(ClassIndex of) const;
        SlabTier getTier(ClassIndex of) const;
        ArenaIndex getArena(ClassIndex of) const;

        /// Names a block to be acquired: a chunk, or the one a large class's resource stands in.
        [[nodiscard]] BlockIndex askChunk(ArenaIndex arena);
        [[nodiscard]] BlockIndex askLarge(ClassIndex of);
        void blockArrived(BlockIndex block);                 // asked → held: sixteen free slabs, or one slot
        void blockRefused(BlockIndex block);                 // asked → gone
        std::optional<BlockIndex> freeBlock(ArenaIndex arena) const;      // held, nothing of it in a class
        void blockReleased(BlockIndex block);                // asserts it free

        /// Whether `reserve(of, …)` has a slot: a free slot of the class outside what drains, a free slab, a free
        /// chunk for a medium class, an arrived block for a large one.
        bool hasRoom(ClassIndex of) const;
        [[nodiscard]] Slot reserve(ClassIndex of, SlotOwner owner);       // asserts hasRoom
        void commit(Slot slot);                              // reserved → live
        void unreserve(Slot slot);                           // reserved → free: an operation withdrawn before it rode
        void retire(Slot slot, DeviceEpoch until);           // live → retiring
        void collect(DeviceEpoch finished);                  // retiring → free; a slab with no slot to the pool

        /// What a plan of `counts` slots a class stands in, by arena, in bytes of whole slabs, chunks and large
        /// blocks: Σ ceil(n / slots a slab). The plan's only arithmetic.
        std::uint64_t bytesFor(RowSpan<const std::uint32_t, ClassIndex> counts) const;

        /// Marks draining what must empty for a free slab of `tier` to come in `arena`, and answers it: nothing
        /// where nothing can be emptied into what is free elsewhere.
        std::optional<Drain> drainFor(ArenaIndex arena, SlabTier tier);
        /// The owner of the next live slot of what drains, lowest slab and slot first; none once it is empty.
        std::optional<SlotOwner> nextToMove() const;

        SlotCounts count() const;                            // blocks, slabs and slots by state, bytes by arena
        bool isConsistent() const;                           // every slot in one state, every count its sum: debug
    };
}
```

- **Every state is a closed set with one function a transition**, each asserting the state it leaves. A slot: free,
  reserved, live, retiring (two bits a slot in its slab's words). A slab: free, in a class, draining. A block: asked,
  held, releasing. Nothing is a flag beside a list.
- **A class keeps its slabs with free slots in a list, fullest first**, so a reserve fills the fullest slab and
  leaves the emptiest to drain: O(1) a reserve, a commit, a retire.
- **Retiring is by epoch, not by count**: `retire` takes the epoch of the submit that last reads the slot, which the
  backend returned, and `collect` the epoch the backend says is finished. `sMostRoomWaits`, the three hand-overs of
  cooling and `StructureStorage::mCooling` are this one stamp. It is the D3D12 residency library's sync point.
- **`bytesFor` is the admission test**: a class of `n` slots stands in `ceil(n / k)` slabs once drained down to it,
  which draining can always reach.
- **A slot knows its owner**, so a drain can ask for what stands in it; the owner is the resident's scene, kind and
  row, packed by `Residency`.

## Residency

`components/rtx/renderer/residency.{hpp,cpp}` (new), one a backend, owned beside it by `RtxRenderer` and by the
harness. It replaces the residency half of `SceneUploader`, `RestQueue`, `RestLimit`, `ArrivalWindow`, `DeviceAnswer`,
and on the device `TextureArray::makeRoom`, `chooseLevels`, `moveOut`, `waitAgain`, `BottomLevelStore::moveOut`,
`mWaiting` and `whenRoomless`.

```cpp
namespace Rtx
{
    class Residency
    {
    public:
        /// What one frame issues at most while the game runs.
        struct Shares
        {
            std::uint64_t mUploadBytes = 0;
            std::uint64_t mCopyBytes = 0;
            std::uint32_t mBuildTriangleCount = 0;
            std::uint32_t mTightCopyCount = 0;
        };

        /// Under a loading screen: until `mUntil` on the run's clock, in the same order.
        struct Slice
        {
            Clock::time_point mUntil;
        };

        using Pace = std::variant<Shares, Slice>;

        /// What one scene's hand-over is of.
        struct HandOver
        {
            SceneHolder& mDevice;
            SceneSlot mSlot;
            SceneDesc& mScene;
            TextureFeed& mFeed;
            Pace mPace;
        };

        Residency(const SlotLayout& layout, const Clock& clock);

        void addScene(SceneSlot slot, const SceneRoom& room);
        void dropScene(SceneSlot slot);                      // its residents are dropped as its last hand-over rides

        /// Once a frame, before any scene is handed over: the cap, what finished, what the feed made.
        void openFrame(SceneHolder& device, TextureFeed& feed);
        /// Plans where an input moved, issues the scene's operations under the pace, and hands the scene over.
        void handOver(const HandOver& handing);
        /// Once a frame, after the last: blocks asked and released.
        void closeFrame(SceneHolder& device);

        /// Whether a scene has operations queued, so its owner hands it over though nothing in it moved.
        bool owes(SceneSlot slot) const;
        /// Whether no operation is queued or in flight for a resident of `atLeast` or a more protected tier.
        bool isSettled(HoldTier atLeast) const;
        /// Whether `texture` stands from `level` or finer: what a composite asks of its layers.
        bool standsFrom(SceneSlot slot, TextureIndex texture, std::uint8_t level) const;
        /// Whether what `prices` add at their floors fits beside what is `Forced` and `Shown`: a cell's admission.
        bool admits(std::span<const ContentPrice> prices) const;

        const ResidencyReport& getReport() const;
    };
}
```

### Residents

One record a texture row and a mesh row of each scene, in a `RowVector` beside the scene's own table, indexed by the
row's index: flat, no map, no list of waits.

```cpp
namespace Rtx
{
    /// Who wants a row, most protected first. A row's tier is the best tier any hold on it states.
    enum class HoldTier : std::uint8_t
    {
        Forced,   // a picture's scene, the sky's sheets, a holder that reads the texture itself
        Shown,    // a placement of the walk, a cell inside the drawn reach, the band a load waits on
        Hidden,   // a cell of a predicted band
        Kept,     // a cell past the bands and inside the kept ring; the exterior while the eye is indoors
        Unheld,   // nothing names it: what rests
    };

    struct NotStanding {};
    struct Failed { Refused mCause; };                 // content the renderer cannot use: terminal until the row goes
    struct Stands { std::uint8_t mLevel = 0; std::array<Slot, 2> mSlots; };       // the image, and its companion
    struct Arriving { std::uint8_t mToLevel = 0; std::array<Slot, 2> mTo; FeedTicket mFeed; };
    struct Restanding { Stands mNow; std::uint8_t mToLevel = 0; std::array<Slot, 2> mTo; FeedTicket mFeed; };
    using TextureStanding = std::variant<NotStanding, Arriving, Stands, Restanding, Failed>;

    struct TextureResident
    {
        TextureStanding mStanding;
        std::uint8_t mFloorLevel = 0;                  // the first level whose longer side is 64 texels or fewer
        std::uint8_t mWantedLevel = 0;                 // `TextureTable::getWant`, held (below)
        std::optional<std::uint8_t> mPlannedLevel;     // nothing where the plan drops it
        std::uint32_t mWantFellAtHandOver = 0;
        std::uint32_t mGeneration = 0;                 // the row's tenant, which a ticket and an operation carry
        RetentionLink mLink;                           // its place in its tier's list
    };

    struct Building { Slot mLoose; };                  // reserved, the build recorded
    struct Loose { Slot mLoose; };                     // built; its tight size asked
    struct Copying { Slot mLoose; Slot mTight; };      // the copy recorded
    struct Tight { Slot mAt; };
    struct KeptLoose { Slot mAt; };                    // a structure that refits: never copied
    using StructureStanding = std::variant<NotStanding, Building, Loose, Copying, Tight, KeptLoose, Failed>;

    struct StructureResident
    {
        StructureStanding mStanding;
        bool mPlanned = false;
        ClassIndex mLooseClass = sNoRow;               // priced as the mesh arrived
        std::uint32_t mGeneration = 0;
        RetentionLink mLink;
    };
}
```

- **A row's tier travels with its holds.** `SceneDesc::holdMesh`, `holdMaterial`, `holdTexture` and `addInstance`
  take a `HoldTier`; a row keeps a count a tier (five counters), and its tier is the best non-zero one. A cell that
  changes band drops and takes its holds at the new tier, a few hundred a cell. `Unheld` is what
  `RestPlace::mResting` says today.
- **A row and its resident share an index and a generation.** `HeldRows` counts a generation a slot, raised as the
  slot is freed; a feed ticket and an operation carry it, and what arrives for another tenant is dropped in one
  place. Replaces the image-pointer compare, `isLive` alone, and nothing at all, which guard a stale slot today.
- **A texture has a floor.** Its chain from 64 texels a side down, about 5 KiB in BC7. A texture that stands at all
  stands its floor, so "the stand-in for want of room" is not a state: the stand-in is drawn for `Failed`, and for a
  walk's texture in the frames before its first texels arrive. Whether a driver charges a small image little enough
  for 8191 floors is measured first; where it does not, the floor is the level that fills the driver's least
  allocation.
- **A level change is a new image**, as today (`standMovedDown`): a slot of the new level's class, the levels both
  share copied on the device, the finer ones uploaded, the descriptors owed to both sets, the old slot retired at the
  epoch of that submit. A move out of a draining slab is the same operation at the same level.

### The plan

Run on the frame's thread at a hand-over that moved an input (a want, a tier, a row, the cap), and a pure function of
its inputs, so two processes plan alike. The counts a class are kept as wants and tiers move, so a hand-over whose
demand still fits pays for what changed; the pass below runs only where the demand does not fit, or fits again after
it did not, over at most the textures' capacity and the meshes' rows.

```
plan(capBytes):
    counts[class] = 0                                   # slots the plan gives each class
    fits(c)  = ledger.bytesFor(counts with c one more) <= limit

    # 1. What is demanded, most protected first; within a tier in row order.
    limit = capBytes
    for tier in Forced, Shown, Hidden:
        for r in tier:
            count r where it fits: a texture at its wanted level, a structure at its slot
            else leave it for step 2 with nothing counted

    # 2. What did not fit is made to: lower, least protected first, the most bytes first.
    while something of step 1 is uncounted:
        v = the counted texture of the lowest tier at or below the uncounted one's, above its floor,
            standing the most bytes at its planned level, lowest row first among equals
        none → break
        lower v one level; count the uncounted where they now fit, floors first
    whatever is still uncounted is starved: planned at nothing, named in the report        # step 4

    # 3. What nothing demands is kept while it fits the cache's share.
    limit = demanded + (capBytes - demanded) * 9 / 10
    for tier in Kept, Unheld, most lately held first:
        keep r at the level it stands where it fits, a texture a level coarser where that fits, else drop it

    # 4. A floor or a structure of Shown that is starved: the streamer shrinks the reach a ring (a finding).
```

- **The plan ends**: every step of 2 lowers one finite level; floors and structures of a cell were admitted as the
  cell was (`Residency::admits`).
- **The plan does not thrash**: `mWantedLevel` rises at once and falls only once the coarser want has stood
  `sWantHoldHandOvers`; a texture is raised only where the plan fits a chunk under the cap, and kept up to the cap;
  the cap follows a one-minute average. Nothing is lowered while everything fits.
- **The most bytes first**, as `chooseLevels` and `makeRoom` choose today and as `docs/architecture.md` records. The
  walk is by class from the largest stride down, a class's residents in a list, so a step costs what it lowers.
- **Refused**: least recently used alone (Dawn, the D3D12 residency library), which resolves "does not fit" by
  waiting on the device and then overcommitting; a global bias fed back from pressure, which is reported to
  oscillate in Unreal and lowers everything for one shortage; the plan on a worker, whose result would depend on
  when it landed.

### Operations and a frame's share

The plan against what stands is a list of operations, kept in order: drops, lowerings, what a drain moves, then
arrivals and raisings, the most protected first.

```cpp
struct StandImage                 // an arrival, a raise, a lowering, a move out of a draining slab: one operation
{
    TextureIndex mTexture;
    std::uint32_t mGeneration = 0;
    std::uint8_t mImage = 0;               // 0 the texture, 1 its companion
    Slot mTo;
    std::uint8_t mLevel = 0;
    std::optional<SlotLevel> mFrom;        // what stands, whose shared levels are copied on the device
    LevelSpan mUpload;                     // the finer levels, in the feed's buffer; empty for a move
};
struct BakeImage { TextureIndex mTexture; std::uint32_t mGeneration = 0; Slot mTo; };   // a composite, a sprite's light
struct DropImage { TextureIndex mTexture; std::uint8_t mImage = 0; Slot mFrom; };
struct BuildStructure { MeshIndex mMesh; std::uint32_t mGeneration = 0; Slot mLoose; };
struct CopyStructure { MeshIndex mMesh; Slot mFrom; Slot mTo; StructureCopy mKind; };   // Compact, or Clone for a move
struct DropStructure { MeshIndex mMesh; Slot mFrom; };
using ResidencyOp = std::variant<StandImage, BakeImage, DropImage, BuildStructure, CopyStructure, DropStructure>;
```

- **An operation is issued with its destination reserved, or stays in the list.** Where its class has no room, the
  frame's copy share goes first to the drain that makes it. The only delay is the retirement of frames in flight,
  which is an epoch and not a guess.
- **An operation rides its own scene's hand-over**, since a structure that moves is named again by that scene's top
  level in the same submit. A scene with operations queued is handed over that frame though nothing in it moved
  (`Residency::owes`): a picture's scene whose texture stands in a draining slab is placed once more, which is under
  a millisecond.
- **A frame's share is fixed**: bytes uploaded, bytes copied on the device, triangles built, tight copies. Bytes and
  triangles, not milliseconds: a wall-clock slice makes the frame a cell stands on the machine's mood, and overruns by
  its largest step, as Unreal's does. Under a loading screen the same loop runs to a slice of the run's clock a
  present, in the same order. First values: 16 MiB uploaded, 8 MiB copied (today's `sMoveOutPerPlacement` and
  `sCompactionPerPlacement`), both set from the handheld's copy rate in batch 2.
- **A raise asks the feed for the levels it lacks, and no more** (`TextureFeed`, below): the levels from the planned
  one to the one that stands. What the device holds is copied on the device, so a level that comes back reads one
  level.
- **Progress is guaranteed.** With no free slab and an operation wanting one, more slabs stand in classes than the
  plan's `bytesFor`, so some class holds a slab's worth of free slots, or content the plan drops; the first is a
  drain, the second a drop, and neither needs a slab. The chunk kept for transit is where a lowering's destination
  stands while its source retires. Held by a property test over random plans.

### The contract with a backend

`components/rtx/renderer/sceneholder.hpp`, in place of `setScene`, `extendScene`, `placeScene`, `takeAnswer`,
`getArrivalRoom`:

```cpp
struct ContentPrice
{
    std::uint64_t mBytes = 0;
    std::uint64_t mAlignment = 1;
};

/// What the device finished and what it found, since the frame before was opened.
struct DeviceEvents
{
    DeviceEpoch mFinished;                              // every submit up to it is done reading
    std::vector<TightSize> mTightSizes;                 // a loose structure's size once copied tight
    std::vector<BlockIndex> mBlocksArrived;
    std::vector<BlockIndex> mBlocksRefused;             // the device had none

    void clear();
};

struct ContentBudget
{
    std::uint64_t mCapBytes = 0;
};

virtual ArenaIndex arenaOf(SlotKind kind) const = 0;                    // read once, as the layout is made
virtual ContentPrice priceImage(const ImageShape& shape) const = 0;
virtual ContentPrice priceStructure(const StructureShape& shape) const = 0;

/// Reads the budget and fills `events`, which the caller keeps and this clears first.
virtual ContentBudget openFrame(DeviceEvents& events) = 0;
virtual void standScene(SceneSlot slot, const SceneDesc& scene) = 0;    // a slot's first build; nothing stands
virtual DeviceEpoch handOver(SceneSlot slot, const SceneDesc& scene, std::span<const ResidencyOp> ops) = 0;
virtual void acquireBlock(BlockIndex block, ArenaIndex arena, std::uint64_t bytes) = 0;      // answered by an event
virtual void releaseBlock(BlockIndex block) = 0;                                             // nothing stands in it
```

- **`handOver` appends what arrived, executes the operations in order, and places**: extend and place are one call,
  since the top level names every instance either way. The epoch it returns is the submit they ride.
- **No operation can be refused for room**: its slot is the core's. A device that cannot make a block says so in the
  events; a device that fails otherwise throws `DeviceError`, as today.
- **`standScene` is a first build and nothing else.** The rebuild of a standing world, which the interface still
  promises and the host has had no geometry for since `MeshArrivals`, goes.
- **`NullRenderer` keeps the contract as a device with a budget** (`NullDevice`): prices by formula, a slot table
  that asserts no slot is stood twice or reused before its epoch, events a frame late, a block refused on a test's
  word. One conformance suite runs over it and over Vulkan.

## Content: from a file to texels

### A file's record

`components/rtx/preprocess/sourceindex.{hpp,cpp}` (new), filled by the preparation and kept in its manifest.

```cpp
struct SourceFile
{
    std::uint64_t mStamp = 0;          // `FileStamps::stampOf`: the archive or folder that holds it, and its time
    ContentStore::Key mFileKey;        // the hash of its levels as described: what every pass keys from
    ImageShape mShape;                 // format, sides, levels: what a header holds
    FileFacts mFacts;                  // the four facts a material asks, one entry
};
```

- **A header is made from the record where the stamp holds**, with no read of the file: `ImageManager::getModelImage`
  asks `SourceIndex::find(path, stamp)` first. A miss reads and derives as today, on the loader's thread, and files
  the record.
- **A record whose file is no older than the manifest's own write is not trusted**, as Git treats a racily clean
  entry ([racy-git](https://git-scm.com/docs/racy-git)) and ccache its inode cache.
- **The four fact passes are one** (`FileFactsPass`), one key, one entry: `derive` computes all four at once already,
  describing the finest level four times. `ImageFactCache`, `HeaderDerived`'s mutex and `FileKeys`' modal name go;
  the header takes its record as a constructor argument.

### The feed

`components/rtx/preprocess/texturefeed.{hpp,cpp}` (new), in place of `PreparedImages`, `SceneTextures::Pending` and
the uploader's waits.

```cpp
struct FeedRequest
{
    FeedTicket mTicket;
    ContentStore::Key mFileKey;
    TextureEncoding mEncoding{};
    ContentStore::Key mPartnerKey;
    std::uint8_t mFirstLevel = 0;
    std::uint8_t mLevelEnd = 0;
};

struct Made { PooledBuffer mTexels; LevelTable mLevels; };
struct FileStands { PooledBuffer mTexels; LevelTable mLevels; };      // the file's own chain, read by range
struct NotAdmitted {};                                                // memory: asked again by the next plan
struct FeedFailed { Refused mCause; };
using FeedOutcome = std::variant<Made, FileStands, NotAdmitted, FeedFailed>;

struct FeedArrival
{
    FeedTicket mTicket;
    FeedOutcome mOutcome;
};

class TextureFeed
{
public:
    bool hasCredit() const;                          // at most `sInFlightMost` requests, and the pool's bytes
    void ask(const FeedRequest& request);            // asserts hasCredit
    std::optional<FeedArrival> take();               // the next completion, in the order asked; never blocks
    void forget(FeedTicket ticket);                  // its completion is dropped as it lands
};
```

- **What is not yet issued never leaves the frame's thread**: it is an operation in `Residency`'s list, so to move it
  up or withdraw it costs nothing. Only what is in flight is past recall, and that is bounded by the credit.
- **The asker owns the result.** A completion is a value handed to `Residency`, which hands its buffer to the
  operation that uploads it and lets it go as the hand-over ends. The map keyed by image address, the held-then-watched
  pointers, the sweep and `shared_ptr<BufferPool>` go; `PooledBuffer` holds `BufferPool&`, which outlives every
  holder by its place in `RtxRenderer`.
- **Completions are taken in the order asked**, so two processes stand one image on one frame under a settled run
  whatever their threads' timing.
- **A hand reads the segments asked** (`Store::open`, `readSegments(first, end)`): format 3 keeps a segment and a
  digest a level for this. A miss makes the chain on the hand, writes it, and hands the levels asked.
- **Admission is one sum**: what the hands hold in the pool and what they are making, against one figure the arbiter
  derives, in place of each hand asking the system alone (`admitsChain`).
- **A composite and a sprite's light bake are residents that depend on others.** A composite's layers are held
  `Forced` at their bake level until it is baked, and `CompositeQueue` asks `Residency::standsFrom` in place of the
  sentinel `mStoodBytes > 0`. A bake is an operation like any other and is planned like any other, so a nameless
  texture has the way back a file has.

### A fold is derived where its template is loaded

- **A model deriver beside the header deriver**, by the user's decision. `Resource::SceneManager` calls it once a
  template, after the load and before any other thread sees the template, on whichever thread loads: the preload
  worker, the cell reader, or the main thread on upstream's own synchronous load. The fork's deriver finds each
  drawable's fold in the store or makes it (`ShapePass`), and keeps it on the drawable. Instances share their
  template's drawables (`SceneUtil::CopyOp` copies no drawable), so the walk and the reader both find the fold by the
  drawable: one route to a fold where today there are two readers held equal by an assert.
- **The fold's bytes are the template's**: counted by `geometryBytes` under the one cache budget, gone when the last
  holder of the drawable goes. A fold that changed nothing is a mark and no arrays.
- **The frame runs no pass.** A drawable no template carries (the sea's sheet, what a script builds) is a request to
  a worker and stands when its fold returns, which a tour counts (`LateStand`). `Reach` goes.
- **The store keeps nothing in memory.** With the facts in the manifest and the folds on their templates, `mResident`
  (512 MiB, filled once, never evicted), `Holding`, `mAtAnySize`, the stamps and their ranking, the start's read of
  every standing model's shapes and `prepareSea` have no reader.
- **The store's faults**: a read that fails is a counted miss (`Platform::File::readAt` answers a `Result` and loops
  on a short count, on both systems); a payload is bounded on every path before a buffer is taken; a refusal reads
  whether its delete worked; entries of a version no pass has are swept by their trailers on the store's thread,
  once after the versions change.

## The world is whole before it is shown

The streaming proposal, rewritten. Its decisions stand but the one on upstream's code; what the review found is under
each.

### Decisions

- **Nothing pops: the world is whole before a frame shows it**, by the user's decision. *Whole* is one predicate,
  `CellStreamer::isWhole(band) && Residency::isSettled(tier)`: no cell of the band left to stand, and no operation
  queued or in flight for a resident of that tier or a more protected one. It ends, since every resident reaches what
  the plan gives it or `Failed`. Refused: streaming in view; a timed hold, a duration standing in for a predicate.
- **The wait is made where the cells change, on the thread that does the work**, by the user's decision.
  `RenderingManager::standWhole(change, position, listener)` forwards to the seam, and `RtxRenderer::standWhole`
  loops covered frames until whole, opening a `Loading::ScopedLoad` of its own only where it has to wait. Upstream's
  `Scene` calls it at three places, each stating its kind: `Scene::update`, after the deferred grid change a walk
  requested (`WorldChange::Crossing`); `Scene::changeToExteriorCell`, after its grid change; and the end of
  `Scene::changeToInteriorCell` (`WorldChange::Cut`). The position is the one the eye is to stand at, since the
  player is moved only after. About six lines in `scene.cpp` and a forwarder, recorded in `UPSTREAM.md`. Refused:
  `Terrain::World::preload` blocking a preload worker until the ring stands, the proposal's first form: that worker
  and the main thread wait on each other with only an unsignalled abort flag between them, `Loading::Reporter::wait`
  draws only as the worker reports and the loading screen skips a report that moves its bar less than a pixel, every
  prediction held the one queue the next cells' models load on, and a band the budget cannot stand could only block
  for ever or be recorded as loaded. Refused too: upstream's `changeEvent` as the kind, which a walk sets as a
  teleport does; a kind inferred from how far the eye moved, which misreads a scripted move of one cell.
- **A cut waits for everything, a crossing for its band.** A load, a teleport, a door and a fast travel loop until a
  frame is whole. A walk across a cell's edge loops only where the eye's band is not whole, and returns without a
  frame where it is. What is shown meanwhile is upstream's: the frozen frame under the progress bar while a game
  runs, the wallpaper only from the menu, and nothing at all in a wait's first 50 ms.
- **Behind on the way, the loading screen**, by the user's decision: a crossing whose band is not whole is that loop.
  Refused: a silent stall; pop-in.
- **Ahead by upstream's predictions**, by the user's decision. `TracedTerrain::preload` registers a band around each
  position upstream predicts and returns at once; `TracedTerrain::createView` returns a view that is the band's hold,
  so the band goes as upstream resets or destroys the view, which is upstream's own signal for "no longer
  predicted". What upstream predicts, as read: the motion's position a second on, outdoors only; fast-travel
  destinations where `preload fast travel` is on (off by default); never a teleport door's destination, whose cells
  it preloads as models alone. So a door out to land the ring does not hold is a cut and waits, which is the decision
  above and not a fault.
- **Hidden until in reach.** A cell of a predicted band stands in the scene's tables and on the device and is drawn
  by nothing until the eye's reach takes it in. It needs no flag: drawn is derived from the eye, as `CellPlacer::place`
  derives it today for a held cell past the reach. This is Unreal's *loaded, not visible*.
- **A band is a claim on memory at its tier, not a count.** A predicted band's cells hold their content `Hidden`: the
  plan gives it room after everything shown and takes its levels first. A band the plan cannot stand is not whole,
  and the eye's arrival there is a wait, which is honest. In place of "at most the cells of two reaches": the bands
  are bounded by the views upstream holds, and their memory by the plan. A hidden cell asks its textures by its
  distance from its own band's centre, or the land would arrive whole and then sharpen.
- **A share of work a frame while the game runs, a slice of time a present under the loading screen**, both in one
  order. In place of "one cell a frame": a cell's adoption is cut into steps of bounded cost (its first-seen models,
  its ground, its placements in runs), and the frame takes steps while its share lasts, at least one. Under the
  screen cells are adopted nearest first and waited for, as a settled walk does, so two runs give a cell the same
  slots.
- **Indoors the land rests.** With no eye band the exterior's cells hold their content `Kept`; the plan keeps it
  while it fits and gives its room to the interior where it does not; the reader's ask is withdrawn. Today the ring
  keeps up to 497 cells' content live beside a room nothing of it is drawn in.
- **A tour fails on a pop**, three findings: `Pop`, a cell adopted inside the drawn reach; `LateStand`, a placement of
  the walk whose mesh or texture stood after its first frame; `LevelStep`, a shown texture raised a level in view. The
  second and third are costs until warm-ahead (below) lands, then faults. And one cost a tour counts:
  `HeldAtCrossing`, a crossing whose band was not whole, with how long it waited, which is how often the flight outran
  the predictions.

### The streamer

`components/rtx/mirror/cells/cellstreamer.{hpp,cpp}` (new), in place of `CellRing` and `CellSupply`'s lists.
`CellReader` and `CellPlacer` stay.

```cpp
enum class BandKind : std::uint8_t { Eye, Awaited, Predicted };

struct Band
{
    CellCoord mCentre;
    std::uint8_t mReachCells = 0;
    BandKind mKind = BandKind::Eye;
};

struct Wanted {};
struct Reading {};                                       // the reader has it in hand
struct Read { PreparedCell* mPrepared = nullptr; };
struct Adopting { PreparedCell* mPrepared = nullptr; HeldCell* mHeld = nullptr; AdoptCursor mCursor; };
struct Stood { HeldCell* mHeld = nullptr; };
using CellStage = std::variant<Wanted, Reading, Read, Adopting, Stood>;

struct CellRecord
{
    CellStage mStage;
    CellStage mGrassStage;            // its groundcover, the same machine
    std::uint8_t mBandBits = 0;       // which bands hold it
    HoldTier mTier = HoldTier::Kept;  // the best its bands give
    std::uint32_t mOrder = 0;         // nearest its nearest band, most protected first: what the reader takes next
};
```

- **One table, cell → record, under the reader's monitor.** The reader takes the wanted record of the least order,
  marks it `Reading`, and files it `Read`; a record with no band left as its read ends is given back and never
  handed. The seven containers, the takes counters and `AskInputs` are this table. "Is it on its way" is the record;
  "is the band whole" is a count of its records not `Stood`.
- **The reader waits at a bound**: `sReadAheadMost` cells read and not adopted. Today it reads the whole list, up to
  429 cells, while the frame adopts one a frame.
- **A model's packed streams go back as the frame reports it adopted.** A lent model keeps a second host copy of its
  geometry for as long as a cell names it today, 20–52 MiB on the desk's Repack legs.
- **Groundcover is the record's second stage**, not a second ring threaded through four classes as pairs.

```
CellStreamer::pump(around, pace):                        # frame thread; inside a walk or a covered frame
    bands   ← the eye's band; the band standWhole waits on; each view's band (a small locked list)
    retarget: each band's disc sets its bit on its records, making them Wanted; a record with no bit past the kept
              ring is dropped (its holds go to Unheld, its prepared cell back to the reader); tiers follow the bits
    publish the table's wants under the monitor, where they moved
    collect what the reader filed Read
    adopt, least order first, while the pace admits the next step:          # at least one step
        models first seen (a mesh and its material a step), the ground, placements a run a step → Stood
    place: stand and drop what crossed the drawn reach (CellPlacer::place)
    report: cells not Stood a band; whether each band is whole
```

### Warm-ahead, after the first tours

A crossing activates three cells whose actors and items the ring never held. Their folds are on their templates by
then, where upstream preloaded the cells; what is still met on that frame is their textures' first texels and their
structures' builds. Two things can be made ahead: a texture's floor levels read into a small host cache as its
header is made, so the walk uploads a floor on the frame it meets the texture; and the reader holding those cells'
models `Hidden` one step outside the active grid. Not in the first build: `LateStand` measures what each would buy.

## Host memory

- **One sampler, one thread** (`Resource::MemoryArbiter`, new, `components/resource/`): once a second it destroys
  what the caches evicted, then reads the system's memory once, then derives both the caches' budget and the device's
  bound from that one reading, then evicts. The device's held bytes reach it as a figure the renderer posts each
  frame. In place of the preloader's `UpdateCacheItem`, the read inside `refreshBudget` on a hand-over, and the
  claim told each frame. `SystemShare`'s arithmetic stays as it is, sampled on a period at last. Refused: two budgets
  each derived from the other's leftovers at its own moment, which is how a block given back went to the caches.
- **What a loader inserts, it makes room for and no more** (`makeRoomForInsertion(bytes)`); the rest is the
  arbiter's.
- **A value leaves a cache by one door.** `removeFromObjectCache` moves the value to the evicted list as eviction
  does, so texels given back by the renderer are freed off the lock and off the frame.
- **A cgroup's room is its limit less what it holds that cannot be reclaimed**: `memory.current` less the file pages
  of `memory.stat`, against the least of `memory.max` and `memory.high`.
- **The budgeted cache is a class of the fork's own** beside upstream's timed one (`BudgetedObjectCache`), by the
  user's decision: single-flight, the order of use, bytes and the evicted list. `GenericResourceManager` takes the
  cache's type, and a budgeted manager has no expiry delay to read by mistake.

## Ownership

```
Engine
└─ RtxRenderer
   ├─ Rtx::Renderer (VulkanRenderer | NullRenderer)
   │    └─ Device → MemoryAllocator (frame, essential), ContentArena → ChunkSupply (thread), Graveyard, Timeline
   ├─ ContentStore::Store (disk; one thread of its own)
   ├─ BufferPool
   ├─ TextureFeed → hands (threads), each a ContentPreprocessor
   ├─ SourceIndex
   ├─ Residency → SlotLedger; a scene's residents a scene slot; the operations
   └─ WorldMirror → SceneDesc, SceneExtractor, CellStreamer → CellReader (thread), CellPlacer; SceneUploader
ResourceSystem → MemoryArbiter (thread) → CacheBudget → the budgeted caches
```

A part is handed what it needs at the call: `Residency` takes the device, the scene and the feed as arguments, and
holds no reference to a sibling.

| shared today | verdict |
|---|---|
| `shared_ptr<BufferPool>` in every `PooledBuffer` | goes: a reference, the pool declared before every holder |
| `Store::Resident::mPayload`, `Opened::mResident` (`shared_ptr<const ByteBuffer>`) | goes with the store's memory |
| `Entry::mHeld` → `mWatched`, `mFileHeld` → `mFileWatched` (`ref_ptr` then `observer_ptr<MadeImage>`) | goes: the asker owns a completion |
| `TextureWait::mSource`, `mPartner`, `Pending::mSource` (`ref_ptr<osg::Image>` as a slot's identity) | goes: a generation |
| `MemoryAllocator::mStructureFree`, `mBlocksComing`, `mRefusedCount`, `mRefusedMutex`, content's `LargestOwed` | go: the ledger is asked |
| `MemoryAllocator::mHeld` atomics, `LargestOwed`'s mutex | kept only if an essential take is shown to come off the frame's thread (the compile hands' binding tables); else plain |
| `ImageHeader::mDerivedMutex`, `mutable mDerived` | goes: a constructor argument |
| `ImageDerivation`'s mutex, condition variable and free list | goes: a counting semaphore over the loaders, which is all it is |
| `GenericObjectCache::mFlights` (`shared_ptr<Flight>`) | in the budgeted cache, the `std::shared_future` itself |
| `CacheBudget`'s `mBytes` atomic, refreshed from two threads | goes: derived in `getBytes()` |
| `Renderer::mScreenshotWriter` (`ref_ptr`, the work queue's last holder) | a borrow: the threads then join where `Engine` ends them |
| `SharedTexture`, `PaintedMirror` (`ref_ptr` to a source "the caller owns") | a reference, as their headers state |
| `VisibilityPass::mCanonicalMutex`, its condition variable and four states | go: which pipelines are shared is known before a hand starts |
| `CellSupply`'s monitor | kept: the one channel between the frame and the reader, now over one table |
| the streamer's list of predicted bands | new, kept: the preload worker writes it and the frame reads it; a mutex over a list bounded by upstream's views |
| `ChunkSupply`'s request and event queues | new, kept: the one channel between the frame and the worker that makes blocks |
| `Graveyard::mLock`, `Timeline::mSubmitted`, `PipelineTally`, `PipelineCodeFolder`, `ValidationLog` locks | kept: compile hands and the layers call from any thread |
| `LogTally::mMutex`, `CardWatch`'s atomic | kept |
| `PreparedTexture::mImage`, `PreparedModel::mTemplate`, the material resolver's key `ref_ptr`s | kept: OpenSceneGraph's own model, and the address is the identity |
| `shared_ptr<OffscreenView>` of a map tile | kept: a paint outlives its segment by frames |
| `NavMeshTilesCache::Item::mUseCount` atomic read outside the lock | counted under the lock |
| `Files::SharedFile` | kept: an archive's handle and each stream's |

## Call graphs

**A frame** (main thread but where marked):

```
RtxRenderer::drawFrame
  WorldMirror::mirror
    SceneExtractor::extractWorld → walk
      CellStreamer::pump(around, the frame's share)        # adopt steps; place what crossed the reach
    SceneExtractor::retire
  Residency::openFrame(device, feed)
    budget = device.openFrame(events)                      # the cap; what finished
    ledger.collect(events.mFinished); tight sizes, blocks arrived and refused
    feed.take() …                                          # completions, by generation
  WorldMirror::hand → SceneUploader::hand
    SceneDesc::orderLights, compact; CompositeQueue::advance
    Residency::handOver({ device, slot, scene, feed, pace })
      plan(budget.mCapBytes)  where an input moved
      issue: reserve, ask the feed, fill ops while the pace admits; a drain where a class lacks room
      epoch = device.handOver(slot, scene, ops)
      commit destinations; retire sources at epoch; let the feed's buffers go
  drawViews: each picture's scene handed over where it changed or `Residency::owes` it
  Residency::closeFrame(device)                            # acquireBlock one ahead; releaseBlock past the cap
  trace; present
[feed hand]    Store::open → readSegments(first, end) | ChainPass::run → write; completion queued
[reader]       CellStreamer's table: take the least order Wanted → CellReader::read → file Read
[chunk supply] vmaAllocateMemory | vmaFreeMemory → event
[arbiter]      destroy evicted; read the system; derive both budgets; evict
[any loader]   SceneManager loads a template → the model deriver → the fold on each drawable
```

**A texture comes nearer**: `MaterialTable::ask` → `TextureTable::ask` → the resident's `mWantedLevel` rises → the
plan gives it the finer level where it fits → `StandImage` queued → the frame reserves a slot of the finer class and
asks the feed for the levels it lacks → a hand reads those segments → the next hand-over uploads them, copies the
rest on the device, swaps the descriptors, retires the old slot at that epoch → `Stands` at the finer level.

**The cap falls**: the arbiter's bound or the driver's budget comes down → `openFrame` answers less → the plan
lowers the most bytes of the lowest tier until it fits → drops and lowerings ride the frame's copy share → slots
retire → the arena stands past its cap, so the chunk with the fewest live bytes drains → `releaseBlock` → the worker
frees it → the usage falls.

**A cut**: `Scene::changeToInteriorCell` | `Scene::changeToExteriorCell` → cells loaded →
`RenderingManager::standWhole(Cut, position, listener)` → `RtxRenderer::standWhole`: loop { covered frame: walk,
pump around `position` with a slice, hand over; whole? return; `listener.increaseProgress` →
`LoadingScreen::draw` → `renderGui` }.

**A crossing**: `Scene::preloadCells` each frame → `CellPreloader::setTerrainPreloadPositions` →
`TracedTerrain::preload` on the preload worker → the band's centre into the streamer's locked list, and returns →
the pump reads and stands the band hidden at the frame's share → `Scene::playerMoved` requests the grid change →
`Scene::update` runs it → `standWhole(Crossing, …)`: the eye's band whole? return, with no frame; else the loop
above.

**A change of mode**: `PendingSettings` taken at the next frame → the backend states the new mode's reserve → covered
frames until `Residency` is settled under the new cap and the arena within it → the backend makes the targets.

## Layout

| new | in place of |
|---|---|
| `components/rtx/common/slotledger.{hpp,cpp}`, `deviceepoch.hpp` | `scene/restqueue.hpp`, `renderer/arrivalwindow.hpp`, `roomwaits.hpp` |
| `components/rtx/scene/holdtier.hpp` | — |
| `components/rtx/renderer/residency.{hpp,cpp}`, `resident.hpp`, `residencyop.hpp`, `nulldevice.{hpp,cpp}` | `scene/deviceanswer.hpp`; the waits of `renderer/sceneuploader.*`; `renderer/scenetextures.*`' pending |
| `components/rtx/preprocess/sourceindex.*`, `texturefeed.*`, `modelderivation.*`, `texture/filefactspass.*` | `preparedimages.*`, `preparedheld.hpp`, `imagefactcache.*`, `imagederivation.*`, `filekeys.*`, `headerderived.hpp`, four fact passes |
| `components/rtx/mirror/cells/cellstreamer.*`, `band.hpp` | `cellring.*`, `cellsupply.*`, `ringstanding.hpp` |
| `components/rtxvulkan/device/memory/contentarena.*`, `chunksupply.*` | `structurestorage.*`, `blocklist.hpp`, `blockscoming.hpp`, `owedgrowth.*` for content, `scene/whenroomless.hpp` |
| `components/resource/memoryarbiter.*`, `budgetedobjectcache.hpp` | the budget's half of `objectcache.hpp`, `UpdateCacheItem` |

The folder order holds: the ledger knows no scene (`common/`), the tier is the scene's (`scene/`), the feed is before
the renderer's folder and `Residency` in it, the streamer after both.

## What goes

| mechanism | fate |
|---|---|
| `RestQueue`'s size classes, `firstFitting`, `RoomWant`, `FitBytes`, `evictFor`, `serveWants`, `WantCooling` | gone: any slot of a class fits, and the victim is chosen by tier, never by shape |
| `RestLimit`, `restLimitOf`, `ArrivalWindow`, `evictResting`, `evictForRoom` | gone: the cache is what the plan leaves, nine tenths of what the demand leaves |
| `TextureArray::makeRoom`, `chooseLevels`, the retry a level down | the plan's step 2, decided before anything is made |
| `TextureArray::moveOut`, `BottomLevelStore::moveOut`, `chooseBlock`, `blockToEmpty`, `mStuck`, `mFruitlessAt` | `drainFor`, with a destination that cannot fail |
| `BlocksComing`, `asksForBlock`, `getBlocksOver`, `whenRoomless`, `sMostRoomWaits`, `waitAgain` | gone: an operation is issued with its slot or stays queued in a plan that fits |
| `mTextureWaits`, `mMovedResting`, `mPending`, `mFiner`, `serveWaits`, `waitForRoom`, `settleMovedResting` | one resident a row |
| ceilings a use, content's owed growth, `mStructureFree`, `getContentFree`, the let-go subtraction | one cap |
| the kept and the loose structure storage, `RunAllocator` in blocks | two classes of one arena |
| `DeviceAnswer`'s seven lists | operations one way, events the other |
| `makeTextureRoom`, the rebuild of a standing world | content settling under a new cap in covered frames |
| `Reach`, the store's memory, stamping, `prepareSea`, `standingDigest`'s shape reads, the frame's `ShapePass` | the fold on its template |
| `PreparedImages`' table and protocol, `ImageDerivation`, `ImageFactCache`, `FileKeys` | the source index and the feed |
| `CellRing`'s lists, `CellSupply`'s channels, the pairs for groundcover | one cell table |

## Before anything is built

- **The prices each driver states** for the texture ladder and for structures; which memory types an image and a
  structure's buffer take, which fixes the arenas; what the least image costs, which fixes the floor: one harness
  verb, on the three machines.
- **Replay.** A tour writes its residency inputs a hand-over (asks, tiers, arrivals, drops, the cap); a host program
  replays them through `Residency` over `NullDevice` and reports what is stranded, operations a frame, frames to
  converge and the plan's cost. It settles the slab sizes, the ladder's steps, the frame's shares and the hold, on
  the handheld's Repack and PBR tours, before the backend changes.
- **The handheld's device copy rate**, which sets the copy share and so how fast a plan is reached.
- **The geometry's runs.** A mesh's vertices and indices stand in essential blocks that never go back, so their
  high-water takes from the cap for good. Their hole count and free bytes after a tour say whether they join the
  arena as slots, which changes how a hit finds a vertex and is not proposed until measured.

## What must not get worse

From `research/2026-11-17-baseline-tours.md`, `2026-11-18-closing-tours.md`,
`2026-11-18-handheld-rooms-and-device-memory.md` and `2026-11-20-handheld-open-issues.md`: the handheld's Repack
rooms at a p99 median of 21–23 ms at the baseline (28.6 at `7ccdebfbb4`), its land legs at 81–87; the handheld's PBR
at 21.6–22.5; the desk's Repack at 4.2–4.4 and vanilla at 1.6–1.7; texels read from files on the handheld's Repack at
0.4 GiB a tour; over budget at no more than 2 stops of 155; lasting faults at one mesh and one stand-in, which this
design is to take to none; `repeat --pairs=10` agreeing; `shot --against` moving nothing.

# Part two: the plan

Twelve batches, the most impact first, each after what it needs; the first, the defects that stood whatever the
design, is done. Each leaves the build and the tests green; a batch that changes what a frame reads owes
`./omw repeat --pairs=10` and `shot --against`, and a batch that changes memory owes the handheld's Repack and PBR
tours beside the desk's. Whoever addresses an item deletes it. An item marked
**(upstream)** changes upstream's code and is recorded in `.notes/UPSTREAM.md` as it lands: those approved on
2026-11-20 say so, and any other waits for a yes of its own.
An item that says "ends" names a defect in code its batch deletes: it is there so the new shape gets the test the
old one lacked.

## Batch 2: see it first

The harness judges memory, and the prices and a tour's inputs are recorded, before anything changes.

Files: `apps/rtxtool/{session,measurer,tourwatch,checks}.cpp`, `apps/rtxtool/model/`, `components/rtx/renderer/
{memoryreport,framereport}`, `apps/openmw/mwrender/rtx/framereport.hpp`, `tools/omw/tour.py`,
`apps/components_tests/rtxtool/`.

What is left here reads `Residency`'s report and the core's types, so it lands with batch 4, after the contract.

- [ ] `apps/rtxtool/measurer.cpp:462-467` — memory is one sample a leg's end. Target: `FrameReport` carries a
  frame's residency (operations queued and issued, bytes uploaded and copied, slots by state, residents below their
  want, stand-ins drawn, starved) and the record keeps each leg's most.
- [ ] `apps/rtxtool/model/finding.hpp` — no finding for memory. Target: `OverBudget` (the driver's usage past its
  budget, by frames), `BelowWant` (a shown texture coarser than wanted for more than N frames), `StandInDrawn`
  (frames a shown slot drew it), `Starved`, `LedgerDrift` (the arena's chunks against the driver's usage less what
  is outside it), each with its threshold stated.
- [ ] `tour --record-residency=<file>`: a hand-over's asks, holds by tier, arrivals, drops and budget, in the core's
  types. One Repack and one PBR tour of the handheld and of the desk are kept as the replay's inputs.

## Batch 3: the ledger and the plan, with no device

New core code and its tests. The game does not use it yet.

Files: `components/rtx/common/{slotledger,deviceepoch}`, `components/rtx/scene/{tier.hpp,rowhold.hpp,scenedesc,
meshtable,texturetable,materialtable,placementtable}`, `components/rtx/common/slots.hpp`,
`components/rtx/renderer/{residency,resident,residencyop,nulldevice}`, `apps/components_tests/rtx/{common,renderer}`,
`apps/rtxtool` (replay).

What is left here reads batch 2's residency records, which land with batch 4, so it follows them.

- [ ] The replay program over batch 2's records: stranded bytes, operations a frame, hand-overs to converge after a
  cut, the plan's time. It fixes `SlotLayout` (a third slab order or not; the ladder's steps), the frame's shares and
  the hold; a dated note records them, and the estimate under "Device memory" is replaced by its figures.

## Batch 5: structures onto the arena

Files: `components/rtxvulkan/scene/{bottomlevelstore,sceneacceleration,devicescene,whenroomless.hpp}`,
`components/rtxvulkan/device/memory/{memory,structurestorage,blocklist,blockscoming,owedgrowth,buffer,image}`,
`components/rtx/scene/{meshtable,restqueue.hpp,deviceanswer.hpp,scenedesc}`, `components/rtx/renderer/
{arrivalwindow.hpp,roomwaits.hpp}`, the tests of each.

- [ ] The first and second open issues (`.notes/ISSUES.md`) flown on the handheld's Repack tour, the first with a CPU
  profile of the rooms after the land legs.
- [ ] `RunAllocator`'s fate decided on the holes a whole Repack tour leaves its vertex and index runs (the report's
  `vertexHoles`, `indexHoles`), now that nothing walks them a placement.

## Batch 6: content, from a file to texels

Files: `components/rtx/preprocess/` (all), `components/rtx/prepare/`, `components/contentstore/`,
`components/rtx/image/{madeimage,imagedescription}`, `components/rtx/common/bufferpool`,
`components/resource/{imagemanager,imageheader}`, `components/rtx/mirror/{meshreader,meshresolver}`,
`apps/openmw/mwrender/rtx/{worldmirror,contentmodels}`, `components/detournavigator/navmeshtilestore.cpp`.

- [ ] `SourceIndex` in the preparation's manifest, as specified; `ImageManager::getModelImage` makes a header from
  a record whose stamp holds (`components/resource/imagemanager.cpp:224-250`). A record newer than the manifest's
  write is not trusted. The read of every source texture a session ends.
- [ ] `imagemanager.cpp:241-250` — two loaders that meet a file at once both derive it. Target: one flight a path,
  through the helper `getOrLoad` uses.
- [ ] `FileFactsPass`: one pass, one entry, in the source's record. `ImageFactCache`, `HeaderDerived`,
  `ImageHeader::mDerivedMutex`, `FileKeys::name/forget`, `ImageDerivation`'s lent preprocessors go; loaders are
  bounded by a counting semaphore. `hashFile` is counted in `ContentStats`.
- [ ] `contentpreprocessor.cpp:120` — every level is read and digested whatever the want. Target: the request's
  levels, by `readSegments(first, end)`; `MadeImage` holds levels from the first asked. On a miss the pass runs into
  the pooled buffer (`:163-171` copies the chain once more).
- [ ] `contentpreprocessor.cpp:28,127` — `run` and `runPooled`, two wrappers of one miss; `chain`/`convert` with
  `ContentAsk::Output`, `ContentCache::find<ChainPass>` and `ContentCodec<MadeChain>::decode` have no caller outside
  tests; `TexelRead::Fact` is never produced. Target: one wrapper; the dead ones go.
- [ ] **(upstream, approved)** `components/resource/scenemanager.{hpp,cpp}` — a model deriver beside
  `ImageManager::setModelImages`' header deriver, called once a template after its load and before any other thread
  sees it. `Rtx::ModelDerivation` finds or makes each drawable's fold and keeps it on the drawable; `geometryBytes`
  counts it. About ten lines upstream.
- [ ] `components/rtx/mirror/meshreader.cpp:641`, `cells/templatewalk.cpp` — a fold the store's memory lacks is made
  on the frame, and its write dropped, every session; the reader and the walk are two readers of a fold. Target: both
  read it off the drawable; a drawable with none is a request to a worker and stands when it returns; `Reach` goes.
  Read first: that a deformed drawable's clone shares the source geometry the fold is kept on
  (`components/sceneutil/clone.cpp:52-66`).
- [ ] `components/contentstore/store.cpp:605-617` and its callers — the store's memory (`mResident`, `Holding`,
  `mAtAnySize`, `mStamped`, `StampTask`, `ranks`), `PreparationManifest::standingDigest`'s read of every shape at a
  start (`preparationmanifest.cpp:256`), and `prepareSea` (`worldmirror.cpp:254`) have no reader once the frame asks
  nothing. They go.
- [ ] `store.cpp:792` — `refuse` counts a delete it did not check, and deletes by path, so a late refusal can
  delete a fresh write. Target: the delete's answer is read; a refusal names the digest it refused.
- [ ] Entries of a version no pass has are unreachable and never removed (chains are at version 8). Target: the
  store's thread sweeps them by their trailers, once after the passes' signature changes, a marker file holding the
  signature last swept. Format 3 stays.
- [ ] `pp/texture/chainadmission.cpp:16`, `worldmirror.cpp:240` — play's chain misses are admitted a chain against
  the system's free memory, each hand alone, and the preparation's budget is read once. Target: one sum over the
  hands against a figure the arbiter derives (batch 8; the system's own until then).
- [ ] `PooledBuffer` holds `BufferPool&` in place of a `shared_ptr`, its pool outliving every made image by owner.
- [ ] `common/bufferpool.cpp:102-107,136` — `take` frees evicted buffers under the mutex a frame's give-back takes,
  and `give` frees on the dropping thread, the frame's included. Target: both past the lock, on the hand.
- [ ] `apps/openmw/mwrender/rtx/contentmodels.cpp` — the preparation lists no groundcover model
  (`MWWorld::GroundcoverStore`), so each is a miss on the reader at first sight. Target: listed.
- [ ] `components/detournavigator/navmeshtilestore.cpp:121,141,162-166` — a tile is copied three times a read and
  a write, a fresh buffer each. Target: the loader owns its buffers.
- [ ] `img/imagedescription.cpp` (`openImage`) pulls `Resource::ImageManager` into the decode folder; errors are
  strings `hashFile` and both chain passes discard. Target: the read is handed in; a cause is an enum.
- [ ] Tests: two loaders of one file; a template loaded on two threads at once derived once; a read error on an entry;
  a cancel and a shutdown with a request in flight; a stamp that moved; a record too new to trust.
- [ ] `docs/architecture.md` "Loading" and "The content store" written again.

## Batch 7: the world is whole before it is shown

Files: `components/rtx/mirror/cells/` (all), `components/rtx/mirror/sceneextractor`, `apps/openmw/mwrender/rtx/
{rtxrenderer,worldmirror,tracedterrain,framereport}`, `apps/openmw/mwrender/{renderer.hpp,renderingmanager}`,
`apps/openmw/mwworld/scene.cpp`, `apps/rtxtool/{stager,session}`, `apps/rtxtool/model/`.

- [ ] `CellStreamer`: the cell table, the bands, the pump, as specified, in place of `CellRing`'s walk and
  `CellSupply`'s lists (`cellsupply.hpp:135-197`, `cellring.hpp:263-312`). `AskInputs`, `mOnTheWay`, the takes
  counters, `mHanded`, `mHandedGrass` and every pair for groundcover go.
- [ ] `cellsupply.cpp:189-229` — the reader reads the whole list while the frame adopts one a frame, and a stale
  cell is always handed. Target: it waits at `sReadAheadMost`; a record with no band is given back unhanded; the
  stop token is asked between a cell's models (`cellsupply.cpp:52` joins a whole cell's read on the frame).
- [ ] `cellring.cpp:358-380` — the adoption's budget is a count. Target: steps of bounded cost under the frame's
  share, resumable inside a cell (`AdoptCursor`); under a covered frame, a time slice in the settled order.
- [ ] `mirror/sceneextractor.cpp:718-719` — the ring can stand only inside the extractor's walk. Target: the pump
  is called by the walk and by a covered frame alike.
- [ ] `cells/prepared.hpp:243`, `cells/held.cpp:54` — a lent model keeps its packed streams for as long as a cell
  names it. Target: the reader lets them go as the frame reports the model adopted.
- [ ] `cellring.cpp:521-527` — indoors the ring keeps all it holds live and the reader reads on. Target: no eye
  band; the cells hold `Kept`; the ask is withdrawn. One room leg flown after land legs says whether this is the
  rooms' 21 against 51 ms.
- [ ] Tiers: a cell's holds are taken at its record's tier and retaken as it changes; a hidden cell asks its
  textures by its distance from its own band's centre (`cellplacer.cpp:612-617` asks by the eye's).
- [ ] `TracedTerrain::preload` registers a band and returns; `createView` answers the band's hold
  (`tracedterrain.hpp:93-95`). The list between the preload worker and the frame is the streamer's, locked, bounded
  by upstream's views.
- [ ] **(upstream, approved)** `apps/openmw/mwworld/scene.cpp`: `mRendering.standWhole(kind, position, listener)`
  at three places, each stating its kind: `Scene::update` after the deferred grid change (line 381), `Crossing`;
  `changeToExteriorCell` after its `changeCellGrid` (line 1037) and the end of `changeToInteriorCell`, `Cut`.
  `RenderingManager::standWhole` forwards to `Renderer::standWhole`, empty for the rasterizer. `changeEvent` is not
  the kind: a walk sets it as a teleport does. Cost: a conflict with an upstream edit at any of the three.
- [ ] `vulkanrenderer.cpp` (`createTargets`) — a change of mode makes its targets at once, and content settles under
  the new cap in the frames after it. Target: the new mode's reserve is stated, content settles under it in covered
  frames, then the targets are made, and with them NGX's feature after its room.
- [ ] `RtxRenderer::standWhole` and the covered frame: `Between → Walking → Placing → Gui → Between`, a phase
  sequence of its own; a world event from the run's hook is staged between frames by the harness
  (`apps/rtxtool/stager.cpp:223`), so `standWhole` is entered from `Between` alone.
- [ ] `FrameReport::isWhole` is the two predicates; `mCellsToStand`, `mImagesPending` and the upload's kind are
  what they report. It sees what it does not today: a mesh not built, a texture below its plan, a composite not
  baked.
- [ ] Findings `Pop`, `LateStand`, `LevelStep`, and the cost `HeldAtCrossing` (model, tests, the expectation files).
- [ ] `cellplacer.cpp:107-133,96-105` — `setNightDay` and each `setGate` walk every placement of every held cell.
  Target: a short index a cell of the placements a switch or a gate touches; gate changes applied in one pass.
- [ ] `cellplacer.cpp:904-917` — every lamp of every shown cell is made again each walk. Target: the still ones
  made at adoption.
- [ ] `cellplacer.cpp:815-822` — a change of the field of view re-asks every material of every held cell. Target:
  the ask's step is derived from a quantity that does not follow a zoom, or the re-ask is measured and kept.
- [ ] `cellring.cpp:102-110` — a reader rebuilt is silent. Target: one line naming which field of `CellWorld`
  moved.
- [ ] `SceneExtractor::walk(…, CellRing*, bool falls)`, `readModel(path, bool groundcover)`,
  `onTheWay(cell, bool grass)`, `moveReference(…, bool away)` — flags select behaviour. Target: an enum each.
- [ ] Tests: a world change while a cell is read; the eye indoors; a cell entering and leaving the grid, "never both,
  never neither" frame by frame; a band whose view goes; `standWhole` over `NullRenderer`, by kind; the bound on cells
  read ahead.
- [ ] `.notes/todo.md`'s unsettled-tours item and "The ring reads what can stand" go or are rewritten against the
  streamer; `docs/architecture.md` takes the decisions.

## Batch 8: host memory

Files: `components/resource/{memoryarbiter,cachebudget,objectcache.hpp,budgetedobjectcache.hpp,resourcesystem,
resourcemanager.hpp,imageheader}`, `components/platform/{memory*,heap*}`, `apps/openmw/mwworld/cellpreloader.cpp`,
`components/rtxvulkan/device/memory/{memory,systemshare.hpp}`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp`.

- [ ] `Resource::MemoryArbiter`: a `std::jthread` at one second that destroys, reads, derives both budgets and
  evicts, as specified. `SystemShare` is sampled by it (`memory.cpp:670-687` samples only when content arrives, so
  a claim outstanding when arrivals stop is withheld from the caches for good); `CacheBudget::yieldTo` each frame
  and the read on a hand-over go. `Platform::availableMemory` has one reader in a process
  (`memory.cpp:680`, `resourcesystem.cpp:102`, `chainadmission.cpp:21`, `worldmirror.cpp:246`,
  `vulkanrenderer.cpp:387` today).
- [ ] **(upstream, approved)** `cellpreloader.cpp:357-364` — the budget's tick is a queue item behind the preloader's
  work. Target: `UpdateCacheItem` and `ResourceSystem::updateCache` return to upstream's text.
- [ ] `cachebudget.cpp:104` — an inserting thread evicts the whole excess, the frame's included, where the header
  says as many bytes as it added. Target: `makeRoomForInsertion(bytes)`.
- [ ] `objectcache.hpp:275-285`, `imageheader.cpp:41-45` — `removeFromObjectCache` destroys under the cache's lock
  on the caller. Target: the value goes to the evicted list.
- [ ] `cachebudget.cpp:87-94` — `mBytes` is a stored function of three atomics refreshed from two threads. Target:
  derived in `getBytes()`.
- [ ] **(upstream, approved)** `objectcache.hpp` — two residencies in one class behind a nullable pointer, eight
  branches, 357 lines in upstream's file. Target: `BudgetedObjectCache` of the fork's beside an untouched upstream
  cache; `GenericResourceManager` takes the cache's type. `mFlights` holds the `std::shared_future`.
- [ ] `components/platform/heapposix.cpp:63-72` — `malloc_info` walks every arena under its locks once a second, the
  shape Windows' `HeapSummary` was removed for at 1.2 s. Target: its p99 measured on the handheld's heap; kept at a
  period the figure allows.
- [ ] `cachebudget.cpp:14-15` — the heap's free memory counts in full as the caches' room though blocks of 1 MiB
  and over never enter an arena. Target: measured against a tour's resident set, then counted for what small
  entries can take, or not at all.
- [ ] `cellpreloader.cpp:243-249`, `scene.cpp:1229` — `admitsPreload` is asked twice a door a frame, a preloaded
  door's stamp is not refreshed before it is refused, and `hasRoom` has a byte of hysteresis. Target: asked once,
  after the refresh.
- [ ] `components/platform/filestdio.cpp` is in no target. It goes.
- [ ] Tests: the tick's order; `CacheBudget`'s own file; the arbiter over a fake system; a claim that ends when
  arrivals stop.

## Batch 9: the device's foundation and the passes

Files: `components/rtxvulkan/device/` (but the arena), `components/rtxvulkan/{frame,present,trace,denoise,upscale,
display,gui}`, `frametracer`, `picturetracer`, `vulkanrenderer`, `components/myguirtx/`.

- [ ] `texture.cpp:507-511` → `Batch::flush` → `Device::waitFor` — an arrival that staged 64 MiB waited out both
  frames in flight on the frame. With an upload share a frame it is reachable only under the loading screen; the
  flush stays there and is asserted out of a played frame.
- [ ] `device/memory/batch.cpp:79-84` — every batch that stages a byte takes its own 8 MiB block for two frames:
  48–72 MiB carrying kilobytes, by a count of batches not measured. Target: one append cursor a submit, shared by
  the batches that ride it; the count measured first.
- [ ] `device/memory/stagingring.hpp:22-34`, `vulkanrenderer.cpp:454,812` — one constant is the ring's floor, the
  spares' floor and a batch's ceiling; 600 submits is 200–300 frames, not the ten seconds the document states;
  trim and shed are the renderer's. Target: the ring owns its policy, in frames and bytes.
- [ ] `device/device.hpp:160-203` — `const Device&` hands out every mutable part, so a compile hand holds the right
  to submit. Target: a resource holds a narrow view (bury, name, allocate); the pool is handed to the main thread's
  owners.
- [ ] `device/memory/batch.hpp:86-91`, `gui/guitextures.cpp:146-158` — "stamped for the next submit" is false for a
  batch open across one, which `GuiTextures` patches by hand. Target: a long-lived batch stamps at hand-over, or the
  type refuses to stay open.
- [ ] `device/retiring.hpp:58-65`, `commands.hpp:161`, `stagingring.cpp:21` — three queues with no stated limit.
  Target: each asserts its bound.
- [ ] `device/memory/image.hpp:134-136` — `Image::waitIdle` has no caller, and six hand-outs stamp `mRead` for it.
  It goes.
- [ ] `device/commands.cpp:49-53` — a watched submit that fails is logged and counted as run. Target: it is
  reported failed.
- [ ] `present/presenter.hpp:205-219` — five flags. Target: one enum and the suboptimal latch.
- [ ] `vulkanrenderer.cpp:81-100` and each owner's `bytesAt` — the frame's reserve is a second composition of what
  `createTargets` makes, held equal by three tests. Target: an owner states one list of descriptions an extent and
  mode; its constructor makes from it and the reserve sums it, as `FsrUpscaler::Targets` does.
- [ ] `trace/tracechain.cpp:89-92,152-166`, `frametracer.cpp:17-25,82-88`, `upscale/dlssupscaler.cpp:81-87` — an
  arena's order is written three times an owner and indexed by hand; only the count is asserted. Target: `declare`
  answers a typed range its owner resolves.
- [ ] `gui/guitextures.cpp:44-58`, `myguirtx/rendermanager.cpp:171-187` — every name MyGUI asks becomes an
  uncompressed essential image nothing destroys; the global map is about 126 MiB twice. Target: a counted use with
  a stated bound and a rest (a name not drawn for N frames gives its image back and is read again); the figure in
  the memory report first.
- [ ] `gui/guitextures.cpp:158-159` — a slot keeps its read-back buffer after its texture goes. Target: it goes
  with it.
- [ ] `vulkanrenderer.cpp:1005-1015`, `picturetracer.cpp:55-73` — the pictures' room is a whole denoising chain at
  the bounding box of every view, for the session. Target: measured, then let go when no view has been asked for N
  frames.
- [ ] `trace/spritebin.cpp:58`, `trace/ripplepass.cpp:126-129`, `gui/guitextures.cpp:184` — three hand-rolled
  read-backs of one word. Target: one type owning the buffer and the value it rides.
- [ ] `denoise/` and `display/` — each pass states its bindings twice in C++. Target: FSR's `Bind` table, from
  which the layout and the writes are made.
- [ ] `denoise/denoisepasses.cpp:80`, `denoise/atrouspass.cpp:116` — an optional parameter selects the behaviour
  and the shape of the result. Target: a shared prefix and two endings.
- [ ] `trace/visibilitypass.cpp:416-470` — which pipelines are shared is decided under a lock by racing hands,
  though it is known before any starts. Target: the work list is the distinct (stage, canonical tuple) pairs.
- [ ] `TraceChain`, `FsrUpscaler`, `BloomPass`, `PictureTracer`, `DenoiseHistory`, `DlssUpscaler` — `resize` means
  four things. Target: `make(extent)`, `place(run)`, `release()`.
- [ ] `trace/spritebin.cpp:47-65`, `gui/guidrawer.cpp:59`, `display/displaychain.cpp:242` — device allocations that
  can land on a frame. Target: the sprite tables sized from the scene's counts at hand-over.
- [ ] Dead: `DenoiseHistory::release()`, `Ngx::OptimalSettings::mMin/mMax`, `PresentFence`'s
  `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`. `myguirtx/texture.cpp:91-104` ignores a lock's access: a contract
  on `Write`. `sharedtexture.hpp:41`, `paintedmirror.hpp:35` hold a `ref_ptr` to a source their headers say the
  caller owns: a reference.
- [ ] `docs/architecture.md:561` describes an `auto` upscaler that falls back to FSR; the code has none. The
  document is corrected.
- [ ] Tests: the graveyard has none of its own (an off-thread burial, the buried count); a shed after an idle
  collect; `device/readstamp.cpp:69` and `slottable.cpp:413` release a held submit from a thread that sleeps;
  `present/presenter.cpp:251` asserts a wall time.

## Batch 10: the harness and the tests as a body

Files: `apps/rtxtool/`, `apps/components_tests/`, `apps/openmw_tests/mwrender/`, `cmake/Tests.cmake`.

- [ ] `apps/rtxtool/session.hpp:49` — ten responsibilities against "the sequence and nothing else". Target: the
  tour's files and log listener with the watch; the clock with staging; the `RtxRun` answers a function of one stop
  state (ten getters repeat `if (mDone || !mStarted)`).
- [ ] `apps/rtxtool/instruments/scenereport.cpp:12` includes a game-side header, unseen by `RtxSourceTreeTest`,
  which also skips `.h`. Target: `WalkReport`'s parts are core types; the test reads every header.
- [ ] `apps/rtxtool/model/benchrecord.cpp` — the record's field list exists in the struct, fifteen `asJson`
  overloads, the report and the test's literal. Target: one field table a struct; `writeJson` returns text, so ten
  test files stop writing and reading back real files.
- [ ] `apps/rtxtool/model/restreuse.cpp:59` — `inplace_merge` allocates on every tour frame. It goes with the rest
  queue; its successor reads the plan's cache tier.
- [ ] Fixtures: `sceneuploader.cpp`'s set-up repeated in 12–18 tests; thirteen local `osg::Image` builders beside
  `support/image.hpp`; seven `refusalOf` wrappers beside `support/refusal.hpp`; four private wait bounds beside
  `WaitBound` (`cardwatchdevice.cpp:118,133`, `testobjectcache.cpp:436`, `platform/thread.cpp:109`). Target: one of
  each.
- [ ] `support/device/harness.hpp` — the GPU binary's one allocator is shared by every test of a shard and three
  other processes; the memory tests assert differences from "now". With the policy in the core, they go; what stays
  on the device is a handful of end-to-end cases.
- [ ] `apps/components_tests/rtx/common/slots.cpp` holds `RtxScratchTest` and `RtxSparesTest`;
  `apps/components_tests/rtx/scene/scenedesc.cpp` is 2842 lines with the same two opening lines in 46 tests. Target:
  a file a type; a fixture.
- [ ] `tools/omw/testing.py` — the gate allows two seconds a test where the rule is one, and sets no limit a suite.
  Target: the rule's figures, or the reason beside them.

## Batch 11: the seam's defects

The seam's structure is a proposal of its own, by the user's decision, after the memory work lands. Its findings are
kept at this batch's end so that proposal starts from them.

Files: `apps/openmw/mwrender/{renderer,renderingmanager,rendersupport}`, `apps/openmw/engine.cpp`,
`apps/openmw/mwgui/{loadingscreen,windowmanagerimp}`, `apps/openmw/mwworld/{worldimp,weather}`,
`apps/openmw/mwrender/rtx/`, `components/settings/migration.cpp`, `.notes/UPSTREAM.md`.

- [ ] `renderer.hpp:570`, `engine.cpp:376-398` — the seam's base holds a `ref_ptr` to the capture operation, the
  work queue's last holder, so the work threads join after `RtxRenderer`'s members are gone. Target: a borrow, as
  its own comment says.
- [ ] `rtxrenderer.cpp:433-438,819`, `windowmanagerimp.cpp:1284,1332-1358` — a window resized under a loading
  screen: the interface is laid out for one extent and drawn into another, and the map is asked for mid-load.
  Target: window events during a load are recorded and applied at its end, in one place.
- [ ] `rtxrenderer.cpp:696-705` — one refused setting drops every changed setting and the menu is not told.
  Target: `applyChangedSettings` answers the declined keys and the settings window restores them.
- [ ] `worldimp.cpp:826,3417`, `:874,897,3411` — a rest and a door into another worldspace are each told as two
  cuts. Target: the world keeps one "cut since the last frame" and tells the renderer once.
- [ ] **(upstream, approved)** `apps/openmw/mwrender/groundcover.cpp:233,428`, `groundcovershapes.{hpp,cpp}`,
  `apps/openmw_tests/mwrender/groundcovershapes.cpp` — rasterizer-only code no started renderer calls, recorded as
  needed by the ray tracer. Target: the two hunks return to upstream's text; the files and their record go.
- [ ] `renderer.hpp:223`, `engine.cpp:234,652` — `groundReadsGates` branches upstream's `Engine` twice to spare an
  unstarted renderer. Target: the gates are built always.
- [ ] `components/settings/migration.cpp:38-45` — a shim for the fork's own older key, against the rule of no
  backward compatibility. It goes.
- [ ] Work nobody reads under the ray tracer: `loadingscreen.cpp:301-306` (stats a declined overlay would read),
  `engine.cpp:695,785` (`shaders.yaml`), `windowmanagerimp.cpp:483,1060` (`PostProcessorHud`),
  `rtx/rtxwindow.cpp:81-99` (four calls a frame once settled).
- [ ] `components/misc/presentation.cpp:101-127`, `apps/launcher/graphicspage.cpp:460` — a resolution is parsed
  back out of its caption. Target: the item carries the two numbers.
- [ ] `.notes/UPSTREAM.md`: not recorded — the loop paced by `Renderer::openFrame`; the teleport's cut in
  `World::updateWeather`; `WeatherManager` settling a `SkyState`; `LoadingScreen` without its traversals;
  `LightSource`'s copy constructor; `CacheBudget::yieldTo`'s meaning; `filestdio.cpp` out of the build. Stale:
  "`ToggleBorders` under the ray tracer" as a `support()` ask.
- [ ] Tests: none constructs `GlRenderer` and its 4,213 lines; none tears an engine down before `prepareResources`;
  `RendererTest` sleeps on the wall and writes `Settings::video()` because the base hides its clock.

For the seam's own proposal, not planned here:

- `renderer.hpp:155-592` — one class with seven responsibilities and state of its own: 22 of 55
  virtuals have work in one renderer, the base reads settings and the wall clock and sleeps. Target: the pacing
  (`awaitFrame`, `openFrame`, `openNestedFrame`, `FramesAside`, the limiter) is a concrete pacer the engine owns;
  the factory calls and world events stay.
- `engine.cpp:707-722,491-563`, `renderingmanager.cpp:134-183` — a renderer is made by ten ordered calls, each
  guarded by an assert, every state between two representable. Target: what exists before the window goes in
  `RendererSpec`; resources arrive with the one call that needs them.
- `rendersupport.hpp`, `rtx/rtxsupport.cpp` and their callers — a declined request has two sources, a virtual
  that does nothing and a table that declines it, keyed by strings. Target: a request is one call that answers its
  refusal; the chain's callers take the post-processor from one accessor that carries the reason beside the null.
- `rtxrenderer.hpp:306`, `rtxrenderer.cpp:479,540` — the world's phase rule is spelled two ways, and
  `updateTraversal` and `renderGui` take `Between` alone. Target: named expectations a caller kind.

## Batch 12: the core's remainder

Files: `components/rtx/{common,scene,renderer,mirror}`, `apps/components_tests/rtx/`.

- [ ] `components/rtx/common/rowvector.hpp:39-50`, `slots.hpp:51,135` — a none or oversize index grows a table
  where it should assert. Target: asserted.
- [ ] Every `Result<…, std::string>` and `Refusal::mWhy` — one cause has two wordings in two layers and is counted
  twice; the expectation files match text. Target: an enum of causes a kind, the text made once at the log.
- [ ] `renderer/slot.hpp` (`GuiSlot`, `SceneSlot` beside `RowIndex`), `mirror/materialresolver.hpp:426-427` (a free
  list beside `SlotRows`), `NodeKinds` and `mUpdaterClasses` (two class caches, the first's overflow read by tests
  alone), four lists of a material's maps. Target: one of each.
- [ ] Dead: `BlockedValues<T>` and `Run::in` (`common/runs.hpp:68,87-91,164-238`); test-only members in production
  headers (`getHoleCount`, `LightGrid(std::span)`, `NodeKinds::getOverflow`).
- [ ] `renderer/nullrenderer.cpp:163-176` — `renderFrame` and `holdAir` describe nothing though `FrameDescriber`
  needs no API under it. Target: the null renderer owns one.
- [ ] `scene/lightgrid.cpp:104,181,209-210` — the grid is made whole again whenever the lamp count changes, with
  three exact reserves that reallocate at each new maximum. `scene/materialtable.cpp:275` — `drop` scans every
  arrived mask a layer. `Refusals::mNamed` has no stated bound. Target: incremental, reserved once, bounded.
- [ ] `scene/sceneidentities.hpp:31` — an atomic "for the tests". Target: `OwnedBy` and a plain counter where every
  production scene is made on the main thread.
- [ ] `RendererOptions::mDlss` (`renderer.hpp:208`) is a vendor's name in the neutral interface. Target: an opaque
  option the backend names.
- [ ] Five setters each move one field of a profile `getProfile` answers whole; `rebuildSubject(…, bool settled)`,
  `traceInto(GuiSlot, bool readBack)`, `setIndirectLighting(bool)` select behaviour by a flag; eleven compound
  asserts. Target: one setter of the profile; enums; split asserts.
- [ ] Stale comments: `scene/refusal.hpp:46` (`SceneHolder::getRefusals`), `mirror/lightbuilder.hpp:36-38,163-167`,
  `mirror/surfacedescription.hpp:80-81`, `sceneuploader.hpp:175` and `sceneuploader.cpp:317-322` on the room kept
  for an arrival.

## Changed

Items that landed in another shape than their target, each with how and why.

- The backend's half of residency's contract is its own interface, `ResidencyDevice`, which `SceneHolder` takes on in
  batch 4: `Residency` and `NullDevice` need none of the rest of a backend, and the conformance suite is
  type-parameterized over it. `priceImage` answers nothing for a shape the device holds none of, which a texture's
  floor then fails on, rather than a price nothing can stand in.
- The plan is a full pass over the flat residents wherever an input moved, its scratch kept, rather than the
  incremental walk over a list a tier (`RetentionLink`): no list to keep in step with every tier change, and a pass
  is bounded by the rows. The replay program measures the plan's time; lists come back where it costs a frame.
- A raise is a `TextureArriving` that keeps what stands (`mNow`), not a fifth state `Restanding`; a structure's build
  and copy are recorded and stood in the one hand-over, so `Building` and `Copying` are not states either.
- A feed's arrival is its request whole, so no table of asks is kept and a ticket, never issued twice, is the one
  check that what arrives is for the tenant waiting on it.
- `Platform::File::readAt` answers `Result<std::size_t, std::error_code>`, not an `IoError` of the fork's: the
  standard's type is one type on both systems and keeps the system's own code, which a new enum would map and lose.
- `CellPreloader::isTerrainLoaded` reads `ResourceSystem::getExpiryDelay`, the delay the system was made with and
  last set. A budgeted manager still carries an `mExpiryDelay` it never reads: taking the field off it is batch 8's
  split of the budgeted cache from upstream's, and an upstream change this item was not approved for.
- The past lost with a chain made again is `FrameTracer::Moves::losesPast`, held by a host test of its table. The
  target's test across `setUpscale` that reads the air needs DLSS, which one vendor's cards run and CI's do not.
- `PipelineCache::save` answers nothing and retries no save: the read keeps the driver's short answer, which is a
  valid cache ("Decisions"), so there is no failed save of a growing cache left to report.
- The one-off submit's fix has no test of a wait that throws: nothing can make a device's wait fail on demand. The
  deferred buffers are retired where they are submitted, in the one function both submits go through.
- NGX's room is made by what the feature was measured at, the most any feature held where it never stood
  (`FeatureBytes`), and its test is the table's. That a larger mode on a full card no longer ends in a refusal is not
  shown: it needs a card filled past its budget under DLSS.
- State read by value went further than the item: a blend's pair and equation are unread only where the surface
  blends (`SurfaceDescription::getUnread`), and the function and the mode have a lock each, as OpenGL keeps them.
- `LargestOwed`'s ceilings stood as the item's target already, by `6047cd186b`, which landed before the plan was
  worked; the one comment that still described the design before (`MemoryUse`) was brought in line.
- A run's record is JSON lines, a line a place and the findings' counts after it, and it and a tour's findings are
  written on a writer's thread (`RtxTool::FileWriter`): a leg's end no longer writes the whole record on the
  frame's thread. **A tour is still a window a leg**, and a continuing leg's first interval is still unmeasured: a
  leg's end drains the device's queue and reads the memory reports for its place (`Measurer::finish`), which no
  frame of a player's flight does, so the interval would measure the harness. One window over a tour means routing
  the device's late answers across legs, a rewrite of the measurer every verb shares; it waits for the batch that
  gives a frame its own memory figures, which takes the leg's end off the device.
- `TourWatch` judges plain frame facts in `model/` (`WatchFrame`, `FindingPlaces`), with `WorldWatch` reading the
  game for it and tests of its hitches, slow frames, refusals and outrun cells. `checks.cpp` stays in the
  executable: `check`'s claims are asked of the world and the device by name, and a plain-facts form of them is
  the typed finding keys' item below.
- The prices are a switch of `info`, `info --prices`, not a verb: a verb is a row in every table of the harness's
  options for one question asked of a device that draws nothing, which `info` already stands up. It states each
  texture's and each first-built structure's size, the kinds of memory and the limits, on the three machines
  (`docs/research/2026-11-20-memory-prices.md`); a structure's tight size stays an item of the batch.
- The content preparation's wait reached further than its item: the store's flush after it is waited out under
  the same screen (`WorldMirror::awaitStored`, `ContentStore::Store::flushFor`). The item's draw on every report
  left the tail it named undrawn where the tail was the store's: a day's first start stamps every entry it found,
  27 s with no frame on the handheld, which left a hang report.
- A finding's identity stays its kind and its detail, with no sum type of keys: the table is indexed by both
  (`FindingTable::mIndex`), a refusal's detail names its reason, and a leg's slow frames and the soak's growth keep
  their figures in the note, so what a finding is no longer moves with its figures. A key a kind would spell each
  detail a second time, in the key and in its wording, for the identity the pair already carries. The renderer's
  refusals and the allocator's two lines beside them are logged at Info, since the refusal is a record a tour reads.
- The device's copy rate is a part of `info --prices` (`Renderer::measureCopies`), timed by the device's clock over
  BC7 chains of 64, 256, 1024 and 4096 texels.
- `HoldTier` stands in `components/rtx/common/holdtier.hpp`, not the scene's folder: `HeldRows`, which counts a row's
  holds a tier, is the common folder's, which includes no later one. What the walk's resolvers remember — a model's
  mesh, a state set's material, an image's texture — holds at `Kept`, since no placement names it; a placement holds
  at its extractor's tier, the world's `Shown` and a picture's `Forced`; the ring `Shown` until batch 7; a holder that
  reads its texture `Forced`. A material holds what it names at its own best tier, `Kept` before its first hold, and
  moves those holds as its tier moves.
- `SlotLedger::bytesFor` takes the arena it answers for, since a plan's classes stand in two where a driver gives
  images and buffers two memory types. A class's open slabs are kept in a list a reserve walks for the fullest, where
  the design had them kept ordered: a class holds a few open slabs at a time, since draining empties the emptiest.

- Batch 4's contract: `SceneHolder` takes on `ResidencyDevice`, and keeps `takeAnswer` and `getArrivalRoom` for the
  meshes alone until batch 5 moves structures onto the arena; `renderer.hpp`'s promise of a rebuild went with
  `makeTextureRoom`. The structure storages stand outside the arena and count in `U − A`, as planned.
- A bake is a `StandImage` whose texels are its source's slot, read on the device, not a `BakeImage`: one operation
  stands every image, and a backend tells a bake from an upload by the shape's source. A sprite's bake is planned as
  one with its source ("Decisions"), a composite at its source level; its layers are not held `Forced` yet, as the
  ground's chunks ask their layers `Shown` and a composite waits on what stands (`CompositeQueue`'s `LayerStanding`).
  A texture's companion stands in its slot beside it, priced with it, not in a second slot.
- The feed's types stand in the preprocess folder (`texturefeed.hpp`), which the renderer's folder includes after it,
  and the feed over the hands is `TextureHands`. A file's shape is an estimate before its chain is made
  (`shapeOfFile`: a cut BC1 is known only from its texels); a completion in another shape prices the classes again
  and plans again, so the slot reserved holds what arrived.
- `TextureTable` keeps its own rest order of the rows residency says stand (`setStanding`, from the hand-over's
  standing changes), in place of asking `Residency` for the oldest unheld row: the scene stays below the renderer's
  folder, and the order is the same.
- A raise uploads the levels it lacks and copies the rest from the slot that stands; the change of mode's covered
  frames wait for batch 7 (its item there), and `Timing::Textures` went with the uploader's texture work.
- A device's epochs are non-decreasing, not rising: a hand-over that issues nothing hands out the epoch before it, and
  `Timeline::poll` answers what finished without waiting. A settled run waits for every block asked as well as every
  texel (`ResidencyDevice::awaitBlocks`) and hands over every operation a frame (`Residency::Shares::whole`), so two
  processes stand one image on one frame and a picture of the world waits until the shown content stands.

- Batch 5's mesh rows keep their place in the rest (`RestPlace`), filed on residency's word as textures' are
  (`SceneDesc::standMesh`), and `RestQueue` stays, cut to the order alone: a lookup by key takes a resting row back,
  which only the scene's table can. Its bytes, fits and classes went with the rest limit.
- `MemoryUse` keeps its name, its uses `Frame`, `Essential` and `Content`, the last the arena's blocks: one held figure
  a use, which the report and the graveyard count by, where a separate `EssentialUse` would have needed a second
  count for the blocks. `mHeld` stays atomic, since blocks are taken on the arena's thread and essential memory from
  the compile threads.
- The harness's rest reuse model went with the meshes' rest bytes it was measured by: the question it answered, what
  a rest limit of each size kept, is the cache's share now.
- `SceneUploader` holds no state: the rest limit, the wants and the device's answer it kept all went.

## Declined

Items judged and not done, each with the reason.

- `SceneDesc::addMesh` and `MeshTable::find` answering a `MeshHold`: every caller holds the row in the statement that
  adds it, and `SceneDesc::isConsistent`, asserted after every retire, names a row nothing holds; a hold answered
  would change 141 test sites for a leak no path has.
