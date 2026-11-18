# Proposal: one spelling for the tree, and the content's memory

One plan of eighteen items. Items 1–7 give the tree one spelling: the four `todo.md` items under Code (audit macros,
`OPENMW_FFMPEG_5_OR_GREATER`, audit `#if`s, order includes) and "unsigned int to uint32", surveyed at `8c032f6e00` over
`apps/` and `components/` (4,023 sources: 1,393 the fork's, 2,630 upstream's, by `fork_files`), and
`extern/osg-ffmpeg-videoplayer` where it holds the FFmpeg macros. Items 8–17 are how content gets from its files to
the device, audited at `4b608db89f` by reading: the game's loaders (`components/resource`), the mirror and the cell
ring, the scene's tables and the uploader, the content store and the texel path, and the device's memory. What the
audit says of sizes is a dated note's measurement or is marked *inferred*; the lines the plan leans on were read twice,
and the handheld's tours at `8c032f6e00` bear out its two largest claims. The plan takes in the open issues at the head
of `.notes/ISSUES.md`.

Written for `/execute-plan`: items in order, each a commit, `./omw gate` at the end. Every decision that was the
user's to make is made and recorded under **Approved**. When the run lands, the decisions on the content fold into
`docs/architecture.md` ("Loading", "The content store", "Device and memory"), those on the tree into `AGENTS.md` and
`UPSTREAM.md`, and this file is deleted.

## Order

Why the items stand in this order, each a reason a later item leans on:

- **What moves no frame goes first, whole** (items 1–7). Every line the later items write is then written in the tree's
  final spelling, under the lint and the checks that hold it, so nothing is fixed twice; and the baseline and every
  tour after it differ only by changes that can move memory or a frame.
- **Dead code out before a tree-wide rewrite** (items 1–2 before 4–5), so the rewrites touch only what stays.
- **The normalizer before the rules it applies** (item 3): the includes' quoted paths and the integers both run
  through it, and the merge driver needs it before the first tree-wide commit meets an upstream merge.
- **Includes before integers** (item 4 before 5): the `<cstdint>` a rename adds lands in a block clang-format already
  forms.
- **The checks before the code they would otherwise meet twice** (item 6 before item 8), and **the platform's
  branches last of the seven** (item 7), the largest of them and the most Windows CI.
- **The harness runs as the game before the baseline** (item 8), or every later figure is held against a run on huge
  pages with a door reach no player has.
- **The heap before anything measured on the handheld** (item 10): it ends its tours three gigabytes into swap, and a
  tour that swaps takes an hour and moves from run to run.
- **Small and independent before large** (items 9–11): the faults, the heap and the ceiling need no format change, and
  end the handheld's refusals and its caches' thrash, so the tours that follow read clean.
- **Every format change of the store and its manifest in one item** (item 12), so the stores are made again once.
- **What a player feels before what tidies** (reviewed with the user, 2026-11-17, at `579612228d`): the rest of the
  plan is ordered by what the handheld measured, not by the shape it was written for. The longest frames are explained
  first (item 8), since every later item is judged against them; then the ring's reading again of models (item 15),
  the largest cost the tours name; then the device's mesh memory (item 14.6), where the tours still refuse structure
  blocks; then need (item 16), behind its meter. **Item 16 then moved first** (the user, 2026-11-17: memory before
  frame times, and levels by distance), ahead of items 19, 15 and 14.6.
- **The rest by what it cures, at the user's review (2026-11-18)**:
  - item 20, the script lookups the worst frames name on both machines;
  - item 19, the device memory made on the frame;
  - the cheap fixes, items 23–25;
  - item 16's rest;
  - item 21 before item 14.6, since a texture block emptied may be the room a structure block wants;
  - item 15, behind its measure;
  - items 22, 26 and 28;
  - item 18, whose tours also answer the Repack's refusals and outruns. What only tidies a mechanism that works is declined: the texture
  planner's rest (item 13), the ring's flat records unless measured (item 14.4), the streams' spike (item 17).
