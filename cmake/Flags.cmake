# Every configuration's compile and link flags, for GCC, Clang and MSVC alike, read through
# `CMAKE_USER_MAKE_RULES_OVERRIDE` as CMake sets each language's defaults: the one place they are set, so
# no preset carries a flag.
#
# - RelWithDebInfo, which debug, asan and tsan build: optimised, with every assert.
# - Release, the build a number is quoted from and the release ships: CMake's own -O3 and NDEBUG.
#
# Both with line tables and frame pointers, so a profile walks the stack and a crash names a line in any of
# them; on MSVC the debug information is embedded in each object (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`),
# which ccache can cache, and every program links a PDB.

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(openmw_flags_relwithdebinfo "/O2 /Oy-")
    set(openmw_flags_release "/O2 /Ob2 /Oy- /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "/DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "/DEBUG")
else()
    set(openmw_frame_pointers "-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")
    set(openmw_flags_relwithdebinfo "-O2 -g1 ${openmw_frame_pointers}")
    set(openmw_flags_release "-O3 -DNDEBUG -g1 ${openmw_frame_pointers}")
endif()

foreach(language C CXX)
    set(CMAKE_${language}_FLAGS_RELWITHDEBINFO_INIT "${openmw_flags_relwithdebinfo}")
    set(CMAKE_${language}_FLAGS_RELEASE_INIT "${openmw_flags_release}")
endforeach()

# Linux links with mold, and its static libraries are thin archives (`ar T`), which name their objects
# rather than copy them: no library is installed, and copying the game's objects cost every relink seconds.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_LINKER_TYPE MOLD)
    foreach(language C CXX)
        set(CMAKE_${language}_ARCHIVE_CREATE "<CMAKE_AR> qcT <TARGET> <LINK_FLAGS> <OBJECTS>")
    endforeach()
endif()
