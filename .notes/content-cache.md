# Proposal: preprocessed content — a content store, filled on the game's loading screen, and every chain built from its finest level

Status: proposed; the dependency, the cap, the preparing module and the chains approved by the user. When it lands, its lasting
decisions go to `docs/rtx/architecture.md` ("`Rtx::ContentPreprocessor`" and the texture bullets), and this file is
deleted. The measurements are in
`docs/rtx/research/2026-11-08-normal-map-chains.md`.

## The problem

- **MVR PBR's normal maps go black at a distance.** In 381 of the profile's 1188 `_nh` maps, the coarser levels hold
  RGB (32, 36, 0) wherever the height is 0. The mips were made with premultiplied alpha. Level 0 is correct, so a
  surface is correct near the camera and has black patches further away. The renderer reads every file's chain as the
  file holds it.
- **MVR PBR's `_spec` maps turn metal at a distance.** 343 of the PBR install's data maps hold a metalness of 0 at
  level 0 and up to 253 in the levels below it.
- **A file with no chain gets one built on the device at every load** (`TextureData::wantsCompletedChain`). A chain
  built this way is always full size, so the budget's side search cannot make it smaller. A chain of 512 or less on a
  side stays loose, at four bytes a texel. The Fullrest Repack loads 5398 such files.
- **The backend derives more from each file at every arrival, on the device**: a colour file's shading map, a normal
  map's spread, a sprite's light bake, and a land chunk's composite and gloss (`TexturePasses`,
  `GroundCompositePass`). Each is a function of its files alone.
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
renderer has no import step, because it reads mod files as they are. So a preparing step on the game's loading
screen is the import step, and its store is the game's input.

## Decisions

- **The game's input is the store, filled before play, and the game computes as little as it can in play**, by the
  user's decision. Every form the renderer reads of a file, and every derivation the host makes of one, is made on the
  loading screen and read from the store in play. The game computes only a miss: content the loading screen could not
  know ahead. A miss runs the same pass and is stored, so the next start reads it, and the game counts its misses in the
  log and the stats row, where a miss the loading screen could have known is a bug of the preparing module.
  Refused: a game that refuses content the module did not see, which a mod's script spawning an object or a body put
  together at run time would break; and a second, cheaper runtime path for a miss, a second route to one result.
- **What the backend derives from a file on the device stays the device's work at arrival**, by the user's decision
  after batch 6: the shading map, the normal spread, the sprite light bake, and the ground composite and gloss cost
  0.02 to 0.10 ms in the median frame that runs them and 1.10 ms at the worst, on frames whose p99 and worst come from
  the host (research note, "What the device's derivations cost at arrival"). Refused: each read back after its pass and
  kept in the store, a device-output format, its guard and a disk read at arrival, for under a millisecond on no
  frame's bottleneck; Unreal's derived data cache and Godot's imports keep what minutes of offline work make, not a
  sub-millisecond pass.
- **Acceleration structures are not stored**: a serialized structure is valid only on a device and driver that
  `vkGetDeviceAccelerationStructureCompatibilityKHR` accepts, so a driver update throws the store's structures away,
  and a static structure builds in the arrival's frame already. Refused for now, and measured again once the rest has
  landed.
- **The key is the content, as `ContentKey` already makes it**: the pass's name and version and every byte the pass
  reads, as one 128-bit MurmurHash3. The bytes are in memory already, because upstream's resource system loads every
  file the scene graph names. Refused: a key made of the path, size and modification time, as ccache's direct mode and
  Godot's `.import` use. A file inside a BSA has no time of its own, a later data folder shadows an earlier file under
  the same path, and a stale hit would be quietly wrong. What hashing every input costs a session is measured first
  (batch 0).
- **MurmurHash3 stays the key's hash**: a key reads about 9.5 GB/s of input, 3% of a shape's run and 7–14% of an
  alpha span's (research note, "What the preprocessor costs a start"). Refused: XXH3, a dependency for a share this
  small.
