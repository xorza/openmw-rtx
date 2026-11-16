# Proposal: the handheld's memory — what the host holds, why a structure is refused, and arrivals before the cache

Three open issues met on the handheld's repack tour at `a3d689826f` (`.notes/ISSUES.md`): the process's anonymous
memory peaked at 7.9 GiB with 5.1 GiB of swap in use and the resource caches squeezed to nothing; at leg 21 a mesh
was left out and a texture drew the stand-in for want of device memory while 1.5 GiB of textures rested; and 467
arriving textures stood smaller than their files beside that rest. When this lands, its decisions fold into
`docs/architecture.md` ("One residency policy for the content", "Loading"), and this file is deleted.

## Decisions

- **The host's memory is measured before anything is changed in how it is held**: a stop's end logs what the process
  holds resident and swapped, and what glibc's allocator holds and how much of it is free, so a tour says whether the
  7.9 GiB is live data or freed arena memory. `heaptrack` is not installed and is not to be; glibc's own figures are
  the in-process view (`mallinfo2`). Refused: a mitigation picked blind — a trim a second, an arena cap, a different
  allocator (a dependency, proposed only if the figures point at the allocator).
- **A content refusal says what it asked and how the heap stood, by the user's decision**: the size, whether it was
  to have memory of its own, the memory type, the heap's budget and usage, each content pool's blocks, allocated
  bytes and largest free range, and what rests by kind. One handheld repack tour then names the cause of the mesh left
  out. As the allocator's essential refusals already name how full the card was.
- **Then structures sub-allocate in the pools where evicted textures leave room, if the report shows the suspected
  cause**: structure storage is made at most half a block (32 MiB), the size under which the allocator places a
  resource in a pool's block and not in memory of its own (`MemoryAllocator::tryAllocate`), so ranges a texture's
  eviction frees can hold it. Where the driver places structure buffers in a type textures cannot take (RADV lists a
  buffer-only type), structure storage is held to the types textures take, so the two share pools. VMA's guidance:
  sub-allocate in blocks, and memory of its own only for what blocks cannot hold. Refused: room counted by kind and
  evicted for until there, which frees much for little, since a small texture's going rarely empties a block;
  defragmentation, refused before (`research/2026-10-22-content-pool-after-a-long-walk.md`).
- **An arrival frees what it needs, and a texture stood short is handed over again at its full side once the room
  is back, by the user's decision**: the eviction ahead of a hand-over is sized from the arrival at hand — its
  textures' full sides, and its structures as before — not from the largest arrival so far; what still does not fit
  stands smaller or draws the stand-in, and the uploader hands it over again when the device's room holds its file,
  as texture streamers drop cached data first and stream the full mips in once the budget allows (Unreal's pool,
  id Tech, Frostbite). The rest limit's share (nine tenths, "The limit is derived from the budget") is unchanged: an
  arrival takes its room from the rest whatever the limit. Refused: waiting at full side, a stand-in at every crossing
  short of room; sizing from the arrival alone, under which a texture stood smaller stays so.
- **A texture stood smaller lets go of its texels and is read again for its upgrade**: through the prepared images,
  as a slot that stood in is (`mPending`), and handed over again only once read, so it keeps its smaller image
  meanwhile. Refused: keeping the texels until the room comes, an unbounded hold on the host's memory on exactly the
  machine short of it. Cost: a file read again per upgrade, from the content store where its chain is kept.

## Final shape

### What the process holds (`components/platform`)

```cpp
namespace Platform
{
    /// What the heap's allocator holds of the process's memory, where it says: what it took from the system, and what
    /// of that is in use; the rest is free memory it keeps until a trim (`trimHeap`). glibc's `mallinfo2`, found at
    /// run time, since the release builds compile against glibc 2.28, which lacks it.
    struct HeapHeld
    {
        std::uint64_t mTaken = 0;
        std::uint64_t mInUse = 0;
    };

    /// What this process holds of the system's memory, as the system counts it: its anonymous memory resident and
    /// swapped out (Linux's `RssAnon` and `VmSwap`; Windows' private bytes and what of them the pagefile holds), and
    /// the heap's figures where the allocator states them.
    struct ProcessMemory
    {
        std::uint64_t mResident = 0;
        std::uint64_t mSwapped = 0;
        std::optional<HeapHeld> mHeap;
    };

    /// Nothing where the system does not say. `memorylinux.cpp` and `memorywin32.cpp`, beside `availableMemory`.
    std::optional<ProcessMemory> processMemory();
}
```

