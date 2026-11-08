# Proposal: an on-disk content cache, and every chain built from its finest level

Status: proposed; the dependency and the cap approved by the user. When it lands, its lasting decisions go to `docs/rtx/architecture.md` ("`Rtx::ContentPreprocessor`"
and the texture bullets), and this file is deleted. The measurements are in
`docs/rtx/research/2026-11-08-normal-map-chains.md`.

## The problem

- **MVR PBR's normal maps go black at a distance.** In 381 of the profile's 1188 `_nh` maps, the coarser levels hold
  RGB (32, 36, 0) wherever the height is 0. The mips were made with premultiplied alpha. Level 0 is correct, so a
  surface is correct near the camera and has black patches further away. The renderer reads every file's chain as the
  file holds it.
- **A file with no chain gets one built on the device at every load** (`TextureData::wantsCompletedChain`). A chain
  built this way is always full size, so the budget's side search cannot make it smaller. A chain of 512 or less on a
  side stays loose, at four bytes a texel. The Fullrest Repack loads 5398 such files.
- **Every pass of the preprocessor runs again at every start.** The shape fold, the alpha walks and the texel means are
  functions of their input alone, and `ContentCache` holds nothing (`sHolds = false`), as a staged design.

## The established answer

An asset pipeline builds each chain once, from the finest level, with a filter that suits what the texels are. It
filters an alpha that is not coverage apart from the colour (texconv `-sepalpha`, NVTT). It keeps the result in a
derived-data cache: a store of disposable outputs, filed under a key made of the inputs and the deriving code's
version, and evicted by last use. Examples are Unreal's Derived Data Cache, whose content is disposable and whose
local store deletes an entry unused for a set number of days
([DDC](https://dev.epicgames.com/documentation/unreal-engine/using-derived-data-cache-in-unreal-engine)), and ccache,
which keeps files in hashed subdirectories and removes the least recently used past a size cap
([ccache](https://man.archlinux.org/man/ccache.1)). The runtime then makes a texture smaller for its budget by
dropping levels from the top of a full chain
([Unity's mipmap streaming](https://docs.unity3d.com/2023.2/Documentation/Manual/TextureStreaming)). This
renderer has no import step, because it reads mod files as they are. So the preprocessor is the import step, and its
cache is the derived-data store.

## Decisions

- **The key is the content, as `ContentKey` already makes it**: the pass's name and version and every byte the pass
  reads, as one 128-bit MurmurHash3. The bytes are in memory already, because upstream's resource system loads every
  file the scene graph names. Refused: a key made of the path, size and modification time, as ccache's direct mode and
  Godot's `.import` use. A file inside a BSA has no time of its own, a later data folder shadows an earlier file under
  the same path, and a stale hit would be quietly wrong. What hashing every input costs a session is measured first
  (batch 0).
- **One file per entry, in 256 folders named by the key's first byte**, as ccache, Bazel's disk cache
  (`cas/54/546197…`, [EngFlow](https://blog.engflow.com/2024/05/13/the-many-caches-of-bazel/)) and Unreal's
  file-system DDC keep them. A fact of a few bytes (`AlphaRange`, `MeanTexel`) is a file of its own like a chain of
  megabytes, so one route reads and writes every entry, and a lookup is one open of a name the key spells. Refused:
  SQLite, by the user's decision; and one pack file per pass written through a rename, whose read, merge and write
  between two processes loses the other's entries unless we write a lock.
- **Every entry is untrusted data.** A file starts with its own header: a magic, the store's format version, the pass
  and its version, the key, and the payload's length and digest. A file that does not match is a miss and is deleted,
  never an assert, as `PipelineCache::read` refuses a blob. A file is written to a temporary name in its own folder and
  renamed into place, so a reader sees a whole entry or none, and two processes writing one key write the same bytes.
- **The store's bound is a size cap with least recently used eviction, by each file's modification time**, as ccache
  and the bazel-remote cache evict ([ccache](https://man.archlinux.org/man/ccache.1),
  [bazel-remote](https://github.com/buchgr/bazel-remote)). A hit is stamped in memory, and each entry used in the
  session has its time set once, at a checkpoint and at the end of the session. A sweep at start, on a worker, lists
  the folders, sorts the entries by time and removes the oldest past the cap, and removes a temporary file older than
  a day (one a killed process left). Cap: 16 GiB, by the user's decision, against `openmw-pbr-all`'s 1.71 GiB of
  chains. Refused: the time set at every hit (ccache's access time), a write on every read; and an index file of
  sizes and times, a second record of what the folders already state.
- **Only the levels the file has wrong are stored, in the file's own format.** For a normal map, levels 1 and down, built
  from the file's level 0, which stays byte-exact. For a file with no chain, every level below its one. The upload
  then joins level 0 from the file and the rest from the store. The result is an ordinary file with a full chain, so
  the side search, the upload of a chain and the residency work as they work now. Refused: the whole chain re-encoded
  to BC7, which doubles a DXT1 map (2762 of the PBR install's normal maps) and moves level 0 by BC7's low bit; and the
  chain kept on the device only, which no later start reuses.
- **Which chains are built**: every normal map's (`TextureEncoding::Normal`), by the user's decision, and every
  chain a file lacks. A colour file's own chain stays as the file holds it, as Godot loads a DDS file's levels
  directly ([Godot](https://docs.godotengine.org/en/4.5/tutorials/assets_pipeline/importing_images.html)), so no
  vanilla picture moves, unless batch 0's scan of the colour chains finds faulty ones (item 3).
- **The filter is `mipchain.comp`'s, moved to the host**: a box of the level above, with three taps along an odd axis
  (`shared/halving.h`), in linear light for an sRGB colour. Where the alpha is coverage, the colour is weighed by it
  and divided back, so a hole's colour does not bleed. Where it is not, as a normal map's height is, the colour is a
  plain box and the alpha has no part in it: the MVR fault was the colour weighed by a height and never divided back.
  A normal is averaged as a vector, as Unreal's default 2x2 `SimpleAverage` does
  ([Unreal](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/TextureMipGenSettings)). The
  spread pass measures the roughness a level loses as the shortening of that vector mean, from the finest level
  (`NormalSpreadPass`, Toksvig), so the chain's mean direction is the one the widened lobe was measured around.
  Refused: NVTT 3's slope-space filter
  ([NVIDIA](https://developer.nvidia.com/blog/Texture-Tools-Exporter-2020-1)), which suits a height-derived map better
  but puts each level's direction in another space than the spread measured around it. Moving both together is a
  separate change. One filter on the host and none on the device, so a chainless file's levels stay what they are now,
  apart from the encoder.
- **The coarser levels are encoded on the host, in the file's format**: BC1, BC2, BC3, BC4 and BC5, the only formats
  the three installs hold for these files. Dependency, approved by the user: `rgbcx.h` from bc7enc_rdo (Rich Geldreich,
  MIT or public domain; the licence is read from the repository before it is vendored), one header with BC1 to BC5 encoders and decoders, whose BC1 matches Compressonator and libsquish in quality at
  a higher speed ([bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo_devel/)). BC2's explicit alpha is our own
  code. Refused: an encoder of our own, a quality problem that a maintained one already solves; and new BC1 and BC3
  encoders on the device with a read-back, which adds two compute encoders and a transfer to the host to fill a disk
  file.
- **A chainless file of 512 or less on a side keeps its loose chain**, stored loose, so no vanilla picture moves. Its
  level 0 is the file's decoded, as now. The proof is vanilla's shots against the base, which are the same.
- **A chain is built on a worker, never on the frame or the reader.** A 2048-square normal map's levels take tens of
  milliseconds to build and encode. A cold start of the PBR profile builds about 1500 of them. A build on the cell
  reader would hold up each cell by seconds, and a build on the frame would be a spike. Workers drain a bounded queue.
  A slot draws the file as it stands until its built chain lands, and then the slot is made again with the built chain,
  as a streaming engine draws a lower level until the real one loads. The harness's "the world stood whole" waits for
  the queue to empty, so a measured or compared frame always draws the built chains. A warm start reads the stored
  levels on a worker as well, so the reader thread does no new I/O. Refused: the build on the reader thread, and the
  device completion kept for what the frame thread meets, which would be a second route to one result.
- **The device completion is removed** (`MipChainPass`, the loose chains, `TextureData::mCompleteChain`) once
  every chain comes from the store. `Bc7EncodePass` stays, because the ground composite encodes through it.
- **A run states its store.** The game uses `<cache>/rtx-content`, under the user's cache directory, which the renderer
  already gets (`mCacheDirectory`). The harness takes `--content-cache=<dir>|off`. A measured run uses the game's store,
  warm after a throwaway leg, as it already warms the card. A test uses a store of its own in its test folder, or none.
  A hit must give the same output as a run, and `./omw repeat` and a cold-against-warm shot hold this.
- **A pass's version moves whenever its output for the same input moves**, an encoder update included, as
  `ContentPass` states. The store's format version moves when an entry's layout moves, and an entry of another version
  is a miss.

## Items

Batch 0: measure before building.

1. **The key's cost.** Turn keying on with a store that always misses, at `seyda-neen-ship` on `openmw-pbr` and the
   Fullrest Repack: `PassStats::mKeyMs` and `mKeyBytes` against the run time, on the frame and off it. If the key costs
   more than the walks it saves, compare XXH3-128 (a proposed dependency) against MurmurHash3 before batch 1.
2. **The preprocessor's share of a start**: each pass's run time over a cold load of both profiles, from the stats
   row, which orders batches 2 and 5.
3. **The colour chains.** Level 1 of every colour and data file that carries its own chain against the box of its
   level 0, in the three installs, as the `_nh` maps were read
   (`docs/rtx/research/2026-11-08-normal-map-chains.md`): the faulty texels and the files that hold them, by mod.
   Where none is faulty, colour chains stay as their files hold them. Where some are, the user decides between
   rebuilding the faulty chains alone and rebuilding every chain, which moves vanilla's pictures at a distance, before
   batch 3.

Batch 1: the store.

4. **`ContentStore`**: the directory and its 256 folders, an entry file and its header, read and validate, a write
   through a temporary name and a rename, the used entries' times set at a checkpoint and at the end, the start's
   sweep to the cap and of stale temporary files. The file operations behind one entity whose API is the same on every
   system (`components/platform`), since a rename over an existing file and a time set are spelled differently on
   Windows. One instance for a process, shared by each thread's `ContentCache`, with the writes on a worker behind a
   bounded queue.
5. **`ContentCache` holds**: `sHolds` true, `find` and `keep` through the store, and a codec for each pass's `Output`
   that writes it and reads it back.
6. **The plumbing**: the game's store under the cache directory, the harness's `--content-cache`, the tests' stores,
   the stats row's hits.

Batch 2: the texture facts through the store.

7. `SolidReach`, `TexelMean`, `SolidFall` and `AlphaSpan` through the store, so a warm start walks no texel for a fact.

Batch 3: normal map chains.

8. **The chain pass** (`ChainPass`): input an image, output the levels below its finest in the file's format, made by
   the box filter and `rgbcx`, vendored under `extern/` with its licence.
9. **The prepared image**: `LaidImageCache` becomes the one place that turns a file into what the upload reads. It
   widens a loose format as now and joins a built chain to the file's level 0. A worker builds, the slot is made again
   when the build lands, and the world is not whole until the queue is empty.
10. **Every normal map through it.**

Batch 4: chainless files.

11. **Every file that lacks a chain through the chain pass**, the loose chain for 512 or less on a side.
12. **The device completion removed**: `MipChainPass`, `mipchain.comp`, the loose chains, `mCompleteChain` and the
    canvas room the arrival keeps for them.

Batch 5: the shapes.

13. `ShapePass`'s output through the store, if batch 0 shows its share of a start is worth it.

## Risks

- **A cold first start is slower to settle.** Every normal map in view draws its file's chain until the workers build
  it, which shows the black patches for the first seconds on MVR PBR. A warm start draws the built chains from its first
  frame of whole world.
- **The key reads every input byte at every start.** If batch 0 shows that this costs more than the walks it saves for
  the small facts, those passes stay uncached, and only the chains, whose build is the cost, are stored.
- **Disk use.** About 1.1 to 1.7 GiB for the PBR profiles, under a 16 GiB cap. A user who clears the cache directory
  only pays a cold start.
- **Two processes on one store**, a harness running beside the game. A rename is atomic, so a reader sees a whole
  entry or none, and two writers of one key write the same bytes. A sweep in one process can remove an entry the other
  is about to open, which that process takes as a miss.
- **Many small files.** A few facts a texture, tens of thousands of files for a large mod list. A fact costs one open
  at its first ask in a session; batch 0's measurement of the walks says whether that is cheaper than walking the
  texels again.
- **A slot made again changes the picture under a running history.** The denoiser and the upscaler see a texture
  change once, at the swap, as they see a texture arrive now.

## Tests and checks

- The store: an entry written and read back is byte-exact. A truncated file, a wrong magic, a wrong format version, a
  wrong key, a wrong payload digest and a payload longer than its file are each a miss, and the file is gone after.
  The sweep removes exactly the oldest entries past a cap of a few hand-sized entries, by their set times, and a stale
  temporary file and no fresh one. Two writers of one key leave one whole entry.
- The chain pass: the box of a hand-made 4x4 level, colour in linear light, alpha apart, normals unrenormalized, with
  exact expected bytes before encoding. A premultiplied fixture (alpha 0 under an ordinary normal) builds the box and
  not the black. A built BC1, BC3 and BC5 level decodes within the encoder's stated error of the box.
- A warm and a cold run give the same frames (`./omw repeat --pairs=10` with the cache warm and cold).
- `./omw shot --views=all --map --upscale=off` on vanilla and the Fullrest profile against the base: vanilla does not
  move. On the PBR profile, the patch at the Seyda Neen place is gone, and each moved picture is read by eye.
- The load: the time to "the world stood whole" and the stats row, cold and warm, at `seyda-neen-ship` on both PBR
  profiles and the Fullrest Repack, before and after. The frame's p99 and worst frame during a cold start's builds.