- **`SolidReach` and `SolidFall` are not cached**: their walks stop at the first texel that answers, and a key reads
  every byte, so a key costs more than the run (3.4 ms against 0.2 on the frame). Shapes, texel means and alpha spans
  are, and the chains.
- **Every normal and data map's chain is rebuilt, and a colour file keeps its own**, by the user's decision after
  batch 0: 343 of the PBR install's data maps, MVR PBR's `_spec` maps, hold a metalness of 0 at level 0 and up to 253
  below it, and colour chains are faulty in few texels a file and vanilla's in none (research note, "The colour and
  data chains"). One rule by encoding, with no threshold to judge a chain by, and no vanilla picture moves, since
  vanilla has no companion maps. Refused: rebuilding the colour chains a check finds faulty. Of the 582 colour chains
  the threshold flags, 579 match an ordinary filter best (plain box, linear light, or weighed by alpha), and one,
  the Fullrest Repack's `sm_leaves`, matches the premultiplied box by a margin (research note, "Which filter the
  flagged colour chains match"): a check would rebuild chains made on purpose with another filter. And rebuilding
  every chain, which moves vanilla's pictures for no fault.
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
  No sync before the rename, which `PipelineCache` makes for its one file a session: the payload's digest finds a body
  a power loss left unwritten, and a load writes thousands of entries that would each wait on the disk.
- **A small entry is held in memory, and the frame's thread reaches memory alone** (`StoreReach`): an entry of 4 KiB
  or less is kept once read or written, and the start's sweep loads every one on the store's thread, so a fact the
  frame's walk asks for is found with no wait on the disk; a larger entry the frame's thread computes, as it did, and
  the cell reader reads from its file. Bounded at 64 MiB of payload. Refused: a file read on the frame's thread, a
  wait on the disk inside a frame.
