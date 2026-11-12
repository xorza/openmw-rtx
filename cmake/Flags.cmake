# Every configuration's compile and link flags, for Clang and MSVC alike, read through
# `CMAKE_USER_MAKE_RULES_OVERRIDE` as CMake sets each language's defaults: the one place the tree sets them, so
# no preset and no folder carries a flag. The code generation is the vcpkg set's (`vcpkg/flags.cmake`, and on Linux
# `vcpkg/linux.cmake`); what is added here is the tree's alone, and the checks that need a compiler are the root's.
#
# - RelWithDebInfo, which debug, asan and tsan build: optimised, with every assert, the standard library's among them.
# - Release, the build a number is quoted from and the release ships: CMake's own -O3 and NDEBUG, with identical
#   functions folded at the link (`/OPT:ICF`, `--icf=safe`), which a debugger would name one for another. A tagged
#   release adds link-time optimisation (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`): ThinLTO, and MSVC's `/GL`.
#
# On MSVC the debug information is embedded in each object (`/Z7`), which ccache can cache, except under link-time
# code generation, and every program links a PDB; the C runtime is linked in, as the set's libraries have it
# (`x64-windows-openmw`): no runtime to ship beside the programs, and none older installed system-wide to fault in.
#
# Every program is position-independent, as MSVC's always are (`/DYNAMICBASE`), Clang defaults to and the set's
# archives (`-fPIC`) allow.

include("${CMAKE_CURRENT_LIST_DIR}/../vcpkg/flags.cmake")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(OPENMW_SANITIZER)
        message(FATAL_ERROR "OPENMW_SANITIZER is Clang's: MSVC's sanitizers have not been measured on this tree")
    endif()
    # Under link-time code generation the debug information goes to each target's PDB (`/Zi`) instead: an object
    # then carries its intermediate code beside its debug information, which took `openmw-lib`'s archive past the
    # 4 GB a COFF archive holds (LNK1248). A tagged release, which alone asks for it, builds cold.
    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
    else()
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    # The STL's hardening, its checks on `operator[]`, `front()` and the like, which is ABI-independent, so the
    # set's archives built without it link beside it (`_MSVC_STL_HARDENING`, the STL's "STL Hardening").
    set(openmw_flags_relwithdebinfo "/O2 /D_MSVC_STL_HARDENING=1 ${openmw_codegen_msvc}")
    set(openmw_flags_release "/O2 /DNDEBUG ${openmw_codegen_msvc}")
    # `/DEBUG` turns the linker's defaults to `/OPT:NOREF,NOICF`, so a release asks for both back.
    foreach(kind EXE SHARED)
        set(CMAKE_${kind}_LINKER_FLAGS_INIT "/DEBUG")
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE_INIT "/OPT:REF /OPT:ICF")
    endforeach()
else()
    include("${CMAKE_CURRENT_LIST_DIR}/../vcpkg/linux.cmake")
    # `sqrt` and the like as one instruction rather than a call that sets `errno`, with the same result: the tree
    # reads `errno` after system calls alone. The tree's, not the set's, whose code was not read for it.
    set(openmw_flags_common "${openmw_codegen_gnu} ${openmw_codegen_linux} -fno-math-errno")
    if(OPENMW_SANITIZER STREQUAL "address")
        string(APPEND openmw_flags_common " -fsanitize=address,undefined")
    elseif(OPENMW_SANITIZER STREQUAL "thread")
        string(APPEND openmw_flags_common " -fsanitize=thread")
    elseif(OPENMW_SANITIZER)
        message(FATAL_ERROR "OPENMW_SANITIZER is address, thread or empty, not ${OPENMW_SANITIZER}")
    endif()
    # libstdc++'s cheap checks, bounds on `operator[]`, `front()` and the like, which change no layout or ABI, so
    # the set's archives built without them link beside them (`_GLIBCXX_ASSERTIONS`, libstdc++'s "Macros").
    set(openmw_flags_relwithdebinfo "-O2 -D_GLIBCXX_ASSERTIONS ${openmw_flags_common}")
    set(openmw_flags_release "-O3 -DNDEBUG ${openmw_flags_common}")
endif()

foreach(language C CXX)
    set(CMAKE_${language}_FLAGS_RELWITHDEBINFO_INIT "${openmw_flags_relwithdebinfo}")
    set(CMAKE_${language}_FLAGS_RELEASE_INIT "${openmw_flags_release}")
endforeach()

# **Linux compiles with the pinned Clang and links with its lld, the set and the tree alike** (`vcpkg/linux.cmake`),
# whose flags name the libstdc++ every compile and link takes. lld for every link, since a tagged release's ThinLTO
# needs a linker that reads LLVM's bitcode, which mold does only through `LLVMgold.so`, which the pinned LLVM lacks.
# Static libraries are thin archives (LLVM's `ar T`, as it indexes bitcode members), which name their objects rather
# than copy them: copying the game's objects cost every relink seconds. GCC's C++ runtime is linked in, as every
# other library is, so a program runs on a system whose libstdc++ is older than the one it was built against (GCC's
# runtime library exception permits it); not under a sanitizer, whose runtime takes the allocator's place in a
# shared libstdc++ alone. A sanitizer's runtime is the shared one, found beside the pinned Clang: linked in, it sits
# ahead of what wraps its interceptors, and Crashpad's `pthread_create`, which calls on the next definition
# (`RTLD_NEXT`), went past it to the C library's, whose threads AddressSanitizer did not know when they were joined.
# Folding is `safe`, which leaves alone what may have its address compared.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_LINKER_TYPE LLD)
    set(openmw_link_flags "-Wl,--gc-sections")
    if(OPENMW_SANITIZER)
        string(APPEND openmw_link_flags " -shared-libsan -frtlib-add-rpath")
    else()
        string(APPEND openmw_link_flags " -static-libstdc++ -static-libgcc")
    endif()
    foreach(kind EXE SHARED)
        set(CMAKE_${kind}_LINKER_FLAGS_INIT "${openmw_link_flags}")
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE_INIT "-Wl,--icf=safe")
    endforeach()
    foreach(language C CXX)
        # Every compile and link, CMake's own checks of the compiler included: a link passes the flags too.
        set(CMAKE_${language}_FLAGS_INIT "${openmw_toolchain_linux}")
        foreach(lto "" _IPO)
            set(CMAKE_${language}_ARCHIVE_CREATE${lto}
                "\"${CMAKE_${language}_COMPILER_AR}\" qcT <TARGET> <LINK_FLAGS> <OBJECTS>"
            )
            set(CMAKE_${language}_ARCHIVE_APPEND${lto}
                "\"${CMAKE_${language}_COMPILER_AR}\" qT <TARGET> <LINK_FLAGS> <OBJECTS>"
            )
            set(CMAKE_${language}_ARCHIVE_FINISH${lto} "\"${CMAKE_${language}_COMPILER_RANLIB}\" <TARGET>")
        endforeach()
    endforeach()
endif()
