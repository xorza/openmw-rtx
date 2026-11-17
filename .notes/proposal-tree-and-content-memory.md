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
- **The planner before the geometry's host copy goes** (item 13 before 14): until no world is rebuilt for a reserve
  that moved, a rebuild reads that copy.
- **Flat ring records before the ring's prefix** (item 14 before 15), so the prefix is written once, on the records
  that stay.
- **Need after the planner, and streams after the geometry** (items 16–17): each is a policy or a layout over a
  mechanism an earlier item makes, and each may be declined on its own measurement without undoing one.

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
  stands where "a residency manager" was refused with no reason recorded.
- **A resting mesh counts its structure and its geometry, and gives both back** (item 14.6), changing "a mesh counts
  what its eviction gives back" (`a3d689826f`).
- **Levels by need, a meter first** (item 16): declined past its meter if under a tenth of the handheld PBR tour's
  texture bytes stand in levels no ray read.
- **What the texture pool strands in pieces is moved, not classed** (item 16.4), only if a tour after item 11 still
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
- **A texture's levels are read by position, from where they stand, into a buffer the reader owns**: the store's entry
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
- **A texture stands at a level, and one planner moves it**, by the user's decision: down by a copy on the device, up
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
- **The ring's records are flat, and a reference states once what is its own**: one buffer for the ring with a run a
  cell, a row a reference and a slot a part. Refused: pools that keep what each object grew to.
- **The ring reads what can stand at its distance**: a cell's references are read largest first as far as the size rule
  reaches one band ahead, and further as the cell nears; a model's radius is the preparation's to state. Refused:
  reading everything in the disc, most of which stands only within a cell or two.
- **A large buffer never reaches the allocator, and the mapping threshold is pinned**: pinning it turns glibc's own
  adjustment off (`mallopt(3)`), so a block past it is mapped and given back whole, whichever thread frees it. Its
  value, and whether an arena cap stands beside it, are item 10's tunables to say. Refused: a trim on a timer, which
  walks every arena to give back what should never have entered one.
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
class LevelReader           // the hands, the queue, cancel(slot); hands out a pool buffer with its levels' spans
struct LevelSource          // std::variant<StoredChain{Key}, ShippedFile{header, first level's offset}>

// components/rtx/scene
struct PackedMesh           // spans in the device's form: positions, normal words, coordinates, colours, tangents,
                            // indices; with its MeshKey and bounds, as the reader made them. A stream the mesh
                            // does not bring is an empty span from the start, whatever item 17 decides of the device
class MeshArrivals          // one flat buffer a stream, a start for each arrived slot, cleared after the hand-over,
                            // given back past its bound
struct TextureLevels { std::uint8_t mStands; std::uint8_t mWanted; std::uint8_t mFloor; };   // on TextureRow
class TexturePlan           // budget and wants in; a bounded list of moves out, a hand-over; a defragmentation
                            // pass's moves are its moves at the same level

// components/rtx/mirror/cells: one RunBuffer each for the ring, a run a cell.
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

Gone: `PreparedImages::mEntries`' holds, `ChainForm::Coarser`, `ShortTexture`, `SceneUploader::TextureWait` and
`evictForRoom`'s shared figure, `TextureArray::chooseLevels`, `rebuildsWorld` and `wasHeldBack`, `MeshTable`'s arrays,
`PreparedModel`'s arrays past adoption, `Placement`, the `Recycled` and `Spares` pools of cells.

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

### 8. Meters, a harness that runs as the game, and a baseline

Then the baseline: tours of the three contents on the desk, the handheld and Windows, to `docs/research/`, which every
later item is held against, each flown twice, so a leg's worst frame has its own spread beside it. The note keeps
every leg's worst frame with what led it, and every outrun. A tour now expires the timer caches as play does and reaches doors as far as a player; what that does to a flight's worst frames is read off it, and a grid reach scaled apart from the doors' is proposed with that figure if a flight needs it. One handheld tour more of each content as tours ran
before (huge pages, the scaled reach, no expiry), so `2026-11-17-handheld-tours-and-store-forms.md`'s figures can be
told apart from the game's.

Then the baseline's longest frames are explained: the ten longest on the handheld, a content each, are profiled
(`perf` beside the run, as `./omw profile` does on the desk) until each names what led it. One an item of this plan
causes or cures is that item's to show gone; one that is upstream's is proposed with its profile.

### 10. The heap, the budgets and the game's own holders

1. The allocator's settings, by tunables and no code: handheld Repack tours under a pinned
   `glibc.malloc.mmap_threshold` at 256 KiB, 1 MiB and 4 MiB, and the best of them again under `glibc.malloc.arena_max`
   at 4. Then pinned in `components/platform` at the setting that leaves the arenas least free memory for the tour's
   time, with its reason beside it. One tour more with the process's transparent huge pages off, behind a switch of
   the harness (`PR_SET_THP_DISABLE`): what the handheld's kernel adds to the heap's free memory is a figure before it
   is anyone's decision.
2. The budget counting the heap's free memory; evicted values destroyed on the deriving worker; `update`'s walk off the
   cache's lock.
