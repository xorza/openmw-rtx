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

Landed: the process's anonymous memory (`Platform::Process::anonymousMemory`) and the heap's figures
(`Platform::heapHeld`, glibc's `mallinfo2` found at run time, Windows' process heap), logged at every stop's end with
the run's peak in the report (`RunRecord::noteHostMemory`); the last refused content take with every content pool's
room (`RefusedTake`, `MemoryAllocator::getLastRefused`), logged when the device answers a fresh refusal, beside what
rests of each kind; the textures the device stood short (`DeviceAnswer::mShort`, `ShortTexture`), waited on and read
again before they are handed over whole (`SceneUploader::TextureWait`); the eviction sized from the arrival's
descriptions, beside the most any arrival's structures took (`SceneUploader::evictForRoom`); the eviction for what the
device waits on, and the structure storages' blocks of content's size, both in `docs/architecture.md` ("What waits is
given the resting row that surely leaves it room", "A bottom level is built loose and copied out tight").

## Steps

1. Handheld repack and PBR tours with the eviction for what waits, which meet no refusal; the figures added to
   `research/2026-11-16-handheld-content-pool-fragmentation.md`.
2. The host's mitigation the figures point at, proposed with them before it is made.
3. Tours of the three contents on the desk, the handheld and Windows; `.notes/ISSUES.md` rewritten from them.
