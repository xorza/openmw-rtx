# Every configuration's compile and link flags, for GCC, Clang and MSVC alike, read through
# `CMAKE_USER_MAKE_RULES_OVERRIDE` as CMake sets each language's defaults: the one place the tree sets them, so
# no preset and no folder carries a flag. The code generation is the vcpkg set's (`vcpkg/flags.cmake`); what is
# added here is the tree's alone, and the checks that need a compiler are the root's.
#
# - RelWithDebInfo, which debug, asan and tsan build: optimised, with every assert.
# - Release, the build a number is quoted from and the release ships: CMake's own -O3 and NDEBUG, with identical
#   functions folded at the link (`/OPT:ICF`, `--icf=safe`), which a debugger would name one for another.
#
# On MSVC the debug information is embedded in each object (`/Z7`), which ccache can cache, but under link-time
# code generation, and every program links a PDB; the C runtime is linked in, as the set's libraries have it
# (`x64-windows-openmw`): no runtime to ship beside the programs, and none older installed system-wide to fault in.
#
# Every program is position-independent, as MSVC's always are (`/DYNAMICBASE`), the desk's GCC and Clang default
# to, and the set's archives (`-fPIC`) allow; the release base's GCC would otherwise link a fixed address.

include("${CMAKE_CURRENT_LIST_DIR}/../vcpkg/flags.cmake")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(OPENMW_SANITIZER)
        message(
            FATAL_ERROR
            "OPENMW_SANITIZER is GCC's and Clang's: MSVC's sanitizers have not been measured on this tree"
        )
    endif()
    # Under link-time code generation the debug information goes to each target's PDB (`/Zi`) instead: an
    # object then carries its intermediate code beside its debug information, and `openmw-lib`'s 414 came to
    # 4.9 GB, past the 4 GB a COFF archive holds (LNK1248); with the PDB, 3.6 GB. A tagged release, which alone
    # asks for it, builds cold.
    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
    else()
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    set(openmw_flags_relwithdebinfo "/O2 ${openmw_codegen_msvc}")
    set(openmw_flags_release "/O2 /DNDEBUG ${openmw_codegen_msvc}")
    # `/DEBUG` turns the linker's defaults to `/OPT:NOREF,NOICF`, so a release asks for both back.
    foreach(kind EXE SHARED)
        set(CMAKE_${kind}_LINKER_FLAGS_INIT "/DEBUG")
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE_INIT "/OPT:REF /OPT:ICF")
    endforeach()
else()
    # `sqrt` and the like as one instruction rather than a call that sets `errno`, with the same result: the tree
    # reads `errno` after system calls alone. The tree's, not the set's, whose code was not read for it.
    set(openmw_flags_common "${openmw_codegen_gnu} -fno-math-errno")
    if(OPENMW_SANITIZER STREQUAL "address")
        string(APPEND openmw_flags_common " -fsanitize=address,undefined")
    elseif(OPENMW_SANITIZER STREQUAL "thread")
        string(APPEND openmw_flags_common " -fsanitize=thread")
    elseif(OPENMW_SANITIZER)
        message(FATAL_ERROR "OPENMW_SANITIZER is address, thread or empty, not ${OPENMW_SANITIZER}")
    endif()
    set(openmw_flags_relwithdebinfo "-O2 ${openmw_flags_common}")
    set(openmw_flags_release "-O3 -DNDEBUG ${openmw_flags_common}")
endif()

foreach(language C CXX)
    set(CMAKE_${language}_FLAGS_RELWITHDEBINFO_INIT "${openmw_flags_relwithdebinfo}")
    set(CMAKE_${language}_FLAGS_RELEASE_INIT "${openmw_flags_release}")
endforeach()

# Linux links with mold, and its static libraries are thin archives (`ar T`), which name their objects
# rather than copy them: no library is installed, and copying the game's objects cost every relink seconds.
# GCC's C++ runtime is linked in, as every other library is, so a program runs on a system whose libstdc++
# is older than the compiler's (GCC's runtime library exception permits it); not under a sanitizer, whose
# runtime takes the allocator's place in a shared libstdc++ alone. Folding is `safe`, which leaves alone what
# may have its address compared; with GCC's objects it folded 1.2 MB of the release `openmw`'s 133 MB of code.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Clang links with its own lld: mold stopped on the vcpkg set's GCC-built archives beside Clang's objects,
    # whose virtual thunks name COMDAT groups GCC's discarded ("probably due to an ODR violation"), and crashed.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_LINKER_TYPE LLD)
    else()
        set(CMAKE_LINKER_TYPE MOLD)
    endif()
    set(openmw_link_flags "-Wl,--gc-sections")
    if(NOT OPENMW_SANITIZER)
        string(APPEND openmw_link_flags " -static-libstdc++ -static-libgcc")
    endif()
    foreach(kind EXE SHARED)
        set(CMAKE_${kind}_LINKER_FLAGS_INIT "${openmw_link_flags}")
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE_INIT "-Wl,--icf=safe")
    endforeach()
    foreach(language C CXX)
        set(CMAKE_${language}_ARCHIVE_CREATE "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")
    endforeach()
endif()