3. The unified bound, asked as a capability of the device, never by vendor.
4. `Scene` asks the preloader's budget before a door's destination is loaded.
5. The navigator's tile cache under the byte budget; its setting and the setting's documentation go.
6. Handheld tours of the three contents: nothing swapped out at a tour's end, and a NIF parsed about once. If the heap
   still holds a fifth of what it took free, the allocator is proposed with those figures.

### 11. The device uses its budget

4. Tours on the handheld, Repack and PBR: no refusal with room under the budget; item 8's figures again.

### 12. One read a texture

One bump of the store's version carries every format change of this plan, the manifest's among them, so the stores are
made again once; a store made on the desk is the handheld's too, the key being the content.

4. The manifest states each model's radius and whether it gives light, which item 15 reads.
6. Tests: a re-arrival opens one file; a level range read equals the decode's bytes for every block format; a stream's
   bytes the same through its buffer and past it, at every boundary; no allocation a hand-over once warm; a cancelled
   job holds nothing.
7. Handheld PBR tour: image loads at one a file, the tour's time, the heap's free share; and item 10's tunables once
   more, now that the largest buffers are the pool's.

### 13. A texture stands at a level

1. A move on the device: a new image at the level, the kept levels copied, the slot's descriptors written, the old
   image buried after; a move up reads only the levels it adds.
2. `TexturePlan`, by room alone: an arrival stands whole where the plan fits, else at the plan's level; a reserve that
   moves changes the budget and the plan follows a bounded step a hand-over.
3. The short list, the waits and the rebuild go, and with them a texture's want, whose hole adds the margins a resting
   texture's fit, its range, never carries (item 9). Tests: a change of mode keeps every structure and geometry buffer; a
   texture brought down and up again reads its top levels once; two runs plan alike.
4. Tours of the three contents on the desk, the handheld and Windows.

### 14. One copy of the geometry

1. The reader packs, keys and bounds; `MeshTable::add` takes a `PackedMesh`; split normals stay words.
2. `MeshArrivals`; `MeshTable` drops its arrays; the masks' host copy goes with them; the tests that read arrays after
   a hand-over read them before it.
3. `PreparedModel` lets go of its arrays once adopted, on the reader's thread; a second adoption finds its meshes by
   key or asks the reader to fill it again.
4. The ring's flat records; the cell pools go.
5. The ground's indices as one run every cell's mesh names.
6. On the device: `MemoryUse::Mesh`; a block no run touches given back, runs taken lowest first so blocks empty; the
   rest counting geometry; and a want for a mesh refused in the loose storage, which a block emptied answers and no
   resting mesh's fit does (item 9).
7. The shape digest made where the host asks for digests; `repeat --pairs=10` agrees.
8. Tests: a crossing allocates nothing; the arrivals' bound. Tours: the host's peak against item 8's.

### 15. The ring reads what can stand

1. A cell's references read as a prefix by radius, one band ahead of the size rule, the radius the manifest's;
   `CellRing` asks the reader to go further as a cell nears.
2. Item 8's count again; `repeat` and `shot`, since a model must stand before the frame that first shows it; and no
   outrun on the legs the baseline outran.

### 16. Levels by need

1. The feedback as a meter that moves nothing: a word a slot in the frame slot's tables, the minimum of the levels
   asked, an anisotropic read stating its shorter axis's. Kept in the trace's kernels only if no kernel changes tier on
   any of the three drivers and the benches stay within their noise; else made by a pass of its own over the frame's
   hit records.
2. Its histogram over the tours: the share of texture bytes in levels no ray read. Under a tenth on the handheld's PBR
   tour, the rest of this item is declined.
3. The plan's order under pressure, with the peak's hold; a floor every texture keeps.
4. Defragmentation by the planner's move, if a tour after item 11 still refused a texture with room in pieces: one
   pass where a want finds no hole, bounded by what a hand-over stages. A test: a pool cut into pieces takes the
   texture it refused, and no frame that fits runs a pass.
5. `noise` and `shot` under a budget held low; tours.

### 17. Streams a mesh brings

Tangents and colours in runs of their own, a mesh without one reading a shared element through a stride of nought, so
the shader keeps one path. Kept only if `kernels` shows no tier change. A spike, not a commitment: a committed hit's
corners through position fetch, which would free the positions' 12 B a vertex.

### 18. Close

`./omw gate`; `./omw release tour` of the three contents on the desk, the handheld and Windows, held against item 8's.
They pass where:

1. no tour meets a fault, an outrun among them;
2. no leg's worst frame is longer than that leg's at the baseline by more than the baseline's own spread;
3. each of the baseline's ten longest frames on the handheld is gone, or stands proposed with its profile.

A condition that fails is worked, not logged: its cause is found and fixed, or the item that owed it is moved to
**Declined** with what was found. Then `.notes/ISSUES.md` is rewritten from the tours; the decisions folded into
`docs/architecture.md`, `AGENTS.md` and `UPSTREAM.md`; the `todo.md` items this closes struck; this file deleted.

## Changed

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
