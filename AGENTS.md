# AGENTS.md

## What this is

A fork of OpenMW whose purpose is a **ray-traced renderer**: a 2002 game made to look
astonishing on current hardware — ray-traced visibility, path-traced indirect light, materials
recovered from pre-lit vanilla textures. Upstream OpenMW stays the host engine — cells, references,
physics, scripts, animation, weather, GUI — and stops owning the picture. Vanilla content and the
PBR replacers made for OpenMW are read as they are: what a replacer's companion maps add reaches the
trace, and a vanilla picture does not change because the renderer can read them.

Read `apps/openmw/mwrender/renderer.hpp` and its callers before changing the renderer seam, and
follow scene or resource data back to its owner before changing how the ray tracer consumes it.
What the tree, `--help` or a commit already answers does not belong here.

## Rules

- **Upstream code changes only on the user's explicit yes**, anywhere in the tree and not only
  where the renderer meets it: a fix, an improvement or a change of design. Each is proposed first,
  with what it changes, why, and what it costs upstream's merges, and waits for a yes. An approved
  change is recorded in [`.notes/UPSTREAM.md`](.notes/UPSTREAM.md) with its reason, and one not
  recorded there is reverted. The diff stays small, but never at the cost of reuse or of the
  abstraction's quality.
- Both renderers stand behind one interface that exposes no implementation detail. Where the game
  would branch on which renderer it has, the seam abstracts the question instead. One binary ships
  both, and the game starts the ray tracer alone: a start it cannot make ends in the fatal error
  box, and the OpenGL renderer stays built and unstarted so that upstream's code merges as it
  stands.
- Target hardware: NVIDIA RTX 20 series and later, and AMD RDNA 2 and later. A vendor the machine
  has no card of is stood up under Mesa's drm-shim, which compiles every kernel and executes none.
