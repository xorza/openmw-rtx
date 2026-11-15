# Proposal: one load per file, and content held by reference and by budget

Temporary: when this lands, each lasting decision folds into `docs/architecture.md` ("The content store" and a new
"Loading" part), the approved upstream changes into `.notes/UPSTREAM.md`, and this file is deleted. The facts it
stands on are `docs/research/2026-11-15-content-reads-and-host-memory.md`.

## The problem

Under the PBR replacers a 75 s tour opened 52,670 streams on 14,424 files: 73% re-read a file the run had read
before, four reads a model. Most repeats are the game's cell preloader reading a model again one to fifteen seconds
after the cache let it go; 2,500 are two threads loading the same entry within 10 ms of each other. The host holds
12 GiB by the first minute, 8.56 GiB of it decoded textures, and on a 16 GiB handheld that is the memory the GPU
needs too: `earlyoom` ends the game at leg 5 of 93.

Four causes, each in `components/resource` or beside it:

1. **Residency by time**: an entry nothing else holds goes 5 s after the last hold, whatever it cost to load and
   however soon the next cell asks for it again.
2. **No coalescing of loads**: two threads that miss the same key both load it; the cache keeps one copy and the
   other lives on in its caller's graph.
3. **Three loaders admitted apart**: the game's preloader, the game's synchronous cell insertion and the ring's
   reader share the cache and nothing else. A flight makes it worse: the harness runs the preloader on up to 16
   threads holding up to 50 cells (`apps/rtxtool/framed.cpp`, `CellPreload`), each re-reading what another let go.
4. **No byte budget**: the preloader is bounded by a count of cells and the cache by a time, so neither knows what a
   PBR texture costs.

## Decisions

