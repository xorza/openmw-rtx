# Content preparation: attack plan

The measurements are in `docs/rtx/research/2026-11-09-content-preparation-cost.md`.

## Where it stands

- A cold preparation takes 5.1 s on vanilla, 84 s on the PBR replacers and 364 s on the Fullrest Repack, with 16
  threads busy from start to end. A warm one, which the manifest answers, ends under 0.9 s after the content load.
- **The bound is the CPU inside the chain pass**: 98% of the threads' time. The disk, the keys, the store, the
  manifest, the model walk and the loaders together are under 3%.
- **Root cause 1, BC1 at rgbcx level 10**: 62% of the Fullrest Repack's time, 30% of the PBR replacers'. Every
  painted colour (a diffuse with no `_spec` map) has every level divided and encoded again in BC1: 14,190 chains,
  6.8 GiB, on the Fullrest Repack.
- **Root cause 2, per-texel arithmetic through libm**: `quantise` calls `lroundf`, the delight and the box call
  `powf` through `toEncoded`, and `paintedLight` places, wraps and floors every texel again. 15% of the Fullrest
  Repack's time, 8% of the PBR replacers'. The same results can come from inline arithmetic and tables, byte for
  byte.
- **Root cause 3, BC7 for normal maps**: 33% of the PBR replacers' time, already narrowed to one mode a block.
- **Memory**: a thread keeps the scratch of the largest file it met, about 200 MB at a 4096-square file, so 16
  threads hold 3 to 4 GB at the peak (4.7 GiB and 5.4 GiB resident).

Cold preparations under the harness, held to the 16 CPUs of the performance cores, and what batch 3 would make of
them from the thread counts measured on the PBR replacers:

| after | vanilla | Fullrest Repack | PBR replacers |
|---|---|---|---|
| the start | 5.7 s | 364 s | 84 s |
| batch 1, exact arithmetic (measured, the stores the same byte for byte) | 4.5 s | 319 s | 80 s |
| batch 3, all CPUs but one, as the game runs it | — | about 245 s | about 61 s |

The same code makes the chains in play (`PreparedImages`, the `chains` workers), so each batch also shortens the
time a slot draws its file before its chain lands.

## Decisions

