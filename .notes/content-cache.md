# Proposal: preprocessed content — a content tool, its store, and every chain built from its finest level

Status: proposed; the dependency, the cap, the tool and the chains approved by the user. When it lands, its lasting
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
renderer has no import step, because it reads mod files as they are. So a content tool run before the game is the
import step, as `openmw-navmeshtool` is for the navigation mesh, and its store is the game's input.

## Decisions

- **The game's input is the tool's output, and the game computes as little as it can**, by the user's decision. Every
  form the renderer reads of a file, and every derivation it now makes of one, is made by the tool before the game and
  read from the store by the game. The game computes only a miss: content the tool could not know ahead, or a store
  older than the content list. A miss runs the same pass the tool runs and is stored, so the next start reads it, and
  the game counts its misses in the log and the stats row, where a miss the tool could have known is the tool's bug.
  Refused: a game that refuses content the tool did not see, which a mod's script spawning an object or a body put
  together at run time would break; and a second, cheaper runtime path for a miss, a second route to one result.
- **Everything the backend derives from a file at arrival is preprocessed too**: the shading map, the normal spread,
  the sprite light bake, and the ground composite and gloss. The tool runs the same device passes with no window, as
  the harness runs the renderer, and reads their results back into the store; the game uploads them and runs no pass.
  A miss runs the pass on the game's device and reads it back into the store. The shaders' float arithmetic is the
  build's (`spirvpin.hpp`), so the result is the same on every device and the key needs no device in it. Refused:
  host ports of the passes, a second implementation of each.
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
- **Only the levels the file has wrong are stored, in the file's own format.** For a normal map, levels 1 and down, built
  from the file's level 0, which stays byte-exact. For a file with no chain, every level below its one. The upload
  then joins level 0 from the file and the rest from the store. The result is an ordinary file with a full chain, so
  the side search, the upload of a chain and the residency work as they work now. Refused: the whole chain re-encoded
  to BC7, which doubles a DXT1 map (2762 of the PBR install's normal maps) and moves level 0 by BC7's low bit; and the
  chain kept on the device only, which no later start reuses.
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
- **A chainless block file of 512 or less on a side keeps its loose chain**, stored loose, so a vanilla level moves by
  the box's rounding and not by an encoder's step. Its level 0 is the file's decoded, as before. **Vanilla moves by a
  byte**, by the user's decision: the host's box rounds where the device's rounded in its own float order, and 11 of
  vanilla's 72 pictures moved by 1 to 3 of 255 on at most 1.25% of their pixels. Refused: the device completion kept
  for vanilla's files so that no picture moves, a second route to one result.
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
- **A tool fills the store before the game, as `openmw-navmeshtool` fills the navigation mesh cache**, by the user's
  decision: a C++ app in the tree, built from the same components, that loads the content list, walks every model and
  land texture it names through the mirror's own walk, and runs the same passes into the same store. The game
  still computes what the tool could not know ahead (a mod added since, a script's spawns, a body put together at run
  time) and writes it to the same store, as the game builds a navigation tile the tool missed. The tool writes a
  manifest of the content list and data folders it read, and the game logs where its own differs, so a stale store is
  named. Refused: a Rust tool that reads NIF, DDS, BSA and ESM again, a second implementation whose output must match
  the game's byte for byte; and a Rust front end over the C++ through bindings, a second toolchain for a progress bar.
- **The game starts the tool itself, at every launch, and shows its progress on the loading screen before the main
  menu**, by the user's decision, so a game started without the launcher prepares its content too. The renderer
  starts the tool as a child process when it is made, so the tool runs beside the game's own loading of the content
  files, and the loading step that now waits for the kernels (`Renderer::awaitShaders`, which shows "Compiling
  Shaders") waits for the tool as well, its label and bar fed from the tool's progress. Many PC games compile and
  prepare on such a screen before their menu; this one already compiles its kernels there. The tool reports its
  progress as binary messages on its standard output, as navmeshtool reports to the launcher
  (`components/navmeshtool/protocol.hpp`, [navmeshtool](https://modding-openmw.com/tips/navmeshtool)), and a reader
  thread hands them to the screen through `Loading::AsyncListener`, as the content's own load is shown. **One route**:
  the tool always runs, compares its manifest with the content list and ends at once where nothing is new, so the
  game asks no question of its own before starting it. A tool that fails or ends is logged, and the game goes on,
  computing its misses. Refused: a launcher tab as well, a second route to the same run (the tool keeps its command
  line for scripts and the harness); and the tool's work done inside the game's process, where a fault in a pass would
  end the game and its memory would stay in the game's heap.
- **The tool's run is incremental by each file's size and time; the game's lookup is by content.** The manifest names
  every file the tool read with its size, its time and the keys made from it, so a run after one mod changed reads
  that mod's files again and no other, as ccache's direct mode trusts a file's stat for its manifest and make trusts
  a time. The game never trusts a stat: it keys every lookup by the bytes, so a stat that lies costs the tool a stale
  manifest and the game a miss, never a wrong answer. Refused: a tool that reads every file of the content list again
  to hash it on each change, minutes for a large list to find one mod's files.
- **A pass's version is held by a test**: each pass runs over fixed inputs, and a digest of its outputs is recorded
  beside its `sVersion`. An output that moves while the version stays fails the test, so a forgotten bump cannot hand
  an old output to a new build. Refused: the build's revision in every key, which empties the store at every build.
- **A device pass stores the same bytes on every run**: a test runs each of them twice over one input and compares
  the read-back bytes, beside the pinned float arithmetic that makes them the same on every device.
- **A store that cannot be written is a store that misses**: a full disk, a read-only directory or a failed rename is
  logged once, and the game and the tool go on computing what they would have stored.
- **The tool is the game's own build**: the game starts the tool next to its own executable, and the first message
  carries the protocol's version and the build's revision; a tool of another build is ended and logged, and the
  game computes its misses.
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
  138 large ones again at every start, a quarter of its shape time. Batch 7's tool does not change this.
- **A write of a key whose file stands stamps it and writes nothing**, since the frame offers those large shapes
  again in every session; and **a payload its pass cannot read is discarded**, so the output run in its place is
  written (`ContentStore::discard`).
- **The pass version guard is in** (`RtxPassVersionsTest`), for the four stored passes, since a stored shape that a
  forgotten bump left would be a wrong mesh.

Batch 6: the device's derivations.

4. **Their cost at arrival measured first**: each pass's device time and the worst frame of a load at
    `seyda-neen-ship` on both PBR profiles and the Fullrest Repack, against which the two items after it are judged.
5. **The shading map, the normal spread and the sprite bake through the store**: each read back after its pass and
    stored, and uploaded from the store at arrival where it is there.
6. **The ground composite and gloss through the store**, keyed by the chunk's layers, their blend weights and the
    delight they were baked at.

Batch 7: the tool.

7. **The content tool** (`apps/contenttool`, `openmw-contenttool`): the content list loaded as navmeshtool loads it,
    from the configuration the game hands it, every model and land texture walked through the mirror's walk, every
    host pass on worker threads and every device pass on a windowless device into the store, the manifest written,
    progress as binary messages on its standard output, and as text on a terminal.
8. **A child process with a pipe** in `Platform::Process`: start a program with its arguments, read its standard
    output, wait for it, end it. A POSIX and a Windows file behind the one header, as the rest of it is.
9. **The game's start**: the renderer starts the tool when it is made, a reader thread turns its messages into the
    loading screen's label and bar, and the step that waits for the kernels waits for the tool too
    (`Renderer::awaitShaders` named again for what it now waits for). The game's log line and stats row of its
    misses. The harness starts it the same way, into the store its run states. The call in `OMW::Engine` is upstream
    code and enters the accepted diff.

Batch 8: content in the renderer's own forms, by the user's request.

10. **The audit**: every place the renderer accommodates a form the content comes in, rather than the one form it
    reads, each with whether it is a function of the files alone (the tool can make it once) or of a run's state (it
    stays). The candidates found so far, each a decision for the user after its measurement:
    - **Normal maps in one encoding.** `decodeNormal` reconstructs the blue where a file holds none, and `resolve`
      stands the plane in for a texel of (0.5, 0.5, 0.5), on every hit. Rewritten by the tool into one encoding with
      a unit normal in every texel, the decode is one expression. Its cost: level 0 re-encoded, and no longer the
      file's bytes, against the decision above that keeps them.
    - **Pictures stored delit.** `sampleAlbedo` divides every colour sample by its shading map at the run's delight
      (`delitTexel`). The game always runs at a delight of one, so the tool could store each colour file divided
      once, and the trace would read it as it stands. Its cost: the harness's `--delight` A/B loses its one route
      unless it reads the files as they stand, and a delit file in BC1 or BC3 is re-encoded.
    - **One specular layout.** The trace and the ground branch on the classic layout against the metal-roughness one
      (`MATERIAL_SPECULAR_CLASSIC`, `layerClassic`). The classic layout's coloured highlight has no metal-roughness
      equal for a dielectric, so a conversion loses it; measured before it is proposed.
    - **Loose formats laid to RGBA8** (`layImage`), now on the reader thread, made once by the tool with the chains.
    - **Vertices packed for the device.** A vertex's colour is three floats in linear light, 22 of the 125 MB of
      vertex and index bytes at `seyda-neen-ship`, and its normal three floats. Packed by the tool, at 16 bits a
      channel for the colour and 32 bits for an octahedral normal as the tangent already is, they would read less
      memory on every hit. Its cost: a precision measured against the shots before it is proposed.
    - **Loose files compressed for the device**: a loose file is four bytes a texel, a BC7 one one. Its cost: a
      vanilla loose file's picture moves by BC7's low bit, against the rule that keeps them loose.

    Already preprocessed: the shape fold and the normals split at hard edges (`ShapePass`), and the alpha facts.

Batch 9: program constants, at any time.

11. **The blue noise's rank matrices and the fog's noise generated by the build** and compiled in, with a test that
    the compiled table equals what the generator makes.

## Risks

- **A miss draws its file as it stands until a worker builds it.** With the tool run, a miss is content the tool
  could not know ahead, and few; a game started on a store older than its content list shows MVR PBR's black patches
  for its first seconds, which the tool's run on the loading screen prevents.
- **A first start of a large mod list waits on the loading screen.** The tool prepares everything the content list
  names before the menu: on the first start, or after the list changed, minutes for a list as large as the Fullrest
  Repack. The screen says what it does and how much is left. The loading screen has no way to skip, and adding one is
  upstream's code, a decision for later if the wait proves long.
- **Disk use.** About 1.1 to 1.7 GiB for the PBR profiles, under a 16 GiB cap.
  The ground composites are the open figure: a 512-square BC7 composite and its gloss are about 0.7 MB a chunk,
  so a mod list with Tamriel Rebuilt's thousands of exterior cells could store several GiB of them. Batch 6 measures
  them before they land, and they stay runtime work if they crowd the chains out of the cap. A user who clears the cache directory
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
- The device passes' guard: two runs over one input, the same read-back bytes.
- The tool's incremental run: a file whose time moved is read again and one whose time stayed is not; a manifest
  whose file the content list lost drops its keys from the next run.
- A warm and a cold run give the same frames (`./omw repeat --pairs=10` with the cache warm and cold).
- `./omw shot --views=all --map --upscale=off` on vanilla and the Fullrest profile against the base: vanilla does not
  move. On the PBR profile, the patch at the Seyda Neen place is gone, and each moved picture is read by eye.
- The load: the time to "the world stood whole" and the stats row, cold and warm, at `seyda-neen-ship` on both PBR
  profiles and the Fullrest Repack, before and after. The frame's p99 and worst frame during a cold start's builds.
