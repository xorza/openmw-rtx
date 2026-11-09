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

What a cold preparation would take after each batch, from the profile's shares (each to be measured as it lands):

| after | Fullrest Repack | PBR replacers |
|---|---|---|
| now | 364 s | 84 s |
| batch 1, exact arithmetic | about 310 s | about 77 s |
| batch 2 at rgbcx level 8 | about 190 s | about 64 s |
| batch 2 at rgbcx level 6 | about 155 s | about 60 s |

The same code makes the chains in play (`PreparedImages`, the `chains` workers), so each batch also shortens the
time a slot draws its file before its chain lands.

## Decisions for you

- **D1, the BC1 encoder's effort** (batch 2). rgbcx's author says to use level 10 if in doubt
  (`extern/rgbcx/include/rgbcx/rgbcx.h`, line 35); levels 5 and up compete with squish, NVTT and icbc. On the bench's
  delit-like input:

  | option | BC1 speed | PSNR against level 10 | Fullrest cold |
  |---|---|---|---|
  | level 10, as now | 1× | — | about 310 s after batch 1 |
  | level 9 | 1.55× | −0.08 dB | about 230 s |
  | level 8 (my pick) | 2.14× | −0.14 dB | about 190 s |
  | level 6 | 3.21× | −0.35 dB | about 155 s |
  | level 5 | 6.16× | −0.62 dB | about 120 s |
  | icbc, Castaño's SIMD BC1 encoder | not measured | not measured | — |

  Level 10 without 3-colour blocks is 3% faster than level 9 at 0.10 dB less, so level 9 stands for it. icbc is a
  new dependency, and its SIMD width is a compile-time choice, so its output could differ between builds for other
  instruction sets; it needs your approval before a measurement. Every option but the first moves `ChainPass::sVersion`
  and `ConversionPass::sVersion`, and each store makes its chains again once.
- **D2, the BC7 search for normal maps** (batch 3). It waits for a bench like D1's over the PBR replacers' normal
  maps: `m_try_least_squares` off, and the error weights, against the angle error the "Normal maps in BC7" decision
  measured.
- **D3, how many threads prepare** (batch 5). The game takes `hardware_concurrency() / 2`, 16 on this machine of 8
  performance and 16 efficient cores. The harness keeps to the performance cores, so only a timed start of the
  game can tell what the other 15 add once the kernels are compiled.

## Plan

### Batch 0: measure in place

- [ ] `WorldMirror::finishPreparing`: log the preparation's wall time, its threads' CPU time and each pass's
  `ContentStats` beside "Prepared … of …", so the game and every verb report it. Today the time is read off the
  log's stamps and the pass figures only reach a harness record, which `scene` writes no stop for.
- [ ] A check that two stores hold the same entries byte for byte (a script under `tools/` or a harness option), for
  batches 1, 4 and 5, which must change no output.

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

### Batch 2: the BC1 effort (D1)

Files: `components/rtx/image/chainbuild.cpp`, `components/rtx/preprocess/texture/{chainpass,conversionpass}.hpp`.

- [ ] `sEncodeLevel` as D1 decides, with the bench's figures in its comment; both pass versions moved; the
  architecture's "A coarser level is encoded on the host" names the level and why.
- [ ] Test: `RtxPassVersionsTest`'s new digests; `shot --views=all --upscale=off` against the pictures before, on
  vanilla and the Fullrest Repack, and the cold preparation timed.

### Batch 3: BC7 normal maps (D2)

Files: `components/rtx/image/chainbuild.cpp` (`encodeLevelsBc7`).

- [ ] The bench over the PBR replacers' normal maps, then the parameters D2 takes, `ChainPass::sVersion` moved.

### Batch 4: memory

Files: `components/rtx/image/chainbuild.{hpp,cpp}`, `components/rtx/preprocess/{contentcache,contentcodec}.hpp`.

- [ ] A chain's finest level decoded, delit and encoded a band of rows at a time, as `buildNormals` already
  streams its finest level: the 4096-square scratch of 89.5 MB becomes the coarser levels' 22 MB and a band.
- [ ] A normal map's means a ring of rows on each level, as its finest level's are, in place of 12 bytes a texel
  under the finest level (67 MB at 4096).
- [ ] `ContentCache::keep` for a `ContentAsk::Stored` ask: the made chain's bytes moved into the payload and not
  copied, a chain-sized buffer and copy fewer a chain (7.4 GiB copied on the Fullrest Repack).
- [ ] Test: the stores byte for byte (batch 0); the peak resident before and after each content list's cold run.

### Batch 5: scheduling (D3)

Files: `components/rtx/prepare/contentpreparation.{hpp,cpp}`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp`.

- [ ] The ground layers first: they hold the largest maps, and the PBR replacers' last 5 s ran 2.3 threads of 16.
- [ ] The thread count D3 takes.

### Batch 6: what the manifest trusts

- [ ] The issue in `.notes/ISSUES.md`: a model the manifest skips is not asked whether the store still holds its
  chains. With no cap, only a refused or hand-deleted entry is lost so; a cap brought back makes it every sweep. The
  shape of the fix needs its own research (the manifest keeping each
  model's keys, or a `holds` a chain), as ccache's direct mode falls back when its result is gone.

## Not proposed

- **A device encoder**: refused by the architecture ("A coarser level is encoded on the host").
- **The trace dividing the painted light again**: replaced by stored delit colours on purpose (`064612bffd`).
- **The file's own BC1 block with its endpoints scaled by the light**: 233 Mtexel/s, but 42.9 dB, below rgbcx's
  level 4.