- **BC1 stays at rgbcx level 10**, by the user's decision: good looks rank above speed. rgbcx's author names level 10
  for when in doubt (`extern/rgbcx/include/rgbcx/rgbcx.h`, line 35). On the bench's delit-like input, level 9 was
  1.55× faster at −0.08 dB, level 8 2.14× at −0.14 dB, level 6 3.21× at −0.35 dB. Not measured: icbc, the BC1
  encoder NVTT uses ([icbc](https://github.com/castano/icbc)), a new dependency.
- **The BC7 search for normal maps stays one mode a block, least squares on**, by the user's decision. On 120 of the
  PBR replacers' normal maps: least squares off was 1.31× faster at a mean normal error of 0.511° against 0.442°;
  uber level 1 1.66× slower at 0.418°; mode 6 for every block 0.362° but a height RMSE of 1.71 against 0.49.
- **All logical CPUs the process may run on, but one, prepare**, by the user's decision. Unity's job system starts
  workers up to the logical core count
  ([Unity](https://docs.unity3d.com/ScriptReference/Unity.Jobs.LowLevel.Unsafe.JobsUtility.JobWorkerCount.html)), and
  on the loading screen the main thread only draws and sleeps. Cold PBR preparation, unpinned: 16 threads 79.7 s, 24
  67.0 s, 31 64.0 s; peak resident 5.6 to 6.1 GiB.
- **A model stands only where the store holds every entry its walk made**, by the user's decision: the manifest keeps
  each model's keys, each key once, and asks the store for each. The Remote Execution API's action cache holds the
  same rule, a result returned only where the blobs it references stand
  ([REAPI](https://github.com/bazelbuild/remote-apis/blob/main/build/bazel/remote/execution/v2/remote_execution.proto)),
  as bazel-remote checks them on each get ([bazel-remote](https://github.com/buchgr/bazel-remote)).

Memory, by the user's decisions, each against the field's practice:

- **A dispatcher, not a semaphore**: a free thread is handed the work that fits the budget, and waits only where
  nothing does. Ray holds a task until the sum of the running tasks' stated memory leaves room for it
  ([Ray](https://docs.ray.io/en/latest/ray-core/scheduling/index.html)); a semaphore blocks on its head.
- **The unit is a walk and a chain**: a walk loads a model, claims its maps and queues one chain job for each, holding
  its image; the chain, 98% of the CPU, is the job sized and ordered. Refused: a model as the job, whose maps are known
  only once its template loads, so a cold run, the one that needs the bound, would dispatch on a guess. Unreal's
  derived data gives each build its own peak, excluding its inputs
  ([`FBuildContext::GetRequiredMemory`](https://dev.epicgames.com/documentation/unreal-engine/API/Developer/DerivedDataCache/FBuildContext/GetRequiredMemory)).
- **A thread's memory is measured, its growth predicted**: each thread reports the capacity it keeps after a job, and
  a chain's peak is computed from its image's extent, format and encoding; the dispatcher counts the kept capacity
  and the growth past it. A debug assert holds the capacity after a job to the prediction. Refused: a prediction
  alone, which a buffer added later escapes; the process's resident size alone (Dask's pause at 80%,
  [Dask](https://distributed.dask.org/en/stable/worker-memory.html)), which lags and stays high on what glibc keeps.
- **Scratch is routed, and released on need**: a large chain goes to a thread that holds large scratch already, and a
  thread lets its scratch go only where a waiting chain fits through it. glibc maps each block past its mmap threshold
  (32 MiB at most on 64-bit) and unmaps it on free ([mallopt(3)](https://man.archlinux.org/man/mallopt.3.raw)), so
  the release reaches the system. Refused: a release after every large chain, against the persistent buffers; and
  never, which fills the budget with each thread's largest file.
- **Largest first, with a reservation for the head** (EASY backfill, Lifka 1995,
  [backfilling](https://dr-evt.readthedocs.io/en/latest/BACKFILLING_ALGORITHMS.html)): a chain that does not fit is
  reserved the time the running chains free its room, and a smaller one starts before it only where it ends by then,
  its time estimated as its texels at its encoding's rate. An estimate orders, it never admits. Refused: greedy,
  which leaves the large chains to a tail of their own; strict, which idles the threads as a semaphore would.
- **The budget is read once, at the start**: the least of `MemAvailable`, a cgroup v2 `memory.max` less
  `memory.current`, and on Windows the process's commit headroom (`ullAvailPageFile`) and `ullAvailPhys`, less a
  reserve measured as the game's own peak on the loading screen, the kernels' compile included. A platform that
  cannot say (macOS) dispatches by threads alone, as its API states. Bazel reads its budget once
  ([Bazel](https://jmmv.dev/2019/12/bazel-local-resources.html)). Refused: a live read, which counts the running jobs
  twice and stays low on what glibc keeps; a fixed fraction (Bazel's 0.67), a guess at both ends of the RAM range;
  a pressure signal beside it, one more source a platform and runs less repeatable.
- **A source image counts from its walk to its chain's end**: a walk is handed out where the budget holds the largest
  model's images met so far and fewer chains are queued than the threads can take, its real size replacing the
  allowance once loaded. The overshoot is at most one model's images a thread. Refused: a queue bounded by count,
  whose bytes are not bounded; every walk first, which held 14.7 GB.
- **A chain past the whole budget runs alone, and is logged**, the others' scratch released first, as Bazel admits an
  action with nothing running and Unreal runs one "anyway"
  ([Unreal](https://forums.unrealengine.com/t/beware-assetcompile-memory-estimate-is-greater-than-available/1918709)).
  Refused: refusing it, which the play's chain workers would build anyway, or leave drawn as its file.
- **The loader's cache lets go of a path when its last job ends**, through a per-path removal on the resource
  managers (`ObjectCache::removeFromObjectCache`), a hook for the seam; the clear once a second goes. Refused: the
  clear kept and its residue measured into the reserve, a periodic sweep a content list can outgrow.

## Plan

Each batch is timed off the log's "Prepared … in … s, … s of CPU on … threads" and the pass lines under it. A batch
that must change no output is checked with `diff -rq --exclude=prepared.manifest` between a cold
store made before it and one made after: the entries are files named by their keys, and two cold vanilla stores made
apart are the same byte for byte.

### Batch 5: what a crash report names

Files: `components/rtx/preprocess/contentpreprocessor.cpp`, `components/rtx/mirror/cells/templatewalk.cpp`.

A preparation's crash report names the model or the ground layer (`preparing the model "…"`), and a texture only
while it is described (`describing the texture "…"`, whose scope ends before the chain is built). The chain's
build and encode, 98% of the preparation's time, run under the model's note alone, and the play's chain workers
(`PreparedImages::serve`) under none.

- [ ] `ContentPreprocessor::chain` and `convert`: a `Crash::NoteScope` naming the file, its encoding and wrap, and
  for a conversion its partner: `preparing the model "…" > making the chain of "…" as painted`. In the preprocessor,
  so the preparation, the reader and the chain workers each name what they make. A note allocates nothing, and a
  chain takes milliseconds, so the cost does not show.
- [ ] `TemplateWalk::take`: the drawable's name under the model's note, as `MeshResolver` names the mesh it adds, so a
  fault in a shape's fold or split names the part.
- [ ] Test: how the crash matrix lands a fault under a note decides it; the report of a fault inside a chain's
  build names the model, the file and the encoding.

### Batch 6: a dispatcher bounded in memory

Files: `components/rtx/prepare/contentpreparation.{hpp,cpp}`, a dispatcher beside it,
`components/rtx/preprocess/{contentpreprocessor,texture/chainpass,texture/conversionpass}.{hpp,cpp}`,
`components/rtx/image/chainbuild.{hpp,cpp}`, `components/platform/process*`, `components/resource/` (the per-path
removal), `apps/openmw/mwrender/rtx/rtxrenderer.cpp`. The decisions are above, under "Memory".

- [ ] Measure first: the game's peak on the loading screen with no preparation, the kernels' compile included, on
  each content list, into the research notes: the reserve.
- [ ] `Platform::Process` (or a memory module beside it): the budget's inputs on Linux and Windows, none on macOS.
- [ ] The peak a chain or a conversion would reach, from its image alone; the capacity a preprocessor keeps; the
  scratch let go of on the dispatcher's ask.
- [ ] Walk and chain jobs; the dispatcher's order, reservation, routing and admission of walks; the oversize chain
  alone and logged.
- [ ] The cache's per-path removal at a job's end, and the clear once a second gone.
- [ ] Tests: each dispatch rule on a synthetic queue with exact budgets (what starts, what waits, what is
  backfilled); the predicted peak against the measured capacity for each encoding and format; a cold PBR run's
  resident peak under a budget set below today's, against the budget.
- [ ] Measured: the three content lists cold, the time and the resident peak, before and after.

## Declined

- **A chain's finest level streamed a band at a time** (89.5 MB of scratch a thread at a 4096-square file): the
  field's compressors hold a whole level (NVTT, DirectXTex, Compressonator), and the streaming build would be a
  second route beside the whole one the conversion and the tests read, for a scratch bounded by the largest file.
- **A stored ask's chain moved into its payload and not copied**: the form's byte appended after the move
  reallocated a buffer reserved to the chain's size, a copy again, and the copy it saves is under 0.1% of the
  Fullrest Repack's CPU.

## Not proposed

- **A device encoder**: refused by the architecture ("A coarser level is encoded on the host").
- **The trace dividing the painted light again**: replaced by stored delit colours on purpose (`064612bffd`).
- **The file's own BC1 block with its endpoints scaled by the light**: 233 Mtexel/s, but 42.9 dB, below rgbcx's
  level 4.