The harness logs it at every stop's end beside the resource caches (`Session::advance`), and a tour's report says
its peak: the resident and swapped memory at the stop that held most, and the heap's free memory there.

### A refused content take (`components/rtxvulkan/device/memory`)

```cpp
/// One content pool's room, as the library counts it (`vmaCalculatePoolStatistics`): the blocks it took, what is
/// allocated in them, and the largest free range, the largest resource it places without a block of its own.
struct PoolRoom
{
    std::uint32_t mType = 0;
    VkDeviceSize mBlocks = 0;
    VkDeviceSize mAllocated = 0;
    VkDeviceSize mLargestFree = 0;
};

/// What a content take the allocator refused asked, and how the heap stood: logged once a refusal, beside the
/// refusal the scene reports.
struct RefusedTake
{
    MemoryUse mUse = MemoryUse::Texture;
    VkDeviceSize mBytes = 0;
    std::uint32_t mType = 0;
    bool mOwn = false;
    VideoBudget mVideo;
    std::array<PoolRoom, VK_MAX_MEMORY_TYPES> mPools{};
    std::uint32_t mPoolCount = 0;
};
```

The uploader's report of the refusal adds what rests of each kind (`TextureTable::getResting`,
`MeshTable::getResting`) and the buried content (`Graveyard::getBuried`).

### What the device stood short (`components/rtx/scene/deviceanswer.hpp`)

```cpp
/// A texture the device stood short of its file for want of room: the bytes it stood — nought for the stand-in, a
/// smaller side otherwise — of the file's whole.
struct ShortTexture
{
    Index mSlot = sNoIndex;
    std::uint64_t mStoodBytes = 0;
};

struct DeviceAnswer
{
    std::vector<Refusal> mRefused;
    std::vector<StoodMesh> mMeshes;

    /// Each texture stood short this hand-over, once: what `mRoomless` was, with the smaller sides beside the
    /// stand-ins, since both are handed over again once the room holds the file.
    std::vector<ShortTexture> mShort;
};
```

### What the uploader waits on (`SceneUploader`)

```cpp
/// A texture stood short, waiting for the room its whole file takes: its slot and the file it waits with; the
/// file's bytes and what it stands now, whose difference the room kept counts; how many hand-overs a stand-in has
/// waited, up to `sMostRoomWaits`, past which it is refused — a smaller side waits on, since it draws its texture;
/// and whether its texels are being read again for the hand-over (`PreparedImages::find`).
struct TextureWait
{
    Index mSlot = sNoIndex;
    osg::ref_ptr<const osg::Image> mSource;
    std::uint64_t mFileBytes = 0;
    std::uint64_t mStoodBytes = 0;
    std::uint32_t mWaited = 0;
    bool mReading = false;
};
```

`mLargestArrival` stays the structures' figure alone; the textures' share of the room kept is the arrival's own,
summed from its descriptions' whole sides once they are described and before the hand-over.

## Steps

1. `Platform::processMemory` with its Linux and Windows files, tested against a `/proc/self/status` text as
   `availableMemory` is (`linuxtext`); the harness's per-stop line and the tour's peak line.
2. `RefusedTake` logged at a content refusal, tested on a GPU device with a budget held under one block
   (`Testing::BudgetLimit`): the pool's figures exact, the largest free range shown to matter.
3. One handheld repack tour at the tip with both reports, flown through `./omw release remote`; the figures go to a
   dated research note, and the steps below are taken or refused by them.
4. If the report shows a structure refused for want of the heap's room beyond the blocks: structure storage held at
   half a block, and to the types textures take; the bottom-level store's tests over the new bound.
5. The eviction sized from the arrival's descriptions; `DeviceAnswer::mShort` from the texture array's chosen levels;
   `TextureWait` handing a smaller side over again once read; tests for each through the uploader's counting
   renderer, with exact bytes, and the texture array's GPU test standing a smaller side short.
6. The host's mitigation the figures point at, proposed with them before it is made.
7. Tours of the three contents on the desk, the handheld and Windows; `.notes/ISSUES.md` rewritten from them.
