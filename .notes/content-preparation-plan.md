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

What a cold preparation would take after each batch, from the profile's shares and the thread counts measured on
the PBR replacers (each to be measured as it lands):

| after | Fullrest Repack | PBR replacers |
|---|---|---|
| now, as the game runs it (16 threads, unpinned) | about 350 s | 80 s |
| batch 1, exact arithmetic | about 300 s | about 73 s |
| batch 3, all CPUs but one | about 240 s | about 59 s |

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

### Batch 0: measure in place

- [ ] `WorldMirror::finishPreparing`: log the preparation's wall time, its threads' CPU time and each pass's
  `ContentStats` beside "Prepared … of …", so the game and every verb report it. Today the time is read off the
  log's stamps and the pass figures only reach a harness record, which `scene` writes no stop for.
- [ ] A check that two stores hold the same entries byte for byte (a script under `tools/` or a harness option), for
  batches 1, 2 and 3, which must change no output.

### Batch 1: byte-exact arithmetic

Files: `components/rtx/image/chainbuild.cpp`, `colour.{hpp,cpp}`, `shadingmap.{hpp,cpp}`, and the other
`quantise`s under `components/rtx/image/`.

- [ ] `quantise` as `int(double(value * 255.0f) + 0.5)` held to 0 and 255, one definition for every caller: the same
  byte as `std::lround` for every float from −3 to 3, with no libm call. `quantiseNormal` goes through it.
- [ ] `encodedByte(linear)`: `quantise(toEncoded(linear))` off 255 thresholds and a table of 4,096 starting bytes,
  made once from `toEncoded` itself, so it is that function's answer on every platform; 9× faster and the same byte
  for every float checked. The delight and the box of a display-encoded level read it.
- [ ] `ChainBuild::delight`: the light factor by row and column (`paintedLight`'s placement, wrap and floor once a
  column and once a row, its two lerps the same operations in the same order), in place of all of it at every
  texel.
- [ ] Test: `RtxPassVersionsTest` does not move; a cold store of each content list before and after is the same
  byte for byte (batch 0); a test of `encodedByte` against `quantise(toEncoded(x))` at every threshold and the floats
  on each side of it.

### Batch 2: memory

Files: `components/rtx/image/chainbuild.{hpp,cpp}`, `components/rtx/preprocess/{contentcache,contentcodec}.hpp`.

- [ ] A chain's finest level decoded, delit and encoded a band of rows at a time, as `buildNormals` already
  streams its finest level: the 4096-square scratch of 89.5 MB becomes the coarser levels' 22 MB and a band.
- [ ] A normal map's means a ring of rows on each level, as its finest level's are, in place of 12 bytes a texel
  under the finest level (67 MB at 4096).
- [ ] `ContentCache::keep` for a `ContentAsk::Stored` ask: the made chain's bytes moved into the payload and not
  copied, a chain-sized buffer and copy fewer a chain (7.4 GiB copied on the Fullrest Repack).
- [ ] Test: the stores byte for byte (batch 0); the peak resident before and after each content list's cold run.

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

## Not proposed

- **A device encoder**: refused by the architecture ("A coarser level is encoded on the host").
- **The trace dividing the painted light again**: replaced by stored delit colours on purpose (`064612bffd`).
- **The file's own BC1 block with its endpoints scaled by the light**: 233 Mtexel/s, but 42.9 dB, below rgbcx's
  level 4.