- Tried, declined, and not to be proposed again:
  - opacity micromaps (`VK_EXT_opacity_micromap`) for the cutouts: no faster, and longer loading;
  - async compute (the next trace on a second queue beside this frame's reconstruction): 0.1–0.2 ms
    gained;
  - hashed alpha (Wyman and McGuire 2017) for the soft edges, a cut fixed to the surface in place of
    one drawn each frame: a moving frame stayed noisier than its frames averaged under the
    upscaler's jitter, and the edges converged harder, with more fireflies (`cutAt`);
  - Shader Execution Reordering (`VK_EXT_ray_tracing_invocation_reorder`): its sorting cost 17–23%
    of the trace, and it shuts out Mesa's drivers;
  - fixes to the rasterizer alone where the ray tracer already does right: its ripple field, which
    steps once a sixtieth and so slows under 60 frames a second, where the ray tracer's steps every
    frame by the time the water's clock moved (`RipplePass::record`); and its object paging, which
    copies a `NightDaySwitch`'s authored child and stands every stage a visibility gate would take
    down.

## Where the code lives

**Vulkan behind an API-neutral core**, not a portability layer. A fact about Vulkan that leaks into
the core is a bug whether or not a second backend ever arrives. `docs/architecture.md` is the
shape as a reader meets it — the seam, the layers, who owns whom, the order a frame is computed in —
and the headers hold the detail.

- `components/rtx/` — the core: what the scene _is_, the light transport. No graphics API, no game
  headers.
- `components/rtxvulkan/` — the backend. What is true of an API lives here and nowhere else; the two
  places that stand one up name `VulkanRenderer` and nothing else does.
- `components/myguirtx/` — MyGUI's backend.
- `components/contentstore/` — what is derived from the content, kept on disk across sessions, which
  the ray tracer owns a store of.
- `apps/openmw/mwrender/` — the seam `MWRender::Renderer`, `GlRenderer` beside upstream's files, and
  the game-side owner in `rtx/`.
- `apps/rtxtool/` — the harness: what a run visits and what a place came to in `model/`, and in
  `instruments/` what a measured run is taken with, none of which knows a world.
- The core and the backend stand in folders by responsibility, in the order `architecture.md` lists:
  a folder includes only the folders before it, and a folder inside another is a part of it.
  `RtxSourceTreeTest` holds the order, so a folder that needs a later one is a file in the wrong
  folder or a file to split.

## Verification

`./omw [flavour] <verb>` is the one way in, and `./omw help` lists the verbs and the flavours. The
flavour comes before the verb and is `debug` unless named; `release` is the build a number is quoted
from. `./omw exec ./openmw-rtxtool --help` lists the harness's options.

Three kinds of content load the renderer differently; where each installs, and the profile that
names it, is the machine's:

- vanilla Morrowind, what the player's `openmw.cfg` names and every suite has been measured on;
- a large mod list with Tamriel Rebuilt, such as the Morrowind Fullrest Repack, which a run takes
  through its own profile with `--replace=config --config=<profile>`;
- Morrowind with PBR replacers stacked by a profile of their own: the content a measurement of
  companion maps, texture memory or residency needs.

The user runs the harness through the Zed tasks in `.zed/tasks.json`: `./omw release view
--size=2560x1440` at a place, one task a profile.

- Compiling is not verifying. Build the targets you touched and run the covering binary with a
  filter: `./omw test <binary> --gtest_filter=...`. `./omw lint driver` before a push that touches
  `tools/omw`.
- `./omw test` once before saying it works. The GPU binary fails without a device rather than
  skipping, so a green run means a device ran it.
- `./omw gate` once at the end. Never a gate beside a build or another gate, and never two runs at
  once: they share the card.
- Do not open the game window to check a rendering change: the harness's verbs check it, at the
  places in `files/rtx/views.cfg` and the suites in `files/rtx/benches.cfg`. `view` is for what only
  a window shows.
- `./omw shot --views=all --map --upscale=off --out=<dir>` ahead of a change and `--against=<dir>`
  after it names the pictures the change moved. Narrow an investigation to its place, and run `all`
  last. Keep the baseline outside `/tmp`, whose cleaner empties it under a long session.
  `--upscale=off`, because an upscaled picture moves with anything its history saw.
- `./omw noise` holds the frame's noise against its own frames averaged, with each place's bias
  against a converged reference and its fireflies. Its legs show different faults: `--strafe` and
  `--walk` a history length or a filter's reach, `--cut` the fireflies after a cut, and
  `--upscale=native` a temporal filter that fetches its history off the pixel. An A/B is
  `./omw release noise --ab=<switch>`, with `--still` for a switch that touches short histories,
  which the upscaler's jitter keeps at every edge. Narrow it to one place and leg first; the suite is
  the verdict.
- `./omw kernels > before.txt` ahead of a shader change and `--against=before.txt` after it names
  the kernels the change moved; a tuple of constants it did not name draws what it drew.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk one place with the
  upscaler and the denoiser off, the second with the queue held behind the host, and must agree
  frame for frame. A pair that finds nothing has found nothing. Read a difference with
  `--exposure=1` and `--pictures`.
- **The denoised frame is not bit-exact on every card** (`docs/architecture.md`): on a high-end
  NVIDIA laptop GPU under a busy queue, the first wavelet dispatch after a pipeline drain sometimes
  differs by an ulp on identical inputs. The card's, not a missing barrier. `repeat` runs unfiltered
  and cannot see it; a difference in the composed frame alone is the card's.
- Measure with `./omw release bench`, on a hot card, back to back, never with a sleep between runs,
  after a throwaway warm-up leg; the first run after a shader change compiles the pipelines. On a weak
  machine, bench in a smaller window (`--size=960x540`), the same size on both sides of an A/B: at the
  default size a run takes too long to repeat.
- Measure on a quiet desktop: start the run in the background and end the turn. In the session's
  foreground it runs under the agent's own redraws, which the editor and the compositor draw, and
  every row moves. The report's `card` lines name another process's work, and `--frame-times=<dir>`
  writes the series behind a tail. The compositor's own slices show only under a GPU context-switch
  trace (`nsys profile --gpuctxsw=true` on NVIDIA), and the renderer cannot outrank them (a
  high-priority queue needs `CAP_SYS_NICE`).
- An A/B reads medians and the p99, never a mean — the zones' from the record `--json` writes, since
  the `gpu` row is each zone's share of the mean.
- Profiling: `./omw profile` for the CPU, into `out/release/perf/`: read the full text reports
  beside the summary (by library, by source line, the callers); `--offcpu` says where it waits. For
  the GPU, `./omw release exec nsys profile ./openmw-rtxtool bench ...`.

## Conventions

**C++20, `.clang-format` at 120 columns.** The user's global Rust rules do not apply to this tree;
the posture behind them does.

- **`#pragma once`, and includes in five blocks** a blank line apart: the file's own header, the C++
  standard library, `<gtest/...>`, other libraries, `<components/...>` and `<apps/...>`, then quoted
  local headers — of the file's own folder only, and any other folder is spelled from the root.
  `.clang-format` sorts inside each block and keeps the blocks. A conditional `#include` goes last,
  and a block out of order carries the comment saying why, the way `memory.cpp` does for the
  allocator. The headers GLSL reads as well (`components/rtx/shaders/*.h`,
  `components/rtxvulkan/shaders/shared/*.h`) are the one exception to `#pragma once`, and
  `portable.h` says why.
- **Include what you name.** A `.cpp` may lean on its own header for what that header's interface
  already needs, and on nothing else.
- **A part that needs another part's state is handed it at the call** (`ThreadContent`, whose cache
  is handed the preprocessor at each read), and holds no reference to a sibling member or to its
  owner. An owner that does wire its parts together by reference is pinned: a class with a
  constructor, its copy and move deleted, each member declared after everything it refers to.
  **Never an aggregate**: MSVC bound a reference that an aggregate's default member initializer
  took to a sibling to another object inside a designated initializer.