- **One load per key in flight** (`GenericResourceManager::getOrLoad`): the first thread to miss a key loads it; any
  other thread that asks for that key before the load ends waits on the same result, and the result enters the cache
  once. A failed load answers every waiter with its error and leaves no entry. This is
  `golang.org/x/sync/singleflight`'s duplicate call suppression
  ([docs](https://pkg.go.dev/golang.org/x/sync/singleflight)), Bevy's `AssetServer` returning one handle for a path
  while it loads ([docs](https://docs.rs/bevy_asset/latest/bevy_asset/)), and Godot's `ResourceLoader` reusing what
  a load of the same path made (`CACHE_MODE_REUSE`,
  [docs](https://docs.godotengine.org/en/stable/classes/class_resourceloader.html)). The load runs on the thread that
  missed, never queued, so a waiting worker cannot starve the queue its load would sit in; and loads nest by kind
  alone (a template asks for its NIF file, images and keyframes, a shape for its NIF file), never for the key being
  loaded, so no thread waits on itself. The frame's thread that asks for a model a preloader is loading waits for
  the rest of that load, as Unreal's `FlushAsyncLoading` waits on a package already in flight. Refused: a lock over
  the whole load, which serialises every thread behind the slowest file.
- **Held while held, then kept by a byte budget, least recently used first**: an entry anything outside the cache
  holds stays, as now; one nothing holds stays in a least-recently-used list until the cache's bytes pass its budget,
  then the oldest go first. The 5 s timer goes. Bevy keeps an asset while a strong handle stands; a budget over the
  rest is what a browser's memory cache and Unity's Addressables cache do. Refused: a longer `cache expiry delay`,
  which keeps the handheld's memory as unbounded as now and still re-reads after a long flight.
- **An entry states its own bytes, and shared bytes live in one cache**: an image its levels'
  (`osg::Image::getTotalSizeInBytesIncludingMipmaps`), exact; a template its arrays and none of its images, which
  the image cache counts; a NIF file and a shape what they parse into. A byte is then counted once however many
  templates share it.
- **The budget is derived, by the user's decision**: the memory the system reports available at startup
  (`Platform::availableMemory`) less 1 GiB, as the content preparation's is, and where the GPU shares the system's
  memory (the device reports itself integrated) less the device heap's budget too (`VK_EXT_memory_budget`), which
  the GPU's allocations take out of the same RAM. No setting. Refused: a setting in MiB, as Unreal's
  `r.Streaming.PoolSize`
  ([docs](https://dev.epicgames.com/documentation/unreal-engine/texture-streaming-configuration-in-unreal-engine)),
  whose default is wrong on some machine, which is the handheld's failure today; a derived figure with an override,
  two routes to one number.
- **One budget and one least-recently-used order over every cache, by the user's decision**: the unreferenced
  entries of the template, NIF file, image, shape and keyframe caches stand in one order under the one figure, so
  whatever was used longest ago goes first whatever its kind, and the share each kind takes follows the content
  (PBR's is images, the Repack's meshes). Refused: a fixed share a cache, as Unreal splits its pools, which is wrong
  for some content and leaves one cache evicting beside another's idle bytes.
- **Eviction is incremental and off the frame's lock**: the thread that inserts past the budget evicts as many bytes
  as it added, and the values go outside the lock, as upstream's `update` already lets them go. No sweep, so no frame
  absorbs a whole cache's eviction.
- **The loaders draw on one budget**: the preloader's admission is bounded by bytes, not by `preload cell cache max`,
  and the ring's reader's held models count against the same figure (`Rtx::ReaderMemory` already reports them). The
  frame's own insertion and the ring's cells inside its band are needed, never refused: they evict what is unused,
  and pass the budget, logged, only where everything is in use. What the reader keeps past adoption (its spare and
  filed models) is cache like any other.
- **The timer goes for what is read from files alone, by the user's decision**: templates, NIF files, images, shapes
  and keyframes are held by the budget; the caches of what the game builds from memory (the land manager,
  groundcover and object paging chunks, the ray tracer's chunk cache, the physics shape instances) keep `cache
  expiry delay` as they are, and upstream's setting and script command stay. A different job, dropping what is
  cheap to build again, not a second route to the same result. Refused: the budget everywhere, the largest upstream
  diff and a budget that prices built chunks too; the timer kept as an idle cap, which brings back the five-second
  re-reads this change stops.
- **A preload that does not fit is skipped, by the user's decision**: dropped for now and counted, and asked again
  on a later frame as the preloader already asks; the cell loads on demand if the player arrives first. A preload is
  a guess, as a browser's prefetch is, which it drops under memory pressure, and as Unreal's streamer streams fewer
  levels when its pool is full rather than blocking. Refused: waiting for room, which stalls the preloader's queue
  behind one cell with everything in use; loading anyway, which is today's unbounded memory.
- **An archive is opened once and read by position, by the user's decision**: a BSA or BA2 holds one handle from its
  load, and an entry's stream reads its bytes by offset (`Platform::File::readAt`: POSIX `pread`, Windows `ReadFile`
  with the offset in an `OVERLAPPED`), keeping its own position, so no thread shares a file position and no read
  opens or seeks. A stream holds the handle by shared ownership and outlives its archive safely. Loose files stay as
  they are. Today every entry opens the archive again: 18,082 opens of `Morrowind.bsa` in 75 s. Refused: a memory
  map, whose file-backed pages count against what the handheld reports used; leaving it to measure after the cache.
- **The images the ray tracer has uploaded are a separate change** (the scene graph holds every texture's decoded
  pixels because no OpenGL `apply` ever drops them): it needs facts kept by path rather than by image address and a
  point where no thread still reads the pixels, and is proposed after this lands and is measured.

## Implementation plan

Each step lands on its own with its tests, and is measured against the step before with the same runs: `strace`'s
repeat count over 75 s, `heaptrack`'s peak over 90 s, and `bench --suite=streaming`'s frame p99 and worst frame on
the Repack, on the desk and the handheld.

1. **Counters first**, as fields of upstream's own `Resource::CacheStats` (`Count`, `Get`, `Hit`, `Expired` today,
   reported to the profiler by `Resource::reportStats`): loads, coalesced waits, loads of a key the cache had let go
   before, and bytes. `bench` and `tour` report them beside the frame figures. Tests: a load, a hit, a coalesced wait
   and a reload after a let-go each move their own field by one. This replaces `strace` as the measure for the steps
   after.
2. **`getOrLoad` in `GenericObjectCache` and `GenericResourceManager`**, used by `SceneManager::getTemplate`,
   `NifFileManager::get`, `ImageManager::getImage`, `KeyframeManager::get` and `BulletShapeManager`. Tests: eight
   threads asking one key call the loader once and get one object; a loader that throws answers every waiter and
   leaves no entry; two keys load in parallel. Expected: the 2,500 near-simultaneous repeats gone.
3. **Archives read by position**: `Platform::File::readAt` on both systems, the archives' one handle, and
   `ConstrainedFileStreamBuf` reading by offset from it. Tests: eight threads reading different entries of one
   archive at once each get their entry's exact bytes; a seek and a read past the entry's end answer as a file's
   end does; a stream read after its archive is gone still reads. Expected: one open an archive in a run.
4. **Bytes per entry**, stated by each manager at insertion and summed by the cache. Tests: an image's bytes are its
   levels'; a template sharing an image with another counts it once.
5. **The budgeted LRU in place of the timer**, the budget derived at startup. Tests: past the budget the least
   recently used unreferenced entry goes first; a referenced entry never goes; a hit moves an entry to the front.
   Expected: most of the 30,700 one-to-fifteen-second repeats gone, and the peak bounded by the budget.
6. **One budget for the preloader and the ring's reader**: the preloader admits a cell when its estimated bytes fit,
   and the reader's held models count; the harness's flight scaling (`CellPreload`) keeps its threads and drops its
   cell count for the budget. Tests: admission refused past the budget and granted once room frees; the frame's
   insertion is admitted regardless.
7. **The verdict runs**: the three content profiles' `tour` on the desk, the handheld and the Windows host
   (`./omw release tour`), with no `earlyoom` kill on the handheld, and `shot --views=all` unchanged.

## What it costs upstream

Steps 1 to 6 change `components/platform`, `components/files`, `components/bsa`, `components/resource`
(`objectcache.hpp`, `resourcemanager.hpp`, the managers) and the cell preloader in `apps/openmw/mwworld`: upstream
code, each needing the user's yes and an entry in `.notes/UPSTREAM.md`. No setting changes: the budget is derived
and `cache expiry delay` stays for the built caches.

**Kept in the fork, by the user's decision**: not offered upstream, so every upstream merge that touches
`components/resource`, `components/bsa`, `components/files` or the cell preloader resolves against it, which the
merge's resolver and review read in `.notes/UPSTREAM.md`. Refused: offering steps 2 to 5 as a merge request after
they are measured; writing them against upstream first, which waits the handheld's memory on a review.