- **The store's bound is a size cap with least recently used eviction, by each file's modification time**, as ccache
  and the bazel-remote cache evict ([ccache](https://man.archlinux.org/man/ccache.1),
  [bazel-remote](https://github.com/buchgr/bazel-remote)). An entry's first hit of a session hands its file to the
  store's thread, which sets its time; later hits set nothing. A sweep at start, on the store's thread, lists
  the folders, sorts the entries by time and removes the oldest past the cap, and removes a temporary file older than
  a day (one a killed process left). Cap: 16 GiB, by the user's decision, against `openmw-pbr-all`'s 1.71 GiB of
  chains. Refused: the time set at every hit (ccache's access time), a write on every read; and an index file of
  sizes and times, a second record of what the folders already state. The file operations are `std::filesystem`'s,
  whose API is one on every system, and the folder listing `Platform::listFolder`'s.
- **A data map stores only the levels the file has wrong, in the file's own format**: levels 1 and down, built from
  the file's level 0, which stays byte-exact, and for a file with no chain every level below its one. The upload then
  joins level 0 from the file and the rest from the store, an ordinary file with a full chain to the side search, the
  upload and the residency. Refused: the chain kept on the device only, which no later start reuses.
- **A normal map is stored whole in BC7, in one encoding**, by the user's decision (item 11): every texel its unit
  normal in three channels, a two-channel map given its blue as a sampler of it would have, a texel or a mean that
  points nowhere made straight up, and the height kept in alpha; so the trace decodes every map as `2 rgb − 1`. BC7,
  linear, is the one block format that holds three channels and an alpha at a normal's precision: Unreal and Microsoft
  store a two-channel normal in BC5 and an RGBA texture in BC7
  ([Unreal](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-format-support-and-settings-in-unreal-engine),
  [Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11)),
  and BC1 holds a normal poorly ([Reed](https://www.reedbeta.com/blog/understanding-bcn-texture-compression-formats/)).
  The cost: a DXT1 normal map doubles (2646 of `openmw-pbr-all`'s 3858 normal maps), and level 0 moves by BC7's step.
  **A block in one BC7 mode**, by the user's decision on the measurement: mode 5 where it holds a height, whose alpha
  it keeps apart from the normal, and mode 6 where it holds none, the modes bc7enc searches first for each kind, as
  DirectXTex narrows its search by mode ([DirectXTex](https://github.com/microsoft/DirectXTex/wiki/Compress)). On MVR
  PBR's maps 16.5 million texels a second where the full search made 2.8 to 4, a height's error a mean of 0.12 to
  0.35 of 255 where it was 0.29 to 0.46, and a normal's 0.31° to 0.58° where it was 0.27° to 0.41°
  (research note, "Normal maps in BC7").
  A two-channel map stands in until its chain is made, since the trace no longer decodes two channels. Refused: each
  map re-encoded in its own format, a second loss for BC1 and BC3 and a path a format; and BC3 for all, a normal at
  5:6:5.
- **Which chains are built**: every normal and data map's (`TextureEncoding::Normal`, `TextureEncoding::Data`), and
  every chain a file lacks. A colour file's own chain stays as the file holds it, as Godot loads a DDS file's levels
  directly ([Godot](https://docs.godotengine.org/en/4.5/tutorials/assets_pipeline/importing_images.html)).
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
  separate change. One filter on the host and none on the device.
- **The coarser levels are encoded on the host, in the file's format**: BC1, BC2, BC3, BC4 and BC5, the only formats
  the three installs hold for these files. Dependency, approved by the user: `rgbcx.h` from bc7enc_rdo (Rich Geldreich,
  MIT or public domain; the licence is read from the repository before it is vendored), one header with BC1 to BC5 encoders and decoders, whose BC1 matches Compressonator and libsquish in quality at
  a higher speed ([bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo_devel/)). BC2's explicit alpha is our own
  code. Refused: an encoder of our own, a quality problem that a maintained one already solves; and new BC1 and BC3
  encoders on the device with a read-back, which adds two compute encoders and a transfer to the host to fill a disk
  file.
- **Every chain the host would lay loose is stored whole in BC7**, by the user's decision: a loose file's (RGBA8,
  BGRA8, and every widened format), its own chain where it is a colour that carried one, and a chainless block file's
  of 512 or less on a side or a BC1 file's of one level with a cut. Encoder, approved by the user: bc7enc from the same
  bc7enc_rdo commit as rgbcx (MIT or public domain), perceptual weights for a colour and linear for data, as its own
  tool chooses them; bc7decomp, from the same commit, decodes in the tests alone. **Vanilla moves by BC7's error**, by
  the user's decision against the rule that kept a level loose so that it moved by the box's rounding alone: 16 of
  vanilla's 72 pictures moved beyond the denoiser's noise, at most 28 of 255 on 0.08% of a picture's pixels, on
  animated effects (research note, "Loose chains in BC7"). Refused: the loose form kept beside BC7 for vanilla's
  files, a second route to one result.
- **A colour the trace delights is stored delit, keyed by use** (item 13, by the user's decisions): a diffuse with no
  metal-roughness `_spec` and a ground layer that is not authored are bound as `TextureEncoding::Painted`, whose
  chain is every level of the file divided by `Rtx::ShadingMap`'s estimate where it stands, held at a reflectance of
  one; a base colour, an emissive map, a sheet and a sprite are colours, read as they stand. One source made two
  textures by its use is how engines import a texture once a use (Unreal's compression settings a texture asset); a
  file used both ways is two slots. The delit chain is re-encoded in the file's own format, by the user's priority
  of memory and speed, so a BC1 colour keeps four bits a texel; a loose, small chainless or cut one is BC7. The
  estimate follows the slot's wrap, along an axis that repeats and one that clamps, so the made chain is keyed by
  it. Refused: every colour file delit, which flattens the PBR packs' authored base colours; and BC7 for every
  delit colour, which doubles every vanilla texture.
- **A chain is built on a worker, never on the frame or the reader.** A 2048-square normal map's levels take tens of
  milliseconds to build and encode. A cold start of the PBR profile builds about 1500 of them. A build on the cell
  reader would hold up each cell by seconds, and a build on the frame would be a spike. Workers drain a bounded queue.
  A slot draws the file as it stands until its built chain lands, and then the slot is made again with the built chain,
  as a streaming engine draws a lower level until the real one loads. The harness's "the world stood whole" waits for
  the queue to empty, so a measured or compared frame always draws the built chains. A warm start reads the stored
  levels on a worker as well, so the reader thread does no new I/O. Refused: the build on the reader thread, and the
  device completion kept for what the frame thread meets, which would be a second route to one result.
- **The device completion is removed** (`MipChainPass`, `RgtcEncodePass`, the arrival's loose chains,
  `TextureData::mCompleteChain`): every chain comes from the host. `Bc7EncodePass` stays, because the ground composite
  encodes through it.
- **A run states its store.** The game uses `<cache>/rtx-content`, under the user's cache directory, which the renderer
  already gets (`mCacheDirectory`). The harness takes `--content-cache=<dir>|off`. A measured run uses the game's store,
  warm after a throwaway leg, as it already warms the card. A test uses a store of its own in its test folder, or none.
  A hit must give the same output as a run, and `./omw repeat` and a cold-against-warm shot hold this.
- **The game fills the store on its loading screen, in its own process**, by the user's decision: a preparing module
  inside the game, built from the same components, that walks every model and land texture the loaded content list
  names through the mirror's own walk, on worker threads, and runs the same passes into the same store, while the
  step that now waits for the kernels (`Renderer::awaitShaders`, "Compiling Shaders") waits for it too, its label and
  bar fed from the module's progress through `Loading::AsyncListener`, as the content's own load is shown. Many PC
  games compile and prepare on such a screen before their menu; this one already compiles its kernels there, and
  Godot's import and Unreal's derived data fill run in the process that uses what they make. It reads the content list
  the game already loaded, and the images it reads stay in the game's caches for the load after it. The game still
  computes what the module could not know ahead (a script's spawns, a body put together at run time) and writes it to
  the same store, as the game builds a navigation tile `openmw-navmeshtool` missed. The harness runs the same module
  for a run's store. Refused: a separate tool the game starts as a child process (`openmw-contenttool`), which loads the
  content list a second time and needs a pipe protocol, a version handshake and a second binary kept in step, for a
  fault isolation the passes do not need: the game runs the same passes for its misses. Unreal's ShaderCompileWorker
  and Steam's Fossilize run apart against driver compilers that crash, which no pass here calls. And a launcher tab, a
  second route to the same run.
- **The module's run is incremental by each file's size and time; the game's lookup is by content.** A manifest in the
  store names every file the module read with its size, its time and the keys made from it, so a start after one mod
  changed reads that mod's files again and no other, as ccache's direct mode trusts a file's stat for its manifest and
  make trusts a time; a start where nothing changed walks nothing. The game never trusts a stat: it keys every lookup by
  the bytes, so a stat that lies costs a stale manifest and a miss, never a wrong answer. Refused: reading every file of
  the content list again to hash it on each change, minutes for a large list to find one mod's files.
- **A pass's version is held by a test**: each pass runs over fixed inputs, and a digest of its outputs is recorded
  beside its `sVersion`. An output that moves while the version stays fails the test, so a forgotten bump cannot hand
  an old output to a new build. Refused: the build's revision in every key, which empties the store at every build.
- **A store that cannot be written is a store that misses**: a full disk, a read-only directory or a failed rename is
  logged once, and the game goes on computing what it would have stored.
- **The scene graph stays the renderer's one input**: the store holds what the mirror derives from it, and not
  models of its own that would replace the walk. Upstream's model and land reading is a small share of a load, and the
  passes are most of the reader's time (research note, "What a load costs the processor"). Refused: a store of
  whole prepared models read in place of the scene graph, a second reader of the content for a share the
  measurement does not show.
- **What the program computes at every start from no content is made by the build**: the blue noise's rank
  matrices and the fog's noise, 136 ms and 53 ms of each start's main thread, are generated at build time and
  compiled in, as the kernels' SPIR-V is.
- **A pass's version moves whenever its output for the same input moves**, an encoder update included, as
  `ContentPass` states. The store's format version moves when an entry's layout moves, and an entry of another version
  is a miss.

## Items

Batch 0 is done; its measurements are in the research note, and its outcomes are the three decisions after the
key's.

Batches 1 and 2 are done: the store, its plumbing, and the texel mean and the alpha span kept in it. The texture
facts came with the store, since a store no pass reaches would be code nothing runs. A warm load of `seyda-neen-ship`
on `openmw-pbr` finds all 488 alpha spans and 19 texel means, which ran 953 ms cold, in 142 ms of keying, and a cold
and a warm shot draw the same frames.

Batch 3 is done: rgbcx vendored (`extern/rgbcx`, its licence in `files/licenses/rgbcx.txt`), the host chain
(`Rtx::ChainBuild`, which the test support's chain now is), the chain pass kept in the store, and the prepared images
(`Rtx::PreparedImages`) that every world slot of a normal or data map reads once made. The patch at the Seyda Neen place
is gone; a settled cold and warm shot draw the same frames, and both stand the world whole on frame 429. What it does
otherwise than the items said:

- **The made image is held by its slot** (`TextureRow::mPrepared`) and not in `LaidImageCache`, which stays the widening
  alone: the slot decides how long the image lives, and the hand-over stands a slot again once its image is made
  (`TextureTable::prepare`), which the frame's describe reads in place of the file.
- **A settled run waits for the images being made** at each hand-over (`SceneUploader::Handing::mSettled`), as a settled
  walk waits for its cells: a cold run otherwise stood the world whole on a frame its threads' timing chose, and two
  cold runs drew two pictures.
- **A made chain holds the levels under the finest alone** (`MadeImage`), and a slot describes it as the file's finest
  level with them under it (`TextureData::mCoarser`): copying the finest level into one buffer held 1226 MiB twice at
  `seyda-neen-ship` on `openmw-pbr`, where the made levels are 409 MiB (research note, "What the made chains hold").
- **Not made again**: BC4 and BC5 maps, which no host reader decodes and whose channels hold no alpha to be weighed by,
  and a BC1 map with a cut, whose one bit of alpha a box blurs.
- `halving.h`, the odd-axis rule, moved to the core's shared headers, so the host chain reads the device's.

Batch 4 is done: every file that lacks a chain has one made by the chain pass, and the device completion is gone
with its passes, its shaders, the arrival's canvas and the BC4 and BC5 encoder. What it does otherwise than the items
said:

- **A made chain has two forms** (`ChainForm`): loose to a texel for a block file of one level up to 512 a side and for
  a BC1 file of one level with a cut, whose coverage BC1 cannot hold; coarser levels in the file's own format for
  every other, which a 4096-square BC1 replacer with no levels keeps in 2.7 MB where loose it took 89 MB.
- **A picture's scene reads the prepared images too**, and is drawn again while a slot of it stands its file
  (`TracedView::draw`), as the world stands a slot again: with the completion gone, a picture's chainless file had no
  chain. A settled hand-over waits for the images inside the describe (`SceneTextures::describe`). A picture that
  always waited held the frame for 339 ms at a cold PBR doll's first hand-over, 330 ms of it waiting, where unsettled
  no hand-over waits.
- **The chain threads' figures are in the report**, a row of their own (`Preprocessed::mPrepared`).
- **A chainless BC4 or BC5 file stands at its one level**, since no host reader decodes it: none of the PBR install's
  or the Fullrest Repack's files of one level is one (research note, "Formats").

Batch 5 is done: the shape pass's output is kept in the store through a codec of its own. Warm, the cell ring finds
every shape at `seyda-neen-ship` on `openmw-pbr` and keys them in 21 ms, where it ran them for 0.9 s cold (research
note, "The shapes through the store"). What it does otherwise than the item said:

- **The frame finds only the small shapes**, which the store holds in memory: its asks reach no disk, so it runs the
  138 large ones again at every start, a quarter of its shape time. Batch 7 does not change this.
- **A write of a key whose file stands stamps it and writes nothing**, since the frame offers those large shapes
  again in every session; and **a payload its pass cannot read is discarded**, so the output run in its place is
  written (`ContentStore::discard`).
- **The pass version guard is in** (`RtxPassVersionsTest`), for the four stored passes, since a stored shape that a
  forgotten bump left would be a wrong mesh.

Batch 6 is done: each derivation is timed in a zone of its own at arrival (`FrameZone::Shading`, `Spread`, `Bake`,
beside `Ground`), and measured on the streaming route on three profiles.

Batch 7 is done: every model a record of the loaded content names and every ground layer it names is read on the
loading screen (`Rtx::ContentPreparation`, `MWRender::contentModels`, `MWRender::contentLayers`) as the cell reader
reads it, its shapes, texture facts and chains into the store, beside the kernels' compile, under "Preparing
Content". At `seyda-neen-ship` on `openmw-pbr`, a cold start prepares 6620 models and 135 layers in 62 s, its chains
940 s of processor time on 16 threads, and play then finds every shape and alpha span the ring asks and 1249 of the
1264 chains; a warm start finds all 6755 as they were in 0.13 s (research note, "The loading screen's preparation").
What it does otherwise than the items said:

- **`awaitShaders` keeps its name and its one call**: the preparation starts and is waited for inside it, so
  `OMW::Engine` is unchanged; the label's string is new in each of the engine's six languages.
- **A layer's files come through the terrain storage's own rule**, `ESMTerrain::Storage::getLayerInfo`, made public,
  on a storage of the preparation's own under the game's settings, and the land textures through the iteration
  `Store<ESM::LandTexture>` now offers: two upstream hooks.
- **The resource caches are emptied once a second and at the end** of the preparation: aged by their expiry they
  kept every model walked, 14.7 GB at the peak under the harness, whose stated step never ages them; emptied, 8.0 GB.
- **The manifest is written only where something moved**, and a model no file stands for is walked again once its
  stamp moves. A model depends on every companion file the loader looks for beside its diffuses
  (`Shader::appendCompanionPaths`), and a layer is named by its normal map as well as its diffuse, so a map a mod
  adds is the model or the layer read again.

Batch 8: content in the renderer's own forms, by the user's decisions on the audit, one at a time.

Item 10 is done: every loose chain is made whole in BC7 (`ChainForm::Bc7`, `MadeImage::whole`), a quarter of its
bytes where it was loose. At `seyda-neen-ship` on `openmw-pbr`, a cold start prepared in 65.7 s, its chains 868 s of
processor time, where it took 72.5 s and 940 s before; the store came to 1.4 GiB (research note, "Loose chains in
BC7"). What it does otherwise than the item said:

- **A loose colour's own chain is encoded as it stands**, and a chain is keyed by every level it reads, so two files of
  one finest level and two chains are two entries.
- **A file in a format the scene refuses has no chain made** (`PreparedImages::prepares`), where a loose file of such a
  format would have asked for a layout it has none of.
- **A widened file's laid copy is still held** after its chain is made, by the cell reader, the material resolver and
  the laid image cache, though the description reads the BC7 chain alone.

Item 11 is done: every normal map is made whole in BC7 in one encoding (`ChainBuild::buildNormals`), and the trace
decodes `2 rgb − 1` alone. At `seyda-neen-ship` on `openmw-pbr`, a cold start prepares in 64 s, as before; the store
holds 5.6 GiB where it held 1.4, every normal map's finest level now in it (research note, "Normal maps in BC7").
What it does otherwise than the item said:

- **`resolve` keeps the plane for a normal that points nowhere**: a mesh with no normals carries noughts, and a file
  drawn before its chain is made may hold a grey texel. The spread pass keeps its flat normal for the same file.
- **A normal map's block takes one BC7 mode**, mode 5 with a height and mode 6 without, where the full search made the
  cold start 270 s; bc7enc has no mode 4.

The order is 13, then 12, by the user's decision: a converted classic material's base colour is solved from the
delit diffuse, which item 13 makes.

Item 13 is done: the trace reads every colour as it stands, and `ShadingPass`, the shading maps, `delitTexel`, the
frame's `mDelight`, the harness's `--delight` and the shading zone are gone; a texture other than a normal map stands
with nothing beside it. At `seyda-neen-ship` on `openmw-pbr`, a cold start prepares in 103 s where the build before
it took 81 s on the same day, and the store holds 6.0 GiB where it held 5.6 (research note, "Colours stored delit").
What it does otherwise than the item said:

- **A medium's diffuse is delit too**, a diffuse with no specular map, where the gather read it as it stood: taking
  it as a colour would need the blend facts first, which are kept on the texture the take makes.
- **The estimate is read as the texture is addressed** (`paintedLight`), clamped along an axis the slot clamps, as
  the device read it through the texture's own sampler.

12. **One specular layout**: every classic `_spec` map converted by preparation to metal-roughness, and the
    `[RTX] specular map layout` setting, `MATERIAL_SPECULAR_CLASSIC` and `layerClassic` gone, by the user's decision.
    - **A map's layout is detected per file**, by the user's decision: a map whose red, green and blue are near equal
      (a mean spread under 0.1) is classic, and every other metal-roughness. The Fullrest Repack's 14 classic maps
      spread 0.004 to 0.042, and the PBR installs' 3400 metal-roughness maps 0.151 at the least, 0.99 at the median.
      Refused: the layout kept as a load option, a setting the item removes; and every map read as metal-roughness,
      which reads a classic map's grey highlight as metal.
    - **A classic pair is converted per texel by Khronos' converter**, by the user's decision: the metalness solved
      from the delit diffuse's and the highlight's perceived brightness (`solveMetallic`), the base colour rewritten,
      and the roughness the exponent's (`roughnessOfExponent`)
      ([Khronos](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Archived/KHR_materials_pbrSpecularGlossiness/examples/convert-between-workflows)).
      A pair is a material's diffuse and its `_spec`, so a converted texture is made a pair. Refused: a metalness from
      the diffuse's mean, wrong where the diffuse varies under one map; and a dielectric alone, which drops a
      highlight of 0.1 to 0.87 to 0.04. glTF-Transform's `metalRough` keeps a separate specular colour
      (`KHR_materials_specular`), a second layout this item removes.


Kept, by the user's decisions: **loose formats widened on read** (`layImage`), where a stored copy is four times a
file on disk and a read for a cheap conversion. Moved to `.notes/todo.txt`: **vertices packed for the device**, the
renderer's own format and no content's.

Batch 9: program constants, at any time.

11. **The blue noise's rank matrices and the fog's noise generated by the build** and compiled in, with a test that
    the compiled table equals what the generator makes.

## Declined

- **The device's derivations through the store** (items 5 and 6): under a millisecond at their worst, on frames whose
  slow ones are the host's; the decision above says what storing them would cost.

## Risks

- **A miss draws its file as it stands until a worker builds it.** With the loading screen's run, a miss is content
  it could not know ahead, and few.
- **A first start of a large mod list waits on the loading screen.** The module prepares everything the content list
  names before the menu: on the first start, or after the list changed, minutes for a list as large as the Fullrest
  Repack. The screen says what it does and how much is left. The loading screen has no way to skip, and adding one is
  upstream's code, a decision for later if the wait proves long.
- **Disk use.** About 1.1 to 1.7 GiB for the PBR profiles, under a 16 GiB cap.
  A user who clears the cache directory only pays a cold start.
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
- The module's incremental run: a file whose time moved is read again and one whose time stayed is not; a manifest
  whose file the content list lost drops its keys from the next run.
- A warm and a cold run give the same frames (`./omw repeat --pairs=10` with the cache warm and cold).
- `./omw shot --views=all --map --upscale=off` on vanilla and the Fullrest profile against the base: vanilla does not
  move. On the PBR profile, the patch at the Seyda Neen place is gone, and each moved picture is read by eye.
- The load: the time to "the world stood whole" and the stats row, cold and warm, at `seyda-neen-ship` on both PBR
  profiles and the Fullrest Repack, before and after. The frame's p99 and worst frame during a cold start's builds.
