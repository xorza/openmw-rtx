# Proposal: one spelling for the tree — macros, conditionals, includes, integers, and the next checks

The four `todo.md` items under Code (audit macros, `OPENMW_FFMPEG_5_OR_GREATER`, audit `#if`s, order includes) and
"unsigned int to uint32", surveyed at `8c032f6e00` over `apps/` and `components/` (4,023 sources: 1,393 the fork's,
2,630 upstream's, by `fork_files`), and `extern/osg-ffmpeg-videoplayer` where it holds the FFmpeg macros. Written for
`/execute-plan`: items in order, each a commit, `./omw gate` at the end.

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

## Plan

**Every upstream change below is approved by the user (2026-11-16)**: items 1–4 and 6, `extern/osg-ffmpeg-videoplayer`
and the unstarted rasterizer included; item 5's fixes to upstream's code fall under the standing approval of
`.clang-tidy` over upstream's code in `UPSTREAM.md`. Each is recorded there under **One standard for the tree**, in the
commit that makes it, with its reason and cost. An upstream change the plan does not name is still proposed first. Windows-only lines (`_WIN32` branches, MSVC pragmas) are checked by Windows CI,
which the branch's pushes start. No item moves a frame, so `./omw gate` and CI are the check; `repeat`, `shot` and the
benches are not owed.

### 1. Legacy conditionals out

Each branch the pin decides is unwrapped or deleted, its comment with it.

1. **FFmpeg**: the 34 sites; `libavutildefines.hpp` and `libavformatdefines.hpp` deleted; the FFmpeg 4 code paths
   (`channel_layout`/`channels`) gone.
2. **Bullet, Qt, compilers, architectures**: the version and compiler rows of the table. `testbsafile.cpp`'s
   `#ifndef _MSC_VER` (MSVC's `streambuf` past 2³¹−1) is tried on Windows CI first and kept, with its reason, if MSVC
   19.44 still fails it.
3. **Switches nothing defines**: `NO_LUAJIT`, `USE_OPENXR` (the VR layout stays out), `DEBUG_GROUPSTACK`, `O_BINARY`,
   `BT_NO_PROFILE` (the profiler tab, which no build can show), the `ALC_ALL_DEVICES_SPECIFIER` fallbacks.
4. **Test fallbacks**: `#ifndef OPENMW_PROJECT_SOURCE_DIR` and `OPENMW_DATA_DIR` deleted, so a target that loses the
   definition fails to compile instead of reading `"."`. Fork's two, upstream's four.
5. **`Q_MOC_RUN`** guards deleted.
6. **`OPENMW_USE_UNSHIELD`** defined `0` or `1` on every build, read into a `constexpr bool`, `if constexpr` where the
   wizard branches, as `~/.claude/rules/cpp.md` asks of a build flag.
7. **The rasterizer's dead branches**: `OSG_HAS_MULTIVIEW`, `OSG_GLES*`, `OSG_GL1_AVAILABLE`, `GL_DEBUG_OUTPUT`;
   `OSG_VERSION_LESS_THAN(3, 6, 6)` only if the pinned `openmw-osg` commit has OSG #1028's fix.

### 2. Macros

1. **The vendored OpenAL headers** deleted for `<AL/alext.h>`, `<AL/efx.h>` and `<AL/efx-presets.h>`.
2. **The GL fallbacks** deleted for Khronos's `<GL/glext.h>`, which the set ships, once both sets are checked to carry
   it; `GL_SRGB_ALPHA8` becomes `GL_SRGB_ALPHA`.
3. **The seven replaceable macros** of Findings become functions and templates, each with a test where the macro had
   none.
4. **`cppcoreguidelines-macro-usage` on**, `IgnoreCommandLineMacros: true`. Each macro that stays carries
   `// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): <why>`.

### 3. Includes

1. **`.clang-format` regroups**: the categories of Decisions, LLVM's dropped, then `omw format` over the tree. Every
   source compiled after it, as a reorder can break a header that leaned on an earlier include, which then includes
   what it names.
2. **Quoted paths outside the file's folder** rewritten from the root (`"../mwbase/world.hpp"` →
   `<apps/openmw/mwbase/world.hpp>`), by the normalizer, so `omw lint` holds it and the merge driver rewrites
   upstream's side.

### 4. Integers

1. **The rule in AGENTS.md's Conventions**, as Decisions states it.
2. **The normalizer** in `tools/omw`, with its tests in `tools/omw/tests`: a table of `before → after` for every
   spelling, strings, comments, raw strings, `int main` and the GLSL-shared headers untouched, idempotence, and the
   `#include <cstdint>` (or `<cstddef>`) it adds to a file that names a type it introduced. Wired into `format`,
   `lint` and `merge-file`.
3. **The premise asserted once**: `static_assert(std::is_same_v<std::int32_t, int>)` and its kin in
   `components/platform`, so a target where the rename changes a type stops the build.
4. **The fork's code normalized**, then **upstream's**.
5. **`long`, `unsigned long`, `long long` and `L`/`LL` literals by hand** (81 keywords, about 66 literals), each its
   width: a file offset 64 bits, a C API's own type kept with its reason. Among them the Lua `seek` offset
   (`mwlua/vfsbindings.cpp`, `sol::optional<long>`), 32 bits on Windows.
6. **`google-runtime-int` on**, `SignedTypePrefix: std::int`, `UnsignedTypePrefix: std::uint`, `TypeSuffix: _t`, and
   **`modernize-deprecated-headers`** (21 `<stdint.h>`-style includes).

### 5. The next clang-tidy checks

The pinned clang-tidy is 23. `clang-tidy --list-checks -checks='*'` is read first: checks move between groups across
releases (the file already names `bugprone-signed-bitwise`), and a name below that 23 lacks or aliases is dropped.
Each check as the file holds the others: `omw tidy` counts it, its findings are fixed in one commit, and it is enabled
whole, or refused with its reason. Every check here finds a class of bug or states one of AGENTS.md's or
`~/.claude/rules/cpp.md`'s rules; one of style alone stays out, as the file's header says.

1. **Expected to find little** — the file's own candidates: `cppcoreguidelines-interfaces-global-init`,
   `misc-misplaced-const`, `misc-static-initialization-cycle`, `readability-ambiguous-smartptr-reset-call`,
   `readability-enum-initial-value`, `readability-redundant-preprocessor`,
   `readability-static-definition-in-anonymous-namespace`; and `concurrency-thread-canceltype-asynchronous`,
   `hicpp-exception-baseclass` (an exception derives from `std::exception`, E.14).
2. **Expected to find some**:
   - the file's own: `cppcoreguidelines-avoid-non-const-global-variables` (no mutable global state),
     `cppcoreguidelines-misleading-capture-default-by-value` (F.53), `google-default-arguments` (a virtual's default
     comes from the static type), `misc-override-with-different-visibility`;
   - `modernize-use-std-format` with `StrFormatLikeFunctions: ::Misc::StringUtils::format`: its 19 calls become
     `std::format`, `StringUtils::format` and its `snprintf` go, and `-Wformat=2`, which `cmake/Checks.cmake` refused
     for that function alone, comes on;
   - `cppcoreguidelines-pro-type-vararg` (C varargs, unchecked), after the above;
   - `modernize-use-constraints` (27 `std::enable_if`; every template parameter has a concept, T.10);
   - `readability-convert-member-functions-to-static` (a member only where it needs the representation, C.4);
   - `google-build-using-namespace` (147 directives; SF.7, though the check takes every scope, not the file's alone).
3. **Counted, then whole or refused**: `misc-non-private-member-variables-in-classes` with
   `IgnoreClassesWithAllMemberVariablesBeingPublic` (a class keeps its data private, a struct is data, C.2 and C.8);
   `misc-no-recursion` (a recursion states its bound); `misc-unused-parameters` (dead code); and
   `readability-identifier-naming` widened from five kinds to the rest the tree follows — classes, functions,
   members' `m`, constants' `s`.
4. **Refused here**: `cppcoreguidelines-avoid-const-or-ref-data-members`, which takes the pinned owner `cpp.md`
   allows; `cppcoreguidelines-pro-bounds-*`, every byte buffer and Vulkan structure; and, as style alone,
   `readability-use-concise-preprocessor-directives`, `modernize-type-traits`, `modernize-use-ranges`,
   `modernize-use-designated-initializers`.

### 6. Platform branches behind `components/platform`

Each caller of Findings' list asks `components/platform` (or a `…posix.cpp`/`…win32.cpp` pair CMake chooses), and
`WIN32` goes; `openmw/main.cpp`'s `WinMain` becomes a `mainwin32.cpp`. The largest item and the most
Windows CI: last, so the rest lands without it.

## Changed

## Declined
