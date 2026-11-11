# What a configure chooses: whether it builds the tests, the sanitizer, and where an install puts things.
# The presets set these; a bare configure gets the defaults.

# The tests and the benchmarks, CMake's own switch, which every preset leaves on: every program is built
# in every flavour.
option(BUILD_TESTING "The tests and the benchmarks" ON)

# **A sanitizer over the whole build**, the fetched projects included: ThreadSanitizer sees a lock only in
# code it instrumented, so a library left out reports races on what it guards, and AddressSanitizer's
# reports carry the frames of every library compiled with frame pointers. In the compiler's flags
# rather than on `openmw-checks`, which reaches the tree's own targets alone. GCC's and Clang's, as
# MSVC's has not been measured on this tree.
set(OPENMW_SANITIZER "" CACHE STRING "A sanitizer over the whole build: address (with undefined behaviour's) or thread")
set_property(
    CACHE
        OPENMW_SANITIZER
    PROPERTY
        STRINGS
            ""
            address
            thread
)
if(OPENMW_SANITIZER)
    if(MSVC)
        message(
            FATAL_ERROR
            "OPENMW_SANITIZER is GCC's and Clang's: MSVC's sanitizers have not been measured on this tree"
        )
    endif()
    if(OPENMW_SANITIZER STREQUAL "address")
        set(sanitizer_flags "-fsanitize=address,undefined")
    elseif(OPENMW_SANITIZER STREQUAL "thread")
        set(sanitizer_flags "-fsanitize=thread")
    else()
        message(FATAL_ERROR "OPENMW_SANITIZER is address, thread or empty, not ${OPENMW_SANITIZER}")
    endif()
    string(APPEND CMAKE_C_FLAGS " ${sanitizer_flags}")
    string(APPEND CMAKE_CXX_FLAGS " ${sanitizer_flags}")
endif()

# A bare configure with no build type builds with line tables and the optimizer.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build" FORCE)
    set_property(
        CACHE
            CMAKE_BUILD_TYPE
        PROPERTY
            STRINGS
                Debug
                Release
                RelWithDebInfo
                MinSizeRel
    )
endif()

# **One portable layout on every system**: every file in one folder, found relative to the programs, the
# libraries a folder below them. The Windows folder and the AppImage are both laid out so.
include(GNUInstallDirs)
set(CMAKE_INSTALL_BINDIR ".")
set(CMAKE_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_DATADIR ".")
set(CMAKE_INSTALL_SYSCONFDIR ".")
if(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
endif()
