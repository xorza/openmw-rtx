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

## Plan

Each batch is timed off the log's "Prepared … in … s, … s of CPU on … threads" and the pass lines under it. A batch
that must change no output (3) is checked with `diff -rq --exclude=prepared.manifest` between a cold
store made before it and one made after: the entries are files named by their keys, and two cold vanilla stores made
apart are the same byte for byte.

### Batch 3: threads and order

Files: `components/platform/process*`, `components/rtx/prepare/contentpreparation.{hpp,cpp}`,
`apps/openmw/mwrender/rtx/rtxrenderer.cpp`.

- [ ] The CPUs the process may run on (`Platform::Process`, its affinity mask), and the preparation on all but one:
  `hardware_concurrency()` counts every CPU the system has, so the harness, kept to 16, would run 31 threads on them.
- [ ] The ground layers first: they hold the largest maps, and the PBR replacers' last 5 s ran 2.3 threads of 16.

### Batch 4: what the manifest trusts

Files: `components/rtx/prepare/{preparationmanifest,contentpreparation}.{hpp,cpp}`,
`components/rtx/preprocess/{contentpreprocessor,contentcache}.{hpp,cpp}`.

- [ ] Each walk's store keys handed back to the preparation (what its asks found or kept), filed in the manifest
  beside its files, each key once in a table as the names are, and the format moved.
- [ ] `standsAsWalked` asks the store for each key (`ContentStore::holds`, which stamps it) after the stamps' digest
  matches; a key the store lacks is the model walked again. The issue in `.notes/ISSUES.md` goes.
- [ ] Test: a model whose chain file is deleted is walked again at the next preparation and its chain stands after
  it; one whose entries all stand is not walked.

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