- **The texture's level moved down before the geometry's host copy went** (item 13's first part before 14): until no
  world is rebuilt for a reserve that moved, a rebuild read that copy.

## Findings

**The fork's own code is already nearly clean.** Its 1,393 sources hold 10 `#define` lines, every one a library's switch
(`VMA_IMPLEMENTATION`, `VOLK_IMPLEMENTATION`, `SPV_ENABLE_UTILITY_CODE`), a compiler's spelling (`OMW_NO_UNIQUE_ADDRESS`)
or a test's fallback, and 11 conditionals. They spell fixed widths `std::uint32_t` 7,851 times and the unqualified
`uint32_t` never. What the items find is upstream's, and what the pinned set and the compiler floors have made dead.

**What the conditionals test is pinned.** The set (`vcpkg.json`, the ports) and `cmake/Checks.cmake` fix every version a
`#if` asks about, so each branch below is always taken or never:

| Asks | Pinned | Sites |
| --- | --- | --- |
| `OPENMW_FFMPEG_5_OR_GREATER`, `_CONST_WRITEPACKET`, `_CONST_INPUTFORMAT` | FFmpeg 9.0.2: all true | 17 in `apps/openmw/mwsound`, 17 in `extern/osg-ffmpeg-videoplayer` |
| `BT_BULLET_VERSION < 310`, `< 317`, `>= 289` | Bullet 3.25 (`325`) | 7: `heightfield.{cpp,hpp}`, `bulletdebugdraw.cpp`, `recastmeshbuilder.cpp` |
| `BT_NO_PROFILE` | defined: the port builds without `BT_ENABLE_PROFILE` | 4: `debugwindow.cpp`'s profiler tab, `physicssystem.cpp` |
| `QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)` and `(6, 5, 0)` | Qt 6.11.2 | 8: five in `apps/opencs`, three in `components/platform/application.{cpp,hpp}` |
| `_MSC_VER <= 1935`, `BOOST_WORKAROUND(_MSC_VER, <= 1900)` | MSVC 19.44 floor | 4: `vfs/pathutil.hpp`, `files/configfileparser.cpp` |
| `!defined(__clang__) && __GNUC__ <= 14` | GCC is refused at configure | 6: `esm3/testsaveload.cpp` |
| `_M_IX86`; `__x86_64__ \|\| _M_X64` | x64 only | 2: `crashpadclientwin32.cpp` (the fork's), `opencs/view/doc/view.cpp` |
| `NO_LUAJIT`, `USE_OPENXR`, `DEBUG_GROUPSTACK` | never defined (`DEBUG_GROUPSTACK` is `#undef`'d above its uses) | 3, 1, 5 |
| `O_BINARY` | never defined on POSIX | 1: `platform/fileposix.cpp` |
| `ALC_ALL_DEVICES_SPECIFIER` fallback | OpenAL Soft 1.25.2's `alc.h` defines it | 2: `openaloutput.cpp`, `launcher/utils/openalutil.cpp` |
| `OPENMW_PROJECT_SOURCE_DIR`, `OPENMW_DATA_DIR` fallback to `"."` | CMake defines both for every test target | 6, two of them the fork's |
| `Q_MOC_RUN` | the Boost 1.48 workaround for Qt 4's moc (QTBUG-22829); Qt 5's moc preprocesses | 15 headers |
| `OSG_HAS_MULTIVIEW` | undefined: the pinned OSG has no multiview (`cmake/Dependencies.cmake`) | 16, `stereo/multiview.cpp` and the rasterizer |
| `OSG_VERSION_LESS_THAN(3, 6, 6)` | OpenMW's OSG reports 3.6.5: **live**, unless its pinned commit carries OSG #1028's fix | 1: `glrenderer.cpp` |
| `OSG_GLES*_AVAILABLE`, `OSG_GL1_AVAILABLE`, `GL_DEBUG_OUTPUT` | desktop GL, compatibility profile | 5, the rasterizer |

**843 of the tree's 939 `#define`s are three vendored OpenAL Soft 1.23 headers** (`mwsound/alext.h`, `efx.h`,
`efxpresets.h`). Every name they define is in the pinned OpenAL Soft 1.25.2's `<AL/alext.h>`, `<AL/efx.h>` and
`<AL/efx-presets.h>`, which add only their own guards. 49 more are GL enum fallbacks (`sceneutil/color.hpp`,
`depth.hpp`, `stereo/multiview.cpp`), one of them wrong (`GL_RGB10_A2` as `0x906F`, which is `GL_RGB10_A2UI`; GL 1.1
defines it, so the line never fires) and one a name GL does not have (`GL_SRGB_ALPHA8` for `0x8C42`, GL's
`GL_SRGB_ALPHA`).

**Macros a function or a template would replace** — upstream's, each with a `#define` and the uses beside it:

- `CONTROL` (`mwlua/localscripts.cpp`): eight `sol::property` pairs, one a member → a template on the member pointer.
- `MATERIAL_FUNC` (`sceneutil/serialize.cpp`): three functions pasted per property for `osgDB::UserSerializer` → three
  function templates on the getter and setter, whose instantiations are the plain function pointers the serializer takes.
- `COMPARE_BINDING` (`sceneutil/optimizer.cpp`): two `return`s hidden in a macro → a comparison function.
- `MYGUI_PLATFORM_EXCEPT`, `_ASSERT`, `_LOG` (`myguiplatform/myguirendermanager.cpp`) → functions; the call
  site travels as a defaulted `std::source_location` in place of `MYGUI_BASE_EXCEPT`'s `__FILE__` and `__LINE__`.
- `RUN_TEST_FOR_TYPES` (`openmw_tests/mwworld/teststore.cpp`) → one variadic `forEachType<Ts...>` over a generic lambda.
- `EXPECT_ERROR` (`components/testing/expecterror.hpp`, used by 6 test sources) → a function over an invocable, failing
  through `ADD_FAILURE()`, which, unlike `FAIL()`, does not need to stand in the test's body.
- `CS_QT_STD_FILESYSTEM_PATH_DECLARED` (two `opencs` headers): a hand guard around one `Q_DECLARE_METATYPE` → one
  header that declares it.

**Macros that stay**, no C++20 construct doing their work: the stringifying `SHAPE_NAME` (`bullethelpers/debug.hpp`),
the navigator's status names, `LOAD_FUNC` (`openaloutput.cpp`) — a table of names is what C++26's reflection would
replace; libraries' switches; `OMW_NO_UNIQUE_ADDRESS`, the one attribute MSVC spells apart; `FAR`/`NEAR` around
`windowspath.cpp`'s shell headers, which `misc/windows.hpp`'s `#undef far` makes necessary; `fx/pass.cpp`'s lines,
which are GLSL inside a string. The GLSL-shared headers (`shaders/*.h`) are AGENTS.md's exception and out of scope.

**Platform branches outside a platform entity** (`#ifdef _WIN32`, `WIN32`, `__linux__`, `Q_OS_WIN` in callers), all
upstream's: `navmeshtool/main.cpp`, `opencs/editor.cpp`, `opencs/main.cpp`, `opencs/model/doc/runner.cpp`,
`openmw/main.cpp`, `mwscript/miscextensions.cpp`, `config/launchersettings.cpp`, `debug/debugging.{cpp,hpp}` (12),
`files/configurationmanager.cpp`, `files/fixedpath.hpp`, `misc/endianness.hpp`, `misc/thread.cpp`,
`misc/timeconvert.hpp`, `platform/platform.cpp`, `platform/application.{cpp,hpp}`, `process/processinvoker.cpp`,
`launcher/maindialog.hpp`. `WIN32` is CMake's default `/DWIN32` flag, not the compiler's: the tree reads it beside the
compiler's `_WIN32` in 10 places.

**Includes.** `.clang-format` sorts within a block and never forms one (`IncludeBlocks: Preserve`), and its categories
are LLVM's (`llvm|clang`), which match nothing here. Against AGENTS.md's five blocks, a rough scan finds 51 of the
fork's sources out of order, a block split, or a quoted path outside the file's folder, and 1,299 of upstream's (636 of
them quoting `"../mwbase/world.hpp"`-style paths).

**Integer spellings**, outside comments and strings, the GLSL-shared headers and the vendored OpenAL headers aside:

| Spelling | Fork | Upstream |
| --- | --- | --- |
| `int` | 1,806 in 347 files | 12,092 in 1,299 |
| `unsigned int`, bare `unsigned` | 357 in 112 | 1,350 in 345 |
| `unsigned char`, `signed char` | 90 in 33 | 348 in 117 |
| `short`, `unsigned short` | 6 in 3 | 190 in 42 |
| `long` keywords (`long long` counts two) | 28 in 10 | 53 in 16 |
| unqualified `uintN_t`/`intN_t` | 0 | 1,867 in 334 |
| unqualified `size_t` | 10 in 5 | 1,326 in 343 |
| `std::uintN_t`/`std::intN_t` | 7,851 in 763 | 1,163 in 278 |

On both targets (x86-64 Linux, x64 Windows) `std::int32_t` is `int`, `std::uint32_t` is `unsigned int`,
`std::int16_t` `short`, `std::uint8_t` `unsigned char`: the rename of every spelling but `long` is the same type,
with no change of overload, ABI, format or behaviour. `long` is 64 bits on Linux and 32 on Windows, so each `long` is a
choice of width, and `std::int64_t` is `long` on one and `long long` on the other.

Where the memory stood when measured:

| Run | Figure | Held by |
|---|---|---|
| handheld, PBR tour (`ee7852dc50`) | 5,581 MiB anonymous, 1,376 MiB of it swapped; the heap 2,008 MiB free of 5,690 MiB taken; 46,822 image loads on 9,552 files; 994.5 s against the desk's 89.6 | the texel path and the heap, below |
| handheld, Repack tour (ahead of `3ab057cd39`) | 859 content takes refused with usage 0.9–1.7 GiB under a 6,144 MiB budget, 78–92 MiB free in pieces | the ceiling, then fragmentation, below |
| handheld, Repack tour (`8c032f6e00`, `2026-11-17-handheld-tours-and-store-forms.md`) | 331 takes refused 1.1–2.1 GiB under the budget, beside 3 GiB of rest; at the end 6,085 MiB resident, 3,202 MiB swapped, the heap 4,642 MiB free of 9,364 MiB taken; 62 minutes | the ceiling, the room's arithmetic and the heap, below |
| handheld, PBR tour (`8c032f6e00`, the same note) | aborted in leg 3 on a sprite light bake whose source did not stand; until then the texture pool full with the process 1.1 GiB under its budget | the ceiling, below; the abort is item 8's |
| desk, Repack tour, heap peak 6.17 GiB (`2026-11-16-host-memory-holders-repack.md`) | 1,141 MiB cell stores, 943 MiB `MeshTable`, 443 MiB the mirror, 345 MiB Bullet shapes, 275 MiB the store's resident entries, 254 MiB navigation meshes, 171 MiB keyframes, 157 MiB `NifOsg::Loader` | the geometry and the game's own, below |

**The texel path reads a texture's source file to find a chain the store already holds.**

- **The store's key is a digest of the source's texels, and the host no longer keeps them.** The worker fetches them
  before any lookup (`preparedimages.cpp:246`, `chainpass.cpp:40-60`) and the uploader drops them once the device has
  its copy (`sceneuploader.cpp:375-380`), so every arrival and re-arrival decodes the source through OpenSceneGraph and
  only then reads the store. "The store is the game's input" does not hold on a hit, and `ContentStats::mKeyMs` leaves
  the read out.
- **A file is decoded whole at four separate moments**: to make its header (`imagemanager.cpp:211-232`), once for each
  fact asked (`threadcontent.hpp:106-123`), to key its chain, and at every later hand-over. On the desk the first three
  share one cached decode; on the handheld the budget stands below what is held, an unheld entry lives about a second,
  and the count is 4.9 loads a file. *Inferred*: some 200 GiB read and copied over that tour, the leading candidate for
  its 994 s.
- **A fact's first ask reads the file on the asking thread, the frame's included** (`materialresolver.cpp:542-547`),
  against "Refused: a file read on the frame's thread".
- **A chain read for a slot that left is held for the session**: `letGo` keeps an entry not yet handed out
  (`preparedimages.cpp:155-169`), its job still runs, and the sweep waits for a file object that never dies, since
  `ImageManager::mHeaders` keeps every header (`imagemanager.hpp:71`). No bound in bytes; how often is not measured.
- **Whether a stored chain is looked up depends on what making it would cost** (`preparedimages.cpp:263-270`): a 4096
  normal map needs some 150 MiB available to be allowed a 22 MiB read.
- **A "coarser" chain is two files for ever**: the store keeps levels 1 down and level 0 comes from the source
  (`chainpass.cpp:135-138`), whose own lower levels are read and unused. The replacers' `_spec` maps are of this form.
- **Every arrival's buffers are allocated on a worker and freed on the frame's thread**, and each worker frees its
  scratch whenever the queue drains (`preparedimages.cpp:279-283`).
- **A source is read 8 KiB a call and copied twice** (`constrainedfilestreambuf.cpp:26-41`, no `xsgetn`): 2,731 reads
  for a 22 MiB image.

**The geometry stands three times on the host, and the ring loads what cannot stand.**

- **Three copies**: the template's arrays, which the reader pins; the reader's unpacked copy at 32–60 B a vertex
  (`prepared.hpp:235-241`), read once at adoption (`held.cpp:54`) and kept for the model's whole lend; and the mesh
  table's at 36 B (943 MiB). *Inferred*: the reader's is 250–350 MiB of the mirror's 443 MiB.
- **Every model of every cell in the disc is read, adopted, uploaded and built**, about 450 cells, not 16
  (`cellgrid.hpp:20`, `cellring.cpp:532`); the size rule is applied only at placing (`cellplacer.cpp:740-757`). At the
  default a model under 82 units stands only within a cell of its own. How much of the ring never stands needs a
  count; it bears on the mesh table, the mirror, the pinned templates and the device's structures at once.
- **The mesh key, the packing and the bounds are made on the main thread at adoption** from arrays the reader walked
  (`mesh.cpp:7-26`, `meshtable.cpp:132,182-203`); a split normal is packed, stored, unpacked and packed again.
- **The ring's pools keep the largest tenant each object ever held** (`held.hpp:187-197`, `prepared.hpp:145,407`), and
  a `Placement` is 128 B of which 4 are its own (`held.hpp:80-103`).
- **The ground's indices and coordinates, one list for every cell, are copied and digested a cell**
  (`cellplacer.cpp:382-387`): 129 KiB a land cell on the host and again on the device.

**The device leaves its budget unused, and counts room where it cannot be taken.**

- **Content's ceiling keeps free a second copy of everything essential, and of every structure as well for textures**
  (`memory.cpp:455-469`). The stated reason, a table that grows by making itself again, holds for megabytes of tables;
  the geometry and the structures' blocks are appended, and a rebuild drains the old world first. Resting meshes'
  structures lower the ceiling too. At `8c032f6e00` the handheld stood 1.1–2.1 GiB under its budget at every one of
  373 refusals, under the replacers with its texture pool full; the split of that gap by use is *inferred*, since a
  refusal does not log it.
- **Since `84a60139e8` room is counted across two storages**: a structure block is 64 MiB of its own, outside
  `getContentFree` (`memory.cpp:471-486`), so an evicted mesh reads as use and lowers the rest limit by nine tenths of
  what it freed (`sceneuploader.hpp:110-115`), and an arrival's structures are sized against the texture pools' room
  (`sceneuploader.cpp:93-97`). The Repack tour at `8c032f6e00` refused content beside 3 GiB of rest.
- **The texture pool is cut as well as capped**: under the Repack up to 337 MiB stood free in its blocks with no range
  past 4.8 MiB while textures of 5.4 MiB were refused. Under the replacers it was simply full.
- **A texture want never matches a resting texture of its own size**: the hole carries alignment margins the fit does
  not (`memory.hpp:118-121`, `texture.cpp:696`, `restqueue.hpp:204`). **A static mesh refused in the loose storage
  files no want** (`bottomlevelstore.cpp:359`).
- **Geometry is essential, appended and never given back, and a resting mesh pins its runs** (`blockedbuffer.cpp:37-75`,
  `meshtable.cpp:283-295`); the rest counts a mesh's structure alone. *Inferred*: 0.5–0.9 GiB under the handheld's
  7,000 resting meshes. The Repack tour's first place, flown again at its end, held half as many vertices again in use
  and 220 MiB more of live structures for the same instances.
- **A texture's residency is its whole image, sized by the room and never by need** (`texture.cpp:358-493`). A move up
  is a new image and the whole chain read again, up to six tries (`sceneuploader.cpp:120-160`). The shaders already
  compute the level each sample reads (`texturing.glsl:275-299`); nothing records it.
- **Fixed rooms and floors**: tables for 2^18 placements against 64–80 thousand in use, ground masks as floats in
  32 MiB, 128 MiB of structure blocks and 23 MiB of stream blocks for every picture's scene, 85 MiB of spread means
  kept after one PBR load, staging unbounded within a batch (1,435 MiB at a PBR world's build).
- **A world is rebuilt for more than a change of mode**: by raw usage against the ceiling above, by a refusal flag
  never cleared (`texture.cpp:663`), and by the first large picture (`vulkanrenderer.cpp:990-1001`).
- **Every refused take walks every allocation of the content pools** (`memory.cpp:522-532`), the walk "room counted by
  the pools' ranges" was refused for.

**The heap keeps what is freed, and the budgets do not see each other.**

- **Nothing tunes the allocator in play.** Image buffers of 5–22 MiB are allocated on 25 or more threads and freed on
  another. glibc raises its mapping threshold to the first such block freed, up to 32 MiB
  ([`mallopt(3)`](https://manpages.debian.org/unstable/manpages-dev/mallopt.3.en.html)), after which they come from
  arenas that give back only their tops. The Repack tour at `8c032f6e00` ended with 4,642 MiB free of 9,364 MiB taken,
  in 25 arenas of which the largest stood nine tenths free, and 3,202 MiB swapped out; that the threshold is the cause
  is *inferred*.
- **The caches' budget counts as taken what the heap holds free** (`cachebudget.cpp:9-15`), and an insertion evicts on
  its own thread, the frame's included, destroying the value inside the budget's lock (`cachebudget.cpp:75`). On the
  handheld's Repack tour at `8c032f6e00` the caches ended holding nothing of what they read: NIFs were parsed 125,678
  times, 102,080 of them again, templates made 104,753 times and collision shapes 82,823.
- **On unified memory nothing bounds the device by the system**: RADV's budget is its heap less the GPU's use
  ([`radv_physical_device.c`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/amd/vulkan/radv_physical_device.c))
  and does not see the process's heap.

**The game's own holders, upstream's code.**

- **Cell stores**: the tour scales `preload distance` to 4.3 cells (`framed.cpp:111-125`), so every door of every
  active cell is followed; `Scene` loads the destination's references before the preloader's budget is asked
  (`scene.cpp:1231`, `cellpreloader.cpp:265`); and a store once loaded is never let go (`worldmodel.cpp:163-168`).
- **A NIF is hashed whole before it is parsed** (`niffile.cpp:557`) for a value two tools print and the game never
  reads; Bullet's shapes hold some 66 B a triangle; the navigator's tile cache is a fixed 256 MiB and stands at it.

**A tour is not a played game**: its door reach covers about 190 times a player's area, its timer caches never expire
(`rtxrenderer.cpp:342-346`), and it runs on huge pages the game does not ask for (`rtxtool/main.cpp:638`).

**Checked and left as recorded**: defragmentation, 16-bit indices, textures shared across scenes, staged host writes,
pageable device memory, one bottom level a template, and one file an entry in the store, whose 86,640 small files cost
a start 0.26–1.48 s.

## Approved

**Decided by the user, 2026-11-17**, each walked through with its options:

- **The stores are made again, once** (item 12): its keys and its entries' format change.
- **Every stored chain is whole** (item 12.3), about 700 MiB more on the desk's three profiles.
- **One planner moves a texture's level** (item 13), in place of the three mechanisms that size a texture today. It
  stands where "a residency manager" was refused with no reason recorded. **Its rest declined at the user's review
  (2026-11-17)**: see **Declined**.
- **The plan reordered by what the handheld measured** (the user's review, 2026-11-17): the longest frames, the ring's
  parses, the device's mesh memory, need; what only tidies a mechanism that works is declined.
- **A resting mesh counts its structure and its geometry, and gives both back** (item 14.6), changing "a mesh counts
  what its eviction gives back" (`a3d689826f`).
- **Levels by distance, a meter first** (item 16, the user's idea and order, 2026-11-17): each texture stands from the
  level its nearest user's distance asks, by the trace's own cone, with the ring asking by cell and band and everything
  else asking level 0; declined past its meter if under a tenth of the handheld's PBR and Repack texture bytes stand
  above their want. Memory first, at the user's word: the desk's frame times are not read while it is busy.
- **What the texture pool strands in pieces is moved, not classed** (item 16, after its step 5), only if a tour after item 11 still
  refuses a texture with room in pieces. Lifts "Refused: defragmentation" for textures; "pools by size class" stays
  refused.
- **The NIF's hash pass stays** as upstream has it: some 15 µs a parse by the files' sizes, an estimate.
- **Upstream's code**, each recorded in `UPSTREAM.md` under **How content is loaded** in the commit that makes it:

  | Change | Why | Cost to a merge |
  |---|---|---|
  | `ImageManager::setModelImages` takes a `Resource::HeaderDeriver` with the `Headers` mode, and `getModelImage` hands it a file's texels and its new header (item 12.1) | the digest and the facts are made there, not by reading the file again | the fork's own lines in upstream's file |
  | `ConstrainedFileStreamBuf::xsgetn` reads a request no smaller than its buffer straight into the caller's memory (item 12.5) | one read and one copy in place of thousands | a conflict with an edit to the stream buffer, already the fork's |
  | `Scene::preloadTeleportDoorDestinations` asks the preloader's budget before `findCell` loads a door's destination (item 10.4) | a skipped preload has still loaded the cell | a conflict with an edit to that loop |
  | `NavMeshTilesCache` stands under `Resource::CacheBudget`, and `max nav mesh tiles cache size` goes (item 10.5) | a fixed 256 MiB, full, outside the budget every other cache yields to | a conflict with an edit to `navmeshtilescache.{hpp,cpp}` or the setting |

  Not proposed, and noted only: unloading a cell store (its changes are a save's), Bullet's double precision, a NIF
  key's unused tangents.
- **No dependency is approved or proposed.** A second allocator (mimalloc) is proposed only if item 10's tours still
  show the heap holding what it freed.

## Decisions

- **A heap profile runs in a cgroup with a memory cap** (`systemd-run --user --scope -p MemoryMax=<GiB>`), stopped by its
  stop count: on 2026-11-17 `heaptrack_interpret` grew to 47 GiB over 37 Repack stops on the desk, and the kernel's OOM
  killer took the desktop down with it.

- **Fixed-width spellings, `std::`-qualified, for every integer.** `std::int32_t`, `std::uint32_t` and their kin from
  `<cstdint>`; `std::size_t` and `std::ptrdiff_t` for sizes, counts, indices and differences; `char` for text, `bool`
  and the floats as they are; `int main`; a library's typedef where the value is its (`GLenum`, `ALuint`, `VkBool32`,
  NVML's `unsigned long long`). As AUTOSAR C++14 A3-9-1 and MISRA C++:2008 3-9-2 require. Refused: Google's `int` by
  default with fixed widths only where size matters, and the Core Guidelines' ES.102, which ask the same — the user's
  call, for one spelling over a judgement each site. Refused: unqualified `uint32_t`, which `<cstdint>` need not
  declare in the global namespace ([cppreference](https://en.cppreference.com/w/cpp/types/integer)).
- **A rename, never a change of signedness.** `unsigned` used to mean "not negative" (ES.106) stays unsigned here;
  changing it changes arithmetic and is its own work.
- **One normalizer, three uses.** One module of `tools/omw` rewrites the spellings deterministically and idempotently,
  outside strings, comments and the GLSL-shared headers: `omw format` applies it, `omw lint` fails a file it would
  change, and `omw merge-file` applies it to the base and both sides before merging, as it already lays them out with
  clang-format. An upstream edit then arrives in the tree's spelling and conflicts only where it overlaps the fork's,
  which is what git's `merge.renormalize` exists for
  ([gitattributes](https://git-scm.com/docs/gitattributes#_merging_branches_with_differing_checkinmerge_attributes)).
  Refused: a clang-tidy check, which none of LLVM's is (`google-runtime-int` takes `short` and `long` alone) and which
  is too slow for a merge driver.
- **Includes grouped by clang-format, not by hand.** `IncludeBlocks: Regroup` with the five blocks as categories and
  `IncludeIsMainRegex` taking the `posix`/`win32` pairs, so `omw format` writes the order and the gate holds it; the
  merge driver regroups upstream's side. An include whose order matters stands after a comment saying why, which ends
  clang-format's block, as AGENTS.md asks. Refused: `llvm-include-order`, whose order is LLVM's and fixed.
- **Measured first, and an item its measurement does not bear out is declined.** Most magnitudes above are read off
  code and one earlier run; the tours of `2026-11-17-handheld-tours-and-store-forms.md` bear out the ceiling and the
  heap.
- **A file's digest is made once, where its texels are first read, and a key is made from digests**, as the Remote
  Execution API keys an action by its inputs' digests and never by their bytes again. The header keeps it for the
  session. Refused: a key remembered for each use of a file, which still hashes the file once a use; the preparation's
  stamp trusted in play, under which a lying stamp is a wrong picture where it is now a miss.
- **What the renderer derives of a file is derived at that first read**, on the thread that read it, by a deriver the
  image manager is given with its `Headers` mode, so none exists without it; as OpenSceneGraph hooks every read
  through its registry's `ReadFileCallback`. The facts the store's memory does not hold are made then and filed, and
  the frame's thread finds them on the header. Refused: the digest made by the header alone, under which a fact's first
  ask still reads the file.
- **A stream reads a large request straight into its caller's memory**, as libstdc++'s `basic_filebuf::xsgetn` reads
  directly where the request is past its buffer. Refused: a larger buffer, still two copies of every byte; positioned
  reads for the renderer beside the game's streams, two routes to one file.
- **Every chain the store keeps is whole, with a digest a level**, by the user's decision: one entry is everything the
  device takes, and a
  range of its levels can be read and checked alone, as KTX 2 indexes its levels for streaming
  ([spec](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html)). The coarser chains kept whole add about
  700 MiB to the desk's three profiles, a twentieth of the store. Refused: the coarser form, a second file for ever;
  one digest over the payload, which a ranged read cannot check.
- **Declined with item 13's planner, and kept for a reader of levels if one is ever proposed again.** **A texture's
  levels are read by position, from where they stand, into a buffer the reader owns**: the store's entry
  for a made chain; the file's own bytes for a block-compressed file that stands as shipped, through the stream the
  game opens it as. The buffers are a bounded pool by size class, so a hand-over allocates nothing and a reader waits
  where the pool is empty, a worker only and never the frame's thread, as DirectStorage reads into a bounded staging
  buffer
  ([guidance](https://github.com/microsoft/DirectStorage/blob/main/Docs/DeveloperGuidance.md)). Refused: a shipped
  file copied into the store, the disk twice; reading into staging, whose memory is write-combined and which the digest
  must read back; a map, since nothing is kept once staged.
- **A read nobody waits for is cancelled**, and what is read and not yet handed over is a pool buffer, so it is bounded
  by the pool.
- **Content's ceiling keeps what essential memory will grow by, not what it holds**: the two types essential memory
  grows through, the doubling table and the blocked stream, state their own next step, so no owner can forget to, and
  the ceiling is the budget less the frame's gap and their sum.
  Refused: the rule as it stands, a second copy of memory that is appended and never made again; no reserve, under
  which a table's growth could end the renderer.
- **Room is counted where it can be taken**: free bytes a storage, a structure storage's own free runs among them, and
  a want is met by a resting row whose own requirements hold it. Corrects "what waits is given the resting row that
  surely leaves it room", whose arithmetic predates the storages standing apart.
- **A mesh is its structure and its geometry, counted and given back together**, by the user's decision: a resting
  mesh counts both against
  the limit, a stream's block no run touches goes back to the device, and geometry is taken as content, refused with
  its mesh. Changes "a mesh counts what its eviction gives back", whose reason holds for an evicted mesh and not for a
  resting one.
- **Declined past its first part (item 13).** **A texture stands at a level, and one planner moves it**, by the user's
  decision: down by a copy on the device, up
  by a read of the levels it adds. RTX Remix makes a new image at the wanted levels and swaps the view
  ([`rtx_texture_manager.cpp`](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_texture_manager.cpp)),
  and Unreal lowers a plan a level at a time until it fits
  ([streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine)).
  It replaces the side chosen at arrival, the short list and its retries, and the world rebuilt for a reserve that
  moved. Where the plan fits, everything stands whole, as now. Refused: sparse images, whose binds cost NVIDIA's Linux
  driver a quarter of a millisecond a page; `VK_EXT_image_view_min_lod`, which clamps a view and frees nothing.
- **Under pressure the level wanted is the one rays sampled**, by the user's decision and only past its meter: the
  trace records the finest level asked of each slot,
  read back two frames late off the frame slot's own copy, its peak held and let go only under pressure, as Remix
  holds it sixty frames. What rests goes first, then what stands above its want, then a level below want, the largest
  first. Refused: distance to the eye, which says nothing of a reflection or a bounce.
- **What the texture pool strands in pieces is moved, not classed**, by the user's decision: where a want finds no
  hole, the allocator's incremental defragmentation names a bounded set of textures and the planner moves each at its
  own level, as VMA spreads its passes across frames and leaves the recreating to the application
  ([defragmentation](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/defragmentation.html)).
  Lifts "Refused: defragmentation" for textures: the move's cost is the planner's already, and no structure stands in
  a texture pool since `84a60139e8`. Refused: pools by size class, which strand a class's free slots beside another's
  want.
- **The geometry has one host copy, for one hand-over**: the reader makes it in the device's form with its key and
  bounds, adoption copies it into the arrivals' store, and both let go, as Unity frees a mesh's system copy once
  uploaded ([`Mesh.UploadMeshData`](https://docs.unity3d.com/ScriptReference/Mesh.UploadMeshData.html)) and Unreal
  drops a static mesh's unless asked
  ([`bAllowCPUAccess`](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/Engine/UStaticMesh/bAllowCPUAccess/)).
  The ground's masks follow the same rule. Refused: the copy kept and trimmed to the live runs, which keeps the
  duplication on unified memory.
- **The harness digests a mesh's shape only where the host asks for digests**: nothing else reads it. Refused: the
  digest made for every mesh the game adds; reading the device's buffers back.
- **Declined with item 14.4.** **The ring's records are flat, and a reference states once what is its own**: one buffer for the ring with a run a
  cell, a row a reference and a slot a part. Refused: pools that keep what each object grew to.
- **The ring reads what can stand at its distance**: a cell's references are read largest first as far as the size rule
  reaches one band ahead, and further as the cell nears; a model's radius is the preparation's to state. Refused:
  reading everything in the disc, most of which stands only within a cell or two.
- **A large buffer never reaches the allocator, and the mapping threshold is pinned**: pinning it turns glibc's own
  adjustment off (`mallopt(3)`), so a block past it is mapped and given back whole, whichever thread frees it. Its
  value is 1 MiB, with glibc's arenas capped at four (`Platform::tuneHeap`), the setting that left the heap least free
  memory on both machines' Repack tours and took 0.86 GiB off the handheld's most
  (`research/2026-11-17-handheld-allocator-tunables.md`); `GLIBC_TUNABLES` naming the allocator overrides it. Refused:
  a trim on a timer, which walks every arena to give back what should never have entered one; the 256 KiB and 4 MiB
  thresholds, which left it 830 and 774 MiB free against 683; huge pages off, which swapped a GiB more.
- **The caches' budget counts what the heap holds free as room, and an evicted value dies off the lock and off the
  frame**, on the worker that derives the budget, where upstream had put its own expiry.
- **A preload asks the budget before anything of it is loaded, and the navigator's tiles stand under the one budget**,
  by the user's decisions: a door's destination is not loaded for a guess the budget skips, and the tile cache has no
  cap of its own, its tiles being a read away in the tile store. Refused: a cap derived once at a start, the fixed
  share the loading design refused. **The NIF's hash pass stays**, by the user's decision: its cost is some 15 µs a
  parse, and the parsing again that multiplied it ends with the caches' thrash.
- **On unified memory the device's budget is the lesser of the driver's and what the system can still back**, with a
  smaller reserve than the caches', so the caches stop first, as decided. Windows says of such an adapter that it has
  one pool ([residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency)). Refused: one order
  across host and device, as before; the kernel's pressure stall figures, which report a stall once it has happened.
- **Every hand-over's device work has a stated bound**: what it stages. A world's build flushes at the bound, and an
  arrival past it waits a hand-over, as content without room already does. Not the arrival scheduler that was refused:
  nothing is ordered or threaded, a hand-over stops at its bound.
- **A run that reports memory runs as the game ships**: no huge pages, the shipped expiry, the shipped door reach.
  Timing runs keep theirs.
- **Not now**: a model's parts and meshes kept in the store in the device's form, the cooked asset every engine ships,
  until item 15's count says what the reader's parse still costs; host image copy on unified memory; positions on a
  16-bit grid, which cracks where meshes meet.

- **A device whose memory is the system's is told by its type**, `VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`, a property
  every driver states and no vendor's name: its budget is held to what it holds and what the system leaves, less a
  gigabyte, read once a second at most. Refused: a heap that is both device-local and host-visible, which a discrete
  card with resizable BAR states too.
- **The room kept for the next arrival is the most of the last 64 extensions**, some three crossings at a tour's speed,
  as a sliding maximum and not the session's, which one heavy arrival held for good.
- **A load's staging is flushed between two whole files once it reaches the kept size**, its texture work recorded
  first: the copies are recorded a phase at a time for the whole run, so a flush between two stagings without the
  record would hand a staged block back before its copy ran. The records past the first are untimed, as one frame
  opens each zone once.
- **What a slot that leaves was read for is forgotten for that slot's ask alone** (`PreparedImages::forget`), as the
  key the slot asked by, and kept where a slot asks again before its job lands: `letGo` serves every slot of a file at
  once, after the device has its copy, and would drop what another slot still waits on.
- **The heap's free memory is read with the system's, once a second off the frame** (`malloc_info`, glibc's one report
  every glibc writes), and counted in the caches' budget: the release builds run on glibc 2.28, which has no
  `mallinfo2`, and `mallinfo` wraps past 2 GiB.

- **A file's key is every level as described, read as data** (`Rtx::hashFile`), one key every image pass adds its own
  inputs to: a chain reads a colour's own levels, so a key of the finest alone could not serve it. An image the
  description refuses as data is keyed by OpenSceneGraph's bytes, since another encoding may describe it: two such
  files never meet under one key. Refused: each pass hashing the texels it reads, which read the file to key what the
  store held ([Unreal's derived data](https://dev.epicgames.com/documentation/en-us/unreal-engine/derived-data-cache)
  keys a texture by its source's hash taken once).
- **The derivation runs on the loader's thread, on a pool of half the processors' preprocessors**, each giving its
  texel scratch back after each file; a loader past the pool waits. Refused: a preprocessor kept a thread, `thread_local`,
  which is global state the threads' owners cannot bound.
- **A run's read of a header is the upload's** (`ChainPass::Input::mRead`): a miss reads the file once for the run and
  the upload, and a whole chain found reads it never.

## Final shape

Sketches of what items 8–17 leave; names are the plan's to settle.

```cpp
// components/resource: a file's identity, beside its size and format on the header; and who derives from its texels.
struct ImageDigest { std::uint64_t mLow = 0; std::uint64_t mHigh = 0; };
struct HeaderDeriver        // handed to setModelImages with ModelImages::Headers; the renderer's makes the digest
{                           // and the facts, on the loader's thread, the one time the texels are read for a model
    virtual void derive(const osg::Image& texels, const ImageHeader& header) = 0;
};

// components/contentstore: an entry read in part. A chain's parts are its levels, finest first.
struct PartRange { std::uint32_t mFirst = 0; std::uint32_t mCount = 0; };
class Store
{
    // The parts' table and the trailer in one read; the range in a second, checked part by part.
    Result<PartTable, Missing> open(const Key& key);
    Result<std::size_t, Missing> read(
        const Key& key, const PartTable& parts, PartRange range, std::span<std::byte> into);
};

// components/rtx/preprocess: replaces PreparedImages' reading half. A miss still goes to the passes.
class BufferPool            // by power-of-two class; a stated bound in bytes; take() waits; give() from any thread
// LevelReader and LevelSource: declined with item 13's planner.

// components/rtx/scene
struct PackedMesh           // spans in the device's form: positions, normal words, coordinates, colours, tangents,
                            // indices; with its MeshKey and bounds, as the reader made them. A stream the mesh
                            // does not bring is an empty span from the start, whatever item 17 decides of the device
class MeshArrivals          // one flat buffer a stream, a start for each arrived slot, cleared after the hand-over,
                            // given back past its bound
// TextureLevels and TexturePlan: declined (item 13); a texture's level moves by makeRoom and the waits.

// components/rtx/mirror/cells: one RunBuffer each for the ring, a run a cell (declined, item 14.4).
struct HeldReference        // model, first part, place, RefNum, gate, radius, flags: what Placement repeated a part
// and std::uint32_t slots, one a part; a placement's transform is made where it stands

// components/rtxvulkan/device/memory
enum class MemoryUse : std::uint8_t { Frame, Essential, Mesh, Texture };   // Mesh: structures and geometry
class MemoryAllocator
{
    void oweGrowth(GrowthOwner owner, VkDeviceSize bytes);       // called by GrowableBuffer and BlockedBuffer alone
    VkDeviceSize getFree(MemoryUse use) const;                   // the texture pools', or the structure storages'
};
```

Gone: `PreparedImages::mEntries`' holds, `ChainForm::Coarser`, `rebuildsWorld` and `wasHeldBack`, `MeshTable`'s
arrays, `PreparedModel`'s arrays past adoption. Staying, their items declined: `ShortTexture`,
`SceneUploader::TextureWait`, `TextureArray::chooseLevels` (item 13); `Placement` and the cell pools (item 14.4).

## Plan

**The upstream changes of items 1–5 and 7 are approved by the user (2026-11-16)**, `extern/osg-ffmpeg-videoplayer` and
the unstarted rasterizer included; item 6's fixes to upstream's code fall under the standing approval of `.clang-tidy`
over upstream's code in `UPSTREAM.md`. Each is recorded there under **One standard for the tree**, in the commit that
makes it, with its reason and cost. **The upstream changes of items 10 and 12 are approved by the user (2026-11-17)**:
the table under **Approved**, each recorded under **How content is loaded** when it is made. An upstream change the
plan does not name is still proposed first. Windows-only lines (`_WIN32` branches, MSVC pragmas) are checked by
Windows CI, which the branch's pushes start.

**Where it runs.** Three machines, each named once here:

- **The desk**, where the run works and builds.
- **The handheld**, `deck@192.168.0.31`: `./omw release remote deck@192.168.0.31 <verb>` builds in the release
  container, stages the tree at its own path there and brings the run's files back. Its kernel gives huge pages to
  every mapping (`transparent_hugepage` is `always`; the desk's is `madvise`), which a report's `hugePageShare` states.
- **The Windows machine**, `192.168.0.8`, over SSH: its tree at `%USERPROFILE%\Projects\openmw` takes the commit under
  test, `omw.cmd` is the driver, and a run that draws is started in the logged-on session by an interactive scheduled
  task, since a process SSH starts has no surface (`research/2026-11-01-windows-tests-over-ssh.md`). It holds the three
  contents' profiles and a release build of `8c032f6e00`, its gate clean there.

A machine that does not answer is waited on for an hour, then its leg is recorded as not flown and the item goes on;
item 18 does not close without all three.

**What an item owes.** Items 1–7 move no frame, so `./omw gate` and CI are their check; `repeat`, `shot` and the
benches are not owed. From item 8 on, a change to what a frame reads owes `./omw repeat --pairs=10` and `./omw shot
--views=all --map --upscale=off --against`; a shader change owes `./omw kernels --against` on the desk and under the
drm-shim; a hot path owes its allocation test; each item ends with the tours it names. Vanilla's pictures do not move
in any item.

### 20. A script finds a reference without walking every cell

**Approved by the user (2026-11-18)**, an upstream change, recorded in `UPSTREAM.md` in the commit that makes it.
`research/2026-11-17-longest-frames.md` shows `World::searchPtr`, `WorldModel::getPtrByRefId` and `WorldModel::getAll`
leading the worst frames on both machines: 288 ms on the handheld's PBR tour, 1.9 s on its Repack one. A lookup visits
every reference of every loaded cell store, and no cell store is ever unloaded, about 3,065 after a Repack tour's first
legs.

- **The defect**: a loaded `CellStore` answers whether it holds an ID only by visiting its references
  (`CellStore::search`, through `updateMergedRefs`). A preloaded one answers by a binary search of `mIds`.
  `CellStore::hasId` for a loaded store returns the inverse of its search, unreached today since its one caller asks
  it only of a preloaded store.
- **The design, to be checked against upstream's own history before writing**: a loaded store keeps its IDs counted,
  kept where a reference enters and leaves (load, insert, a move in or out). `hasId` answers from that count in every
  state, and `getPtr`, `search` and `getAll`'s walk ask it first. The order cells are asked in stays, so
  `getPtrByRefId`'s rule for an ambiguous ID (the exteriors in reverse, for `chargen_plank`) stands, and the answers
  are the same references as before.
- **Tests**: upstream's suites, plus a store answering an ID after an insert and after a move out, and a lookup over many
  stores visiting only those that hold the ID.
- **Measure**: the handheld's tours, their longest frames against `research/2026-11-17-longest-frames.md`.

### 19. Device memory made off the frame

Item 8's handheld PBR profile at `5db3a1fbf7`: the frame thread spent 938 samples at 499 Hz (1.9 s) of the tour inside
`vkAllocateMemory`, the kernel clearing and remapping the pages each new allocation takes, and up to 38 ms of a
100 ms window. That cost leads four of the tour's ten longest frames: 160–176 ms, place-led. The desk's Repack profile
holds about 100 samples of 429k there. Three callers ask for it:

- the normal-spread means, made to each arrival's largest group exactly (`Growth::Exact`), and let go once past the
  room, so every map past the room makes and frees its own means (451 samples);
- the content pools' new blocks under `Texture::standFile` (273);
- the staging ring's blocks past `sStagingKept`, which it sheds again a block a frame (123).

Allocating on the frame path is what Vulkan's vendors advise against: NVIDIA's
[dos and don'ts](https://developer.nvidia.com/blog/vulkan-dos-donts), and Samsung's
[usage recommendations](https://developer.samsung.com/game/usage), which say to reuse rather than allocate per frame.
The design is taken when the item starts, and must hold a device total no larger than today's peak. A test asserts
that an arrival whose largest group fits what the means already hold makes no new means. Tours: on the
handheld's PBR tour, none of the ten longest frames is led by `vkAllocateMemory`, and the means and the ring are gone
from its callers.

**Written** (the commit after `f61cb7c291`):

- **The means grow by doubling within the room** (`meansGrownTo`), so a session makes them a few times. Before, they
  were made at each run's largest group exactly. A map past the room still gets means of its own, given back after its
  run, as the room bounds what the means hold for the session.
- **A spare staging block is given back only once it has stood unused for `sStagingIdleSubmits`**, 600 submits. That
  is jemalloc's decay rule for unused pages (`dirty_decay_ms`, ten seconds by default). Before, a block shed a frame
  after the crossing that grew it was made again at the next.
- **The content pools' new blocks are left as they are**: they are growth, not churn. The library keeps one empty
  block before it frees another, and item 16's standing by want shrinks what they hold.

Its tours are flown with items 20 and 23–25's.

### 16. Levels by distance

The user's idea (2026-11-17): distant land never loads its finest levels, the further the smaller, and only the
nearest cells load level 0. This is the canonical texture streamer's shape. Unreal's
([overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine),
[build](https://dev.epicgames.com/documentation/en-us/unreal-engine/building-texture-streaming-data-in-unreal-engine))
keeps a UV density per mesh and a UV channel and scale per material texture. From them it works out each texture's
wanted level, the nearest user deciding, and streams levels in and out against a pool. Here it holds by the trace's own
rule, not by a guess: a hit reads the level its ray cone resolves (`texturing.glsl`, `coneBaseOf`). A secondary cone
opens no slower than the primary one (`shading.glsl`, `water.glsl`), and starts as wide as the footprint it leaves
from. So the triangle inequality bounds every read at a point from below, by the primary ray's level at the eye's
distance to it. The finest level any read takes is the head-on one along the short axis, which anisotropic filtering
reads, so the facing term is left out. The bound is conservative, and a picture moves only where a ray reads finer than
it.

**What the investigation found the design must be** (`5db3a1fbf7`):

- **The need is computed where the content is placed, and once per change, never by a scan per frame**: the frame
  rule ("Frame times are uniform"), and how Unreal registers primitive bounds as they come and go. Every placement
  states who stood it (`Stander::Walk`, `Stander::Ring`). The walk's placements are the active grid's, which the user's
  "only the nearest cells load level 0" leaves whole. The ring's are held by cell (`CellPlacer`), and the ring already
  measures every held cell against the eye once a frame. So the ring asks by cell and band, and everything else asks
  level 0.
- **Asks attach to materials, and textures derive their want from them**, mirroring the hold chain (placement →
  material → texture, `MaterialTable` holding what a material names). A material's textures change after its placements
  stand: a chunk's composite slots arrive later (`CompositeQueue`), and `setMaterial` rewrites rows. An ask stored per
  texture would go stale there, while an ask per material moves with the row.
- **A band per doubling of distance, measured from the cell's nearest point**, so a cell's every ask moves by exactly
  one level when its band changes, and only the cells crossing a band's edge do any work. Each cell asks the level of
  the band one nearer than its own, so its levels arrive before the eye does, as the ring reads a band ahead
  (`CellRing::keptTo`). A move down waits for pressure, so receding costs nothing and no hysteresis is needed.
- **Every term is the shader's own**, rounded to the finer side at each step:
  - the mesh's finest texture-to-world area ratio per coordinate set, over its triangles, the `uvArea / cone.mArea`
    term;
  - the placement's scale;
  - the material's texture transform for that unit, the `uv * xy` term;
  - the texture's own resolution;
  - the frame's pixel spread at the render size;
  - `mLevelBias`.

  A degenerate triangle reads level zero in the shader, so it makes the mesh's ratio infinite here too. An environment
  map is read by direction, not by coordinates, so it asks level 0.
- **The ground's bake is a reader too**: a chunk's composite is summed from its layers on the device. A chunk's layer
  textures therefore ask the level the composite's texel reads at, and the composite slot asks by distance like any
  texture.
- **No new mechanism moves a level.** The arrival's first level (`TextureArray::chooseLevels`) starts at the want. A
  texture whose want moves finer than it stands is filed as a wait, as a short one is (`SceneUploader::TextureWait`,
  handed over whole, "whole" meaning from its want). `makeRoom` moves down what stands above its want first, by item
  13's copy.

**Progress, 2026-11-18**: step 4 is written as well. The handheld's PBR tour at `43cf84a1b0` read 28.2% of what
rested above its last want (486 of 1,722 MiB), past its tenth. The device peak barely moved (5,780.6 MiB against
5,786.6), since the rest fills what the budget leaves. Step 4 keeps more in the same rest: a texture's last want
stays on its row (`mLastWant`), the device moves what rests above it down first (`makeRoom` by `readersWant`), the
scene keeps it resting smaller (`TextureTable::restandResting`), and the uploader evicts only what a hand-over's
move cannot give back (`SceneUploader::sRestShrinkMost`). Steps 1–3 and 5 were written and tested earlier (`66aa444291`, `02ffdefad9` and the commit after). Step 4 and the
tours of step 6 remain. The meter at its peak: the handheld's PBR 23.2%, its Repack 17.0% (that tour was cut at stop
135 of 155 by its lost SSH session, no crash recorded), the desk's PBR 23.2%. All clear a tenth. Step 5's `shot
--against` moved none of 74 pictures, and `repeat --pairs=10` agreed.

- **Whole is from the want**, in one place: `TextureData::topLevel` replaced `firstLevelWithin`, so the arrival's
  first level, a short texture's whole bytes and what waits all start from the want.
- **A want finer than what stood arrives again** at the scene's `compact`. `makeRoom` moves what stands above its want
  first, and such a move is named `MovedTexture`, not short, so nothing hands it over again.

- **The direction was turned in the writing.** A ray reads finest where a mesh lays the least texture on a unit of
  surface, so the density kept is the least over the triangles, not the most.
- **Two kinds of hold.** The identity holds a walk keeps on a texture read nothing (`MaterialResolver::takeTexture`).
  A holder that reads a texture itself (the sky's sheets, an emitter's sprite and its light) takes a `ReadingHold`,
  which asks level 0.
- **The composite's size moved into the core** (`GROUND_COMPOSITE_EXTENT`, `scene.h`), as a chunk asks its layers the
  level its bake reads them at.

The steps:

1. **The meshes' UV density**: `MeshTable` computes, at add and once, each mesh's finest texture-to-world area ratio per
   coordinate set. Tests: hand-computed ratios for a quad stretched each way, a degenerate triangle, and a second set
   apart from the first.
2. **The want**: every texture slot's first level wanted, in the scene (`components/rtx/scene`), from:
   - each material's asks, ring cells counted by level and walk placements as level 0;
   - direct holds (sprites, sky, emitters) as level 0;
   - the material's units, each through its transform and coordinate set.

   The ring keeps each held cell's per-material finest ask, made at adoption from its placements, and re-asks a cell
   whose band moved. Counts change by events, never by a pass over the table. A change list names the slots whose want
   moved finer. Tests: a far cell asks a coarse level and a near one level 0; a texture a walk placement also uses wants
   level 0; a band crossing moves the want by one; a material rewritten moves its asks to its new slots; a hot path that
   allocates nothing once warm.
3. **The meter, which moves nothing**: the memory report states the bytes standing above their want, and those
   resting above the want their last readers asked. It is flown on the handheld's PBR and Repack tours. **Judged at the
   peak** (the user, 2026-11-17: the worst stop is what memory runs out at, as the frame's worst is the metric): under a
   tenth of the textures standing at the worst stop on both, step 4 is declined. The desk's PBR tour at `66aa444291`
   measured 451 of 1948 MiB, 23.2%, at the first land leg; the median stop 5.4%, every stop together 6.1%.
4. **Resting at the last want**, apart from step 5: a texture laid to rest is moved down to the level its last readers
   asked, by the device's copy, so the rest holds several textures in the room one held whole. The resting half of the
   meter is its measure, and it has its own tenth. The desk's rest reached about 7 GiB at the same tour's peak, four
   times what stood named.
5. **Standing by want**:
   - `SceneTextures` lays a description from its want, so a far arrival stages and stands only its small levels;
   - `chooseLevels` starts from the want;
   - `DeviceAnswer`'s short is a texture standing coarser than its want;
   - the waits hand over again a texture whose want moved finer;
   - `makeRoom` moves down what stands above its want first, then the largest.

   Tests: an arrival stands from its want; a want moved finer is handed over again whole from it; a room short moves
   down the texture above its want before a larger one.
6. **Checks and tours.** `shot --views=all --against` moves no picture, a shadow ray's cutout aside, and `repeat
   --pairs=10` agrees. Tours of the three contents on the handheld show the device's texture bytes and the refusals
   against item 8's.

Defragmentation is item 21's, by the library's own incremental passes. The GPU's feedback meter is declined unless step 6's pictures move: the bound is derived, so its
promise is checked by the pictures, not measured by a second path.

### 21. Fix: the texture pool defragments, incrementally

**Closes**: "the pool refuses small resources while it has room free in pieces". Takes over item 16's conditional
defragmentation. At the handheld Repack tour's mesh refusal (`5db3a1fbf7`), the texture pool held 793 MiB free in its
blocks, its largest range 12.4 MiB. A whole block emptied is room a 64 MiB structure block can take too, so item 14.6
follows it and runs only where refusals remain.

- **VMA's own incremental defragmentation**, over the texture pool alone (`vmaBeginDefragmentation`,
  `vmaBeginDefragmentationPass` with `maxBytesPerPass`,
  [VMA, "Defragmentation"](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/defragmentation.html)):
  the library plans the moves, and each image is copied on the device by item 13's copy, at its own level.
- **Structures are not moved.** One moves only by `vkCmdCopyAccelerationStructureKHR`, every instance naming it written
  again. The structure storage gives back blocks nothing uses (item 14.6) instead.
- **When**: a pass runs only where a take was refused while the pool's free bytes would have held it. It is bounded by
  what one hand-over stages (`sMoveCopyMost`), so no frame absorbs a whole defragmentation: the frame rule.
- **Refused**: pools by size class, which the architecture already refuses; and a full pass at load, which strands
  again by the next crossing.
- **Tests**: a pool cut into pieces takes, after its passes, the image it refused; a frame whose takes fit runs no
  pass; a pass never copies past its bound.

### 14. The device's mesh memory

6. **After item 21, and only where the tours still refuse a mesh for room** (the Repack at `f45fb184d7` refused three 64 MiB structure
   blocks): `MemoryUse::Mesh`; a block no run touches given back, runs taken lowest first so blocks empty; the rest
   counting geometry; and a want for a mesh refused in the loose storage, which a block emptied answers and no resting
   mesh's fit does (item 9).
8. Tests: a block no run touches goes back, and a mesh refused in the loose storage stands once one empties. Tours:
   no mesh refused for room on the Repack.

### 15. The ring reads what can stand

0. **Measured first**: item 8's count of models parsed and parsed again on the handheld's Repack tour at the head, and
   what the parse costs the loading threads in its profile. Declined if the parses again are under a tenth of all.
1. A cell's references read as a prefix by radius, one band ahead of the size rule, the radius the manifest's;
   `CellRing` asks the reader to go further as a cell nears. Written on the ring's records as they stand.
2. Item 8's count again; `repeat` and `shot`, since a model must stand before the frame that first shows it; and no
   outrun on the legs the baseline outran.

### 22. Fix: a texture's want matches a rest of its own size

**Closes**: "a texture's want never matches a resting texture of its own size".

- **The fit states the range it leaves**: its size and the alignment it begins at (`FitBytes`), and the want states
  its size and alignment. `RestQueue::firstFitting` takes the first rest whose range holds the want's size at the
  want's alignment. That is exact, so no margin is needed.
- **The granularity margin goes where it cannot apply**: a texture pool holds optimally tiled images alone, which
  `bufferImageGranularity` never separates (Vulkan spec, "Buffer-Image Granularity"). The pool asserts that it holds
  images alone.
- **Tests**: a want matches a rest of its own size and alignment and no smaller one; a coarser alignment fits a finer
  want, and not the other way.

### 26. Measure, then fix: Windows says what holds the process's memory

**Addresses**: "on the Windows machine the tour's process holds far more anonymous memory than its heap takes". The
issue closes once the gap is named and either fixed or decided.

- **A meter first**, in `components/platform`'s Windows file: `VirtualQuery` walks the process's committed private
  regions, sums them by the module or heap that owns them (`HeapWalk` for the CRT's heaps), and the stop report states
  the top holders. On Linux, the same question is `smaps_rollup`'s, which the report already reads.
- **Then**: a holder the fork owns becomes an item; a driver's holding is noted in the research with its figure.

### 28. Measure: the three lint checks' hits in the fork's own code

**Narrows**: the issues of `cppcoreguidelines-avoid-non-const-global-variables`, `misc-no-recursion` and
`google-default-arguments`. Their hits in upstream's code are upstream's style and not defects (the user, 2026-11-18),
so those issues are deleted and nothing of upstream's is changed.

- **Counted over the fork's folders alone**: `components/rtx*`, `components/myguirtx`, `apps/rtxtool`,
  `apps/openmw/mwrender/rtx` and the fork's other files.
- **Then**: a check with no hits there is enabled over those folders in `.clang-tidy`, so CI keeps it at none. One with
  hits gets an issue of its own with the fork's count, fixed in a later round.

### 18. Close

`./omw gate`; `./omw release tour` of the three contents on the desk, the handheld and Windows, held against item 8's.
They pass where:

1. no tour meets a fault, an outrun among them;
2. no leg's worst frame is longer than that leg's at the baseline by more than the baseline's own spread;
3. each of the baseline's ten longest frames on the handheld is gone, or stands proposed with its profile.

The same tours' logs answer what the open issues' last measure asks (the user, 2026-11-18, folded here rather than
flown apart). The Repack's refusals are read by kind and by why: each refusal's reason, the pools' free bytes and
largest range at it, and the resting bytes beside it. Also read: the heap's free share, the bytes swapped, and the
first place flown again against its first flight. Its outruns are read by cause: the cell read late, adopted late, or
refused for room. A cause no item cured becomes an item ahead of this one, and the two Repack issues are rewritten
with what is left, or deleted.

A condition that fails is worked, not logged: its cause is found and fixed, or the item that owed it is moved to
**Declined** with what was found. Then `.notes/ISSUES.md` is rewritten from the tours; the decisions folded into
`docs/architecture.md`, `AGENTS.md` and `UPSTREAM.md`; the `todo.md` items this closes struck; this file deleted.

## Changed

- **23. A bake waits for room as a file does**: as planned. A refused bake or composite is named short with what it
  stands whole (`TextureArray::bakeBytesOf`), and the uploader has its slot arrive again once the room holds it,
  reading nothing. `textures-readable` counts the texture refusals since the stop before it (`StopFacts::
  mTexturesRefusedBefore`). Its issue closes once the handheld's PBR tour draws no stand-in for good.
- **24. The GPU memory tests start drained**: the fixture already drained the graveyard. The fault was the library's
  usage figure, its own tally since the driver last spoke, corrected by the driver's next word at the take. The
  fixture now has the library fetch the driver's figures in `SetUp`. Seeds 44490, 1, 7 and 1234 pass.

- **8, its tail**: the longest frames are explained in `research/2026-11-17-longest-frames.md`. Their upstream cause
  became item 20 on the user's yes, and the renderer's became item 19. Whether a tour runs unsettled, as the game walks,
  moved to `todo.md` (the user, 2026-11-18): changing it mid-plan would fly the baseline twice over.
- **Issues closed on investigation (2026-11-18)**: a texture's arrival reading its file before the store, which the
  header's derived key already ended (`FileKeys::of`); and the cache budget counting free heap as taken, which
  `CacheBudget::derive` already adds back.

- **10.1. The allocator's settings**: pinned in `Platform::tuneHeap` from `Platform::init`, so every program of the tree
  takes them, and left to `GLIBC_TUNABLES` where it names the allocator, which is how a run tries another. The desk's
  tours were flown too, against a fear that four arenas would hold back its 32 threads: as fast, fewer hitches.

- **10.6, 11.4 and 12.7. The handheld's tours at `f45fb184d7`** (`research/2026-11-17-handheld-allocator-tunables.md`):
  none met its bar, each left to the item whose mechanism answers it. The Repack still swapped 2.6 GiB at its end, the
  heap 12% of what it took free, under the fifth past which another allocator is proposed; NIFs parsed again stand
  with item 15. Content was refused room 247–699 MiB under the budget, where 1.1–2.1 GiB stood before item 11; the
  planner of item 13 sizes every texture by that room. A PBR file that stands as shipped is read again whenever its slot
  arrives, which item 13's reader of levels by position ends.

- **14.1. The reader packs, keys and bounds**: `PackedStreams` packs what `MeshReader` read, where its caller lands
  it — a model's streams on the ring's thread, the walk's scratch on the frame's — so the reader's own tests of what a
  drawable reads stay on the arrays it reads. The ground packs on its reader's thread and keeps naming the shared
  corners and triangles. Split normals are words from the read on, as every normal is.

- **14.2 and 14.7, the geometry. One host copy, for one hand-over, and digests where the host asks**: ahead of 14.1,
  since the arrivals take the table's packing as it stands and the reader's packing moves it later without touching
  them. Every stream's digest is made with the triangles' (`MeshDigest`), so `repeat` still names the stream a
  difference landed in. A run of shared indices is brought by the arrival that made it. The ground's masks follow in a
  commit of their own, a mask let go of before its hand-over staged by none.

- **13, its first part. A texture that stands moves down**: `TextureArray::makeRoom` copies a level down on the
  device for an arrival the room does not hold whole and for a reserve a change of mode grows past content, in place of
  building the world again. A move up is the waits' hand-over whole again, not one that reads only the levels it adds,
  which needs the `LevelReader`. Every arrival also moves textures down for what content holds past its ceiling, a
  budget that fell beside it; a reserve that shrinks sets what was refused for room waiting again, structures and
  textures, so a change of mode never builds the world again. The waits and the short list stay as the move up until a
  planner sizes every texture by the room (13.2, 13.3).

- **13, its second part. What moved down comes back** (`579612228d`, its stand-in's ask undone after): three faults of
  the first part. A short met on a hand-over that only placed was lost, so what a reserve moved down never came back
  up; a texture moved while it rested was filed with no file to read again, and now goes at the next hand-over; one
  moved while standing still rested as whole, and now rests on nothing while it waits.

- **12.4. The manifest's radius and light** move to item 15, their one reader: a field no code reads is dead until
  then, and the manifest has a signature of its own, so it is made again with item 15 and not with the store.

- **12.6. The tests**: a re-arrival reads none of its file
  (`aHeaderWhoseChainTheStoreHoldsWholeComesBackWithNoRead`), a stream's bytes through its buffer and past it (with
  `xsgetn`), no allocation a warm hand-over (`RtxBufferPoolTest`), a cancelled job reads nothing; a level range read
  against the decode moves to item 13 with the `LevelReader`.

- **12.5. The pool, the ranged read and cancel**: a made chain is read from the store a segment at a time into a
  buffer of a bounded pool (`Store::open`, `Store::readSegments`, `Rtx::BufferPool`), and given back as its made image
  goes; a job whose slot left before it started is cancelled. The `LevelReader` of a file that stands as shipped
  moves to item 13, which reads a texture's levels by need: whole, its read is the decode it stands in for.

- **12.3, its second part. The kind's folder and a digest a segment** (store format 3): a generic segment table and not
  a level's, as the store knows no levels; `Store::write` takes `ContentStore::Filed`, its fifth parameter folded in.
  `Store::open` and the ranged read wait for item 13, their first reader, rather than stand unread.

- **12.3, its first part. Every chain whole**: a block file's built levels are kept under its finest as the file
  holds it, in the own form, and the coarser form, `TextureData::mCoarser`, `MadeLayout::mFirst` and the prepared
  image's second flag went with it; the chain pass to version 8, the conversion to 5 for its form's renumbered byte.
  The digest a level, the ranged read and the kind in the entry's folder stay with item 12.3.

- **12.1–12.2. The header's digest, and facts at the first read**: no pass's version moved, since no output did; the
  keys moved with the file's key, which files every old entry apart by construction. The solid reach and the solid
  fall are kept in the store now that their key costs nothing, so no pass stands unstored and `sStored` went.

- **11. The device uses its budget**: the doubling tables owe twice their size and the blocked streams and structure
  storages their next block; a scratch or a storage held to a room grows to what its owner asks and owes nothing, so a
  build's 64 MiB scratch does not keep 128 MiB from content. The masks stay floats (item 11.2, declined).

- **9. What is plainly wrong**: each its own commit and test, as planned, but for the room arithmetic — the storages'
  free runs, `evictForRoom`'s storage, the loose storage's want and a texture's fit — which moved to item 11, whose
  rooms replace it; a fix to each first would be rewritten there. The prepared images' orphan is forgotten per slot
  (`PreparedImages::forget`), as `letGo` serves every slot of a file at once; `admits` became the preprocessor's
  (`ChainRuns`), asked past the store's miss; the refusal flags became counts of what stands short of room now.

- **8. Meters**: each stop's place in the report states them and the run's end names each peak with its stop; the
  device's vertex attributes and indices are counted apart from its tables. Image loads are counted as the fork's asks
  of a header for its texels, by why (a fact, a slot's file, a partner, the preparation) with their bytes, beside the
  image cache's own loads: a load by cause needs the cause carried into `ImageManager::getImage`, an upstream change no
  approval names, and the header's own read is upstream's `getModelImage`. A retake after an eviction and a move up
  are one cause, a slot's file: telling them apart needs the uploader's reason carried through `PreparedImages::find`,
  and item 13 removes the move up.

- **7. Platform branches**: MSVC's warning pragmas around third-party headers stay, a compiler's and not a system's;
  so does `OMW_NO_UNIQUE_ADDRESS`, item 2's.

- **6. The next clang-tidy checks**: taken whole, each counted over the tree first, its findings fixed:
  `concurrency-thread-canceltype-asynchronous`, `cppcoreguidelines-interfaces-global-init`,
  `cppcoreguidelines-misleading-capture-default-by-value`, `cppcoreguidelines-pro-type-vararg` (each C API's varargs
  marked with its reason), `misc-static-initialization-cycle`, `modernize-use-constraints`, `modernize-use-std-format`,
  `readability-ambiguous-smartptr-reset-call`, `readability-enum-initial-value`, `readability-redundant-preprocessor`,
  `readability-static-definition-in-anonymous-namespace`. `hicpp-exception-baseclass` is clang-tidy 23's
  `bugprone-std-exception-baseclass`, which `bugprone-*` already holds. Refused in `.clang-tidy` with what each finds:
  `misc-misplaced-const`, `misc-override-with-different-visibility`, `readability-convert-member-functions-to-static`,
  `google-build-using-namespace`, `misc-unused-parameters`, `misc-non-private-member-variables-in-classes`.

- **4. Includes**: the standard library's category matches case-sensitively, so Qt's `<QDialog>` stands with the other
  libraries, and `processwin32.cpp` keeps the tree's Windows header in a block ahead of the SDK's.
- **5. Integers**: a `ull` literal stays, 64 bits on both targets and checked by no lint; `L` only where an API's `long`
  asks it (`std::lround`, zlib's `crc32`). A cast's declaration became `auto` where `modernize-use-auto` newly reached a
  fixed width's longer name.

- **3. The normalizer**: `omw format --check` fails a file the rules would change, where the plan named `omw lint`, so
  the check CI's checks job and the gate already run holds it with no second step; it also quotes an own-folder
  include spelled from the root, and refuses a file whose Qt string-compared macro names an integer type.

## Declined

- **25. The build image fetches autoconf from GNU's mirrors**: `env/install_almalinux_deps.sh` is an input of the
  dependency set's key (`tools/omw/deps.py`, `SET_INPUTS`). Changing it means building and publishing a new set and
  repinning `pins.py`, hours of machine time for a fetch that failed on two runs of one day. It is made with the next
  change to the set: `fetch` takes a list of URLs, GNU's redirector `ftpmirror.gnu.org` first. The issue stays.

- **27. The cell reader's lent models held to their content** (the user, 2026-11-18): about 30 MiB on the handheld's
  PBR tour (51.8 MiB of room for 22.4 MiB), against a 7 GB heap. The measure and the fix cost more than they return.
  The issue stays logged.

- **14.3. A prepared model lets go of its geometry once adopted**: the reader's models held 64.3 MiB of content at the
  repack's peak on the handheld (`research/2026-11-17-handheld-allocator-tunables.md`), before packing halved it, and a
  model named again after its meshes left the device would need the reader to fill it again while a cell waits on it.
  What the reader holds is room: 170 MiB lent for those 64 MiB, and 39 MiB spare.

- **13, past its first two parts: the move up by the levels it adds, `TexturePlan`, the short list and the waits
  gone** (the user's review, 2026-11-17). With the stand-in's fix the waits do what the planner was for: everything
  stands where it fits, a stand-in gets room made for it, and what moved down comes back. What the rest buys is a
  quarter of a move up's bytes, as the kept levels are copied rather than read, and one mechanism for three; the
  PBR tour's 13.8 GB read again are chiefly retakes after evictions, which it does not touch. Its `LevelReader` would be
  a second reader of the shipped file beside the image manager's. Proposed again only with a tour whose moves up read
  more than a tenth of its texel bytes, which needs the cause carried through `PreparedImages::find` (item 8).

- **A stand-in that what rests gave no room to has what stands moved down for it** (`579612228d`, undone after): read
  off a miscount. The 95 and 63 "stand-ins" of the handheld's tours were the log's refused takes, each a texture the
  device came down a level for; each tour drew one stand-in for good, and the tour at `579612228d` drew it still,
  refused at once, as the device refuses a texture where nothing rests or is buried, never reaching the waits. Not
  worth an interface's call for no measured case.

- **14.4. The ring's flat records, the cell pools gone**: an allocation's worth per crossing, which the allocator's
  four arenas and pinned threshold (item 10.1) already keep from fragmenting the heap; nothing measured names the ring's
  pools among the handheld's peak. Proposed again with a profile or a peak that does. Item 15 is written on the records
  as they stand.

- **17. Streams a mesh brings**: a spike with no measured cost behind it; the positions' 12 B a vertex freed by
  position fetch is a guess until a tour shows geometry near the device's ceiling.

- **11.2. The ground's masks as the land's bytes**: read a byte at a time (`storageBuffer8BitAccess`), the RADV
  compile of the handheld's chip under the drm-shim put 14 landing kernels with a mod's maps from 112 registers to
  128, a wave fewer on the PBR content's landings, and the ground composite from 24 to 32; unpacked from words, 46
  kernels. NVIDIA's held one or two registers fewer. Not worth 24 MiB of the device's memory without a bench on the
  handheld that shows the wave costs nothing.

- **6. `Misc::StringUtils::format` as `std::format`, and `-Wformat=2`**: its 13 callers format the content's own GMST
  strings, whose syntax is printf's and which `std::format` cannot read; the format stays, and so does the reason
  `-Wformat=2` is off.
- **6. `cppcoreguidelines-avoid-non-const-global-variables`, `misc-no-recursion`, `google-default-arguments`**: right
  about what they find (175, 163 and 363 sites), and each site a change of design — an owner for a global, a bound or
  an iteration for a recursion, a non-virtual interface for a default — not a lint's fix; logged in `.notes/ISSUES.md`.
- **6. `readability-identifier-naming` widened**: not counted in this run.