- **The preprocessor switches only where nothing else can.** What systems spell differently goes in
  a `…posix.cpp` and `…win32.cpp` pair that CMake chooses, behind one header: a general fact in
  `components/platform` (`Platform::Process`), the crash catcher's own in its `…system.hpp`. A build
  flag is defined in every build as `0` or `1` and read once into a `constexpr bool`
  (`Rtx::sDebugNames`), which code asks with `if constexpr`. Code only one build has is a file CMake
  chooses (`crashimagenone.cpp`), never an `#ifdef` around it. A test of an `assert` calls
  `Testing::expectAssertDies`. What stays: a chain inside the one file that owns a system's
  difference, an include only one system has, and the headers GLSL and C++ both read, whose
  differences `shaders/portable.h` holds.
- **GPU vendors stand behind one entity, as systems do.** What NVIDIA and AMD do differently — an
  upscaler, a driver's fault the renderer works around, a card's sensors — is one interface with an
  implementation file per vendor, chosen in one place: from the driver's identity once, at device
  creation, or by CMake where a vendor's SDK is built in or not. Every other file asks a capability
  or an interface, never a vendor ID, a driver ID or a vendor's name. A strong preference: a branch
  on the vendor anywhere else carries its reason beside it.
- **Comments say _why_**: an invariant, a workaround and its cause, a trade-off against the obvious
  alternative — never a restatement of the line under it. No decorative dividers. Stale narration
  in code you are editing is fixed with it; sweeping other files is a separate task.
- **Frame times are uniform**, and an average that hides a spike is not an answer. Work is
  _incremental_, never _batched behind a threshold_: a table recycles its slots, a resource is
  appended rather than rebuilt. What cannot be made cheap belongs off the frame path entirely, not
  on a rota. Report the p99 and the worst frame beside the median.
- **Allocation is a metric on the frame path and at load.** Persistent scratch buffers refilled with
  `clear()`, results into an out-parameter, no `std::string` or `std::function` per frame, logging
  that compiles out; a test enforces it. A loader is a persistent object owning its buffers,
  `clear()`ed and refilled for each thing it reads: cells arrive while the game is running, so a
  spike taken at load is a spike a player feels.
- **Nothing is computed twice, and everything as early as it can be** — at initialization or at
  load. A frame reads what it was handed.
- **A shader's float arithmetic is the build's, not the driver's.** Every module is pinned
  (`components/rtxvulkan/spirv/spirvpin.hpp`), so GLSL is written as usual; an operation the pinning
  refuses stops the build, and the message says what it is. Nothing is fused, so every product and
  sum rounds where it is written, and an `fma` is refused: the device that would round it once is
  not every device.
- **One path through a shader.** A single computation covering every case beats a tree that skips
  work per lane: a factor of zero, a table lookup, a value selected without a jump. Divergence needs
  a measurement saying the branch pays for itself, named where the branch lands.
