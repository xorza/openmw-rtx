# The third-party code the tree fetches, in one place: each project at a pinned archive, and the
# system's own where one is installed and the declaration names `FIND_PACKAGE_ARGS`, which FetchContent
# tries first. A preset that must build the same everywhere sets `FETCHCONTENT_TRY_FIND_PACKAGE_MODE`
# to `NEVER`, as Windows does: upstream's prebuilt set carries none of these. Every fetched project is
# a system's to the tree (`SYSTEM`), whose warnings are its own, and builds only what the tree links
# (`EXCLUDE_FROM_ALL`). What the tree finds and never fetches, OpenSceneGraph, Bullet, MyGUI and the
# rest, the root finds beside its options.

include(FetchContent)

# Each project's own settings stay inside the block; Crashpad's folder leaves it, for
# `components/crashcatcher/CMakeLists.txt`, which builds one of its files into every executable.
block(SCOPE_FOR VARIABLES PROPAGATE CRASHPAD_SOURCE_DIR)
set(CMAKE_CXX_CLANG_TIDY "")

# SDL3: the system's where it has one, and the pinned release built static otherwise, as upstream's
# prebuilt set has none. Static, so a package carries it inside the binaries. Ahead of the tree's own
# definitions, which are not SDL's: Windows's `NOGDI` and `NOMB` take out what SDL's video is built on.
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA512=74a5ee1e5bba138a8daa710e200ac3585c60f696311c46252d86bc685c55e8271cd0d68439f8e13ca515b702d46ef58914d0f7b2352019870a5763ed79739e9a
    SOURCE_DIR fetched/sdl3
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS 3.4 CONFIG
)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
FetchContent_MakeAvailable(SDL3)

# Recast at the commit pinned here on every system, built in, and never a system's: the navigator's
# tiles are its output, and `NavMeshTileStore::sTileVersion` keys them by it, which a distribution's
# own build moves without a version to say so (`DetourNavigatorTileVersionTest`).
FetchContent_Declare(recastnavigation
    URL https://github.com/OpenMW/recastnavigation/archive/03259f3287ff8330f0d66fcd98d022edddffaa97.zip
    URL_HASH SHA512=63db2ac90904764d7f1466d3c95994ec51d777cddd870b70f8ef674c09ed166b3d16ffc4ac1d18d0eab865fda4bf51b6ea17049025b66a338d3cadd0f48dc712
    SOURCE_DIR fetched/recastnavigation
    SYSTEM
    EXCLUDE_FROM_ALL
)
set(BUILD_SHARED_LIBS OFF)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")
FetchContent_MakeAvailable(recastnavigation)

# yaml-cpp, built static where no system's is found. One commit after the 0.8.0 tag, for CMake 4.
FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/c2680200486572baf8221ba052ef50b58ecd816e.zip
    URL_HASH SHA512=9eb328e37ff547e7f681d64e3a8f268a8a5645cb1c258106d1d9f959eec498dfc7c466690c56f210ad7d5fb95b806a30c87ccbb019cccf5c3c6135cfc3c13fe1
    SOURCE_DIR fetched/yaml-cpp
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS 0.8 CONFIG
)
set(YAML_BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(yaml-cpp)

if (BUILD_COMPONENTS_TESTS OR BUILD_OPENCS_TESTS OR BUILD_OPENMW_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        URL_HASH SHA512=2712d6f6e7f38eb0c8d5ebba957588c70aff68ea433c7aa0b31bde8cacfbe47de908a6e5a99a021969da568f5075daa2c45fca602bbf56711d146d333b76562b
        SOURCE_DIR fetched/googletest
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS 1.10 NAMES GTest CONFIG
    )
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON)
    FetchContent_MakeAvailable(googletest)
endif ()

if (BUILD_BENCHMARKS)
    # 1.10, whose header no longer reads `__COUNTER__` in an `#if`, which Clang 23 counts a C2y extension
    # and 1.8's own `-pedantic-errors` made an error.
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.10.0.zip
        URL_HASH SHA512=9aaa9f7bbaa0dfa3a34932f31c0dad5cef5b5c15a7da2ac4e6567c29428b43e791f1d9a7c2541127cbde199d3e7c4a61516ae4c06db2962503082bb851bae8d0
        SOURCE_DIR fetched/benchmark
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS CONFIG
    )
    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)
    FetchContent_MakeAvailable(benchmark)
endif ()

# volk, which loads the Vulkan loader when the first instance is made rather than when the program
# starts: no program imports it, so the game starts on a machine with no loader or an old one and says
# why it cannot draw there, and the ray tracer refuses the loader by name (`Instance`). The system's
# package only at the SDK version the tree is built against or a later one, which volk's own version is: an older one lacks the Vulkan 1.4 entry points the backend calls
# (`vkCmdPushDescriptorSet`), and would be taken and then fail to compile.
set(RTX_VOLK_VERSION 357)
FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.${RTX_VOLK_VERSION}.0.tar.gz
    URL_HASH SHA512=44feaf57236d8f52d4bde05cf4ba59de5df4730b6c8dc38c278a3aa13d02d4bb48e11660832232f7122c2d55b65d60fe9b5f8fe6725ee63d5d5213e5275b7110
    SOURCE_DIR fetched/volk
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS ${RTX_VOLK_VERSION} CONFIG QUIET
)
set(VOLK_HEADERS_ONLY ON)
set(VOLK_PULL_IN_VULKAN OFF)
FetchContent_MakeAvailable(volk)

# Vulkan Memory Allocator, behind `device/memory/memory.hpp`: a system's only at the pinned version or a
# later one of its major.
set(RTX_VMA_VERSION 3.4.0)
FetchContent_Declare(VulkanMemoryAllocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v${RTX_VMA_VERSION}.tar.gz
    URL_HASH SHA512=563acbcd8912d10d92c23715eba7acf0e7c1683af36021f415b36f359c2cce065f3906e395c32282a8410ec5c8179fbcb6412935c6629a49357475d4b4410e2a
    SOURCE_DIR fetched/vma
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS ${RTX_VMA_VERSION} CONFIG QUIET
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

include("${CMAKE_CURRENT_LIST_DIR}/../extern/crashpad.cmake")
endblock()
