# What a configure chooses: the programs and suites it builds, the features compiled in, and where an
# install puts things. The presets set these; a bare configure gets the defaults.

option(OPENMW_BUILD_GAME "The game, openmw" ON)
option(OPENMW_BUILD_LAUNCHER "The launcher" ON)
option(OPENMW_BUILD_WIZARD "The installation wizard" ON)
option(OPENMW_BUILD_CS "The Construction Set, openmw-cs" ON)
option(OPENMW_BUILD_INIIMPORTER "The importer of Morrowind.ini" ON)
option(OPENMW_BUILD_ESSIMPORTER "The importer of Morrowind's saves" ON)
option(OPENMW_BUILD_BSATOOL "bsatool, which lists and extracts archives" ON)
option(OPENMW_BUILD_ESMTOOL "esmtool, which dumps content files" ON)
option(OPENMW_BUILD_NIFTEST "niftest, which loads every model of an archive" ON)
option(OPENMW_BUILD_NAVMESHTOOL "The navigation mesh generator" ON)
option(OPENMW_BUILD_BULLETOBJECTTOOL "The Bullet object tool" ON)
option(OPENMW_BUILD_DOCS "The Doxygen reference" OFF)
option(OPENMW_BUILD_COMPONENTS_TESTS "The components' tests and the ray tracer's" OFF)
option(OPENMW_BUILD_GAME_TESTS "The game's tests" OFF)
option(OPENMW_BUILD_CS_TESTS "The Construction Set's tests" OFF)
option(OPENMW_BUILD_BENCHMARKS "The benchmarks, with Google Benchmark" OFF)

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
        set(sanitizer_flags "-fsanitize=address,undefined -fno-omit-frame-pointer")
    elseif(OPENMW_SANITIZER STREQUAL "thread")
        set(sanitizer_flags "-fsanitize=thread -fno-omit-frame-pointer")
    else()
        message(FATAL_ERROR "OPENMW_SANITIZER is address, thread or empty, not ${OPENMW_SANITIZER}")
    endif()
    string(APPEND CMAKE_C_FLAGS " ${sanitizer_flags}")
    string(APPEND CMAKE_CXX_FLAGS " ${sanitizer_flags}")
endif()

# Qt for every program that draws a window of its own.
if(OPENMW_BUILD_LAUNCHER OR OPENMW_BUILD_CS OR OPENMW_BUILD_WIZARD OR OPENMW_BUILD_CS_TESTS)
    set(USE_QT TRUE)
else()
    set(USE_QT FALSE)
endif()

# libunshield for the wizard, which unpacks the installer's archives everywhere but on Windows, whose
# players run the installer itself.
if(NOT WIN32 AND OPENMW_BUILD_WIZARD)
    set(OPENMW_USE_UNSHIELD TRUE)
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