- **Asserts** guard contracts the code must keep, not data the world might supply. Hot paths use the
  debug-only form; untrusted input is never an assert.

## C++ idioms

The common ground of the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
(cited by rule), [Google's style guide](https://google.github.io/styleguide/cppguide.html) and
[LLVM's](https://llvm.org/docs/CodingStandards.html). They are defaults: a case that needs another
answer takes it with a comment saying why, and the conventions above win where the two meet. They
hold for the code the fork writes; upstream's code is not brought in line with them (Rules).

- **An object owns each resource** (R.1, R.3, R.11, CP.20). Memory, a handle, a file or a lock is
  released by a destructor, never by a call on each way out. No naked `new` or `delete`:
  `std::make_unique`, a container or a handle class. A raw pointer or reference owns nothing, and a
  lock is a `std::scoped_lock` or `std::unique_lock`.
- **`std::unique_ptr` is the owner by default** (R.20, R.21): `std::shared_ptr` only where ownership
  is truly shared, and made with `std::make_shared`.
- **The rule of zero** (C.20, C.21, C.66, C.37): a class whose members manage themselves declares no
  copy, move or destructor. One that declares or deletes any of the five does all five, and its
  moves and destructor are `noexcept`.
- **A parameter says how it is used** (F.7, F.15–F.18, R.30). In: a cheap type by value, anything
  else by `const&`. In-out: by reference. Kept: by value and moved into place. A function that only
  uses an object takes `T&` or `T*`, and a smart pointer only when it changes the object's lifetime.
- **Borrowed text is `std::string_view`, a borrowed sequence `std::span`** (SL.str.2, F.43). A view
  or a reference never outlives what it points into: none is returned to a local or a temporary,
  kept past the call that lent it, or handed to a C API that reads up to a terminator.
- **Results are returned** (F.20, F.21), several as a named struct rather than a `std::pair` or a
  tuple, except on the frame path, whose results go into persistent out-parameters (Conventions).
- **`const` by default** (Con.1–Con.5): a variable, a member function, what a pointer or reference
  reaches. `constexpr` for what the compiler can compute.
- **Declared where first needed, and initialized** (ES.5, ES.12, ES.20–ES.22, ES.26, ES.28): small
  scopes, one purpose a variable, no name that shadows an outer one. A `const` whose value takes
  several steps is set by an immediately invoked lambda.
- **The type says what a value is** (Enum.3, C.46, SL.con.1): `enum class`, `nullptr`, `explicit`
  single-argument constructors and conversion operators, `std::optional` for a value that may be
  absent, `std::variant` for one of several, `std::array` or `std::vector` and never a C array.
- **Casts are named and rare** (ES.46, ES.48–ES.50, ES.100): no C-style or functional cast, a
  narrowing conversion spelled out, `std::bit_cast` to read an object's bytes as another type, and
  `reinterpret_cast` or `const_cast` with a comment saying why. Signed and unsigned do not meet in a
  comparison or a sum without a conversion (`std::cmp_less`, `std::ssize`).
- **Class hierarchies** (C.35, C.67, C.128): an override says `override` or `final` and not
  `virtual`, a base class's destructor is public and virtual or protected and not virtual, and a
  polymorphic class allows no public copy or move, which would slice it.
- **The standard library before hand-written code** (ES.1, ES.71, SL.1): an algorithm or a range
  `for` over an index loop where either serves.
- **`auto` where the type is noise** (ES.11): an iterator, a `make_unique`, a cast's result. A type
  the reader needs to follow the code is spelled.
- **A lambda that outlives its scope captures by value** (F.53): one stored, returned or sent to
  another thread names what it captures and holds no reference to a local.
- **Headers** (SF.7, SF.21, SF.22): no `using namespace` at a header's file scope and no unnamed
  namespace in a header. What a `.cpp` keeps to itself goes in an unnamed namespace.
- **Exceptions are classes, thrown by value and caught by reference** (E.14–E.16). A destructor, a
  deallocation, a `swap` and a move do not throw.
- **Threads** (CP.25, CP.42): `std::jthread` over `std::thread`, and a `condition_variable` waits
  with a predicate.
