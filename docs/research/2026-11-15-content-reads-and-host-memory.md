# What the content's loaders read twice, and what the host holds

Measured 2026-11-15 on a high-end laptop CPU beside a high-end NVIDIA laptop GPU, release build at `5f220805f5`,
the PBR replacers' profile: `tour`, cut by a signal after 70 to 120 s, so each run covers the tour's first legs
(Seyda Neen's region and its interiors). No run flew the whole tour.

## Host memory

The process's anonymous memory (`RssAnon`), sampled every 2 s:

| run | 10 s | 30 s | 60 s | 90 s |
|---|---|---|---|---|
| as it stands | 9.1 GiB | 10.9 GiB | 12.0 GiB | 12.0 GiB |
| one malloc arena (`MALLOC_ARENA_MAX=1`) | 0.7 GiB | 0.9 GiB | 9.4 GiB | 11.0 GiB |
| malloc's huge pages off (`glibc.malloc.hugetlb=0`) | 9.1 GiB | 10.8 GiB | 12.0 GiB | 12.0 GiB |
| the game's cell preloading off (`[Cells] preload enabled = false`) | 6.9 GiB | 8.6 GiB | 9.7 GiB | 10.2 GiB |

One arena loads slower and ends as high: what is held is live, not freed memory the allocator keeps.

`heaptrack` over 90 s (with `GLIBC_TUNABLES` set ahead, so the harness does not start itself again under huge
pages, which a preloaded tracer does not follow): the peak's largest holders are

| held at the peak | from |
|---|---|
| 8.56 GiB | `ReadDDSFile`, 9,692 calls: decoded textures |
| 1.29 GiB | `ContentStore::Store::readFile`, through `PreparedImages`: chains read from the store |
| 0.39 GiB | the NVIDIA driver |
| 0.16 GiB | `ContentStore::Reach`'s resident entries |

and the 8.56 GiB by the path that loaded it: 4.23 GiB the game's cell preloader on its work queue
(`SceneManager::getTemplate`, `attachAutoMaps` adding the replacers' normal and specular maps), 2.30 GiB the ring's
reader (`CellReader::readStatic` → `getTemplate`), 0.89 GiB the ray tracer's own reads (`readTexture` →
`openImage`), 0.54 GiB the game's cell insertion on the main thread.

## Reads

`strace -f -ttt -e trace=openat,lseek,close` over 75 s; a file read is a stream opened on it: a loose file's
`openat`, an archive's entry the first `lseek(SEEK_SET)` after the archive is opened (`openConstrainedFileStream`
opens the archive for every entry). The archive's index maps an offset to its entry.

- 52,670 streams opened on 14,424 files; 38,246 (73%) on a file a stream had been opened on before in the run.
- The gap from a file's previous read: 30,700 between 1 and 15 s, 5,810 past 15 s, 2,500 within 10 ms, 50 between
  10 ms and 1 s.
- 2,496 of the reads within 10 ms were by another thread than the read before: two threads that missed the
  same cache entry at once, each loading it.
- By thread, a repeat named by the thread before it and its own: 33,926 the preloader's after the preloader's,
  1,271 the preloader's after the ring's reader's, 1,184 the main thread's after the preloader's, 1,021 the main
  thread's after its own, 721 the preloader's after the main thread's, 123 the reader's after either.
- **The preloader ran on 16 threads holding up to 50 cells**: the harness scales the game's preloader to a
  flight's speed (`apps/rtxtool/framed.cpp`, `CellPreload`: the shipped thread count times the speed over a run's,
  up to the ring's 16 cells or the processor less one, and the cache up to two grids with their rings), against
  the shipped 1 thread and 20 cells. Each of the 16 threads seeked into an archive 14,000 to 22,000 times, from 8
  to 140 s.
- By archive and kind, in a run of 22,701 reads: `Morrowind.bsa` 17,017 reads of 5,142 entries (12,587 repeats),
  nearly all `.nif` (16,544 reads of 3,966 models, four reads a model in 75 s); the replacers' loose `.dds` 4,466
  reads, 26 repeats.
- The most-read entries are the interiors' and the furniture's models (`in_velothismall_*`, `furn_de_*`), 15 to 19
  reads each, by the preloader's thread and the main thread.

A stack for every archive read (`strace -k`, resolved with `addr2line`): 38% `NifFileManager::get` ←
`SceneManager::getTemplate` ← `getInstance` on the main thread (cell insertion), 21% the same under the cell
preloader, 13% under `Rtx::CellReader::readModel`, 3% `ImageManager::getImage` under MyGUI, 2% `KeyframeManager`.

## Upstream's caches as they stand

`Resource::GenericObjectCache` keeps an entry while anything outside it holds the value, and drops it `cache
expiry delay` (5 s) after the last such hold (`objectcache.hpp`, `update`). A load takes no lock between the miss
and the insert, so two threads that miss together both load, and `addEntryToObjectCache` keeps the later; the
earlier caller's copy lives on in its own scene graph. The cell preloader keeps at most `preload cell cache max`
(20) cells, by count.
