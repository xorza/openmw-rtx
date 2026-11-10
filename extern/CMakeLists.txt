# SPDX-License-Identifier: GPL-3.0-or-later

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL on)
set(CMAKE_CXX_CLANG_TIDY "")

if(NOT OPENMW_USE_SYSTEM_RECASTNAVIGATION)
    if(RECASTNAVIGATION_STATIC)
        set(BUILD_SHARED_LIBS OFF)
    else()
        set(BUILD_SHARED_LIBS ON)
    endif()

    set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
    set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
    set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")

    include(FetchContent)
    FetchContent_Declare(recastnavigation
        URL https://github.com/OpenMW/recastnavigation/archive/03259f3287ff8330f0d66fcd98d022edddffaa97.zip
        URL_HASH SHA512=63db2ac90904764d7f1466d3c95994ec51d777cddd870b70f8ef674c09ed166b3d16ffc4ac1d18d0eab865fda4bf51b6ea17049025b66a338d3cadd0f48dc712
        SOURCE_DIR fetched/recastnavigation
        # Its headers as a system's, as a distribution's install of it is: they are not written to this
        # tree's warnings.
        SYSTEM
    )
    FetchContent_MakeAvailable(recastnavigation)
endif()

add_subdirectory(smhasher)
add_subdirectory(rgbcx)

if (BUILD_BENCHMARKS AND NOT OPENMW_USE_SYSTEM_BENCHMARK)

    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)

    include(FetchContent)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
        URL_HASH SHA512=d73587ad9c49338749e1d117a6f8c7ff9c603a91a2ffa91a7355c7df7dea82710b9a810d34ddfef20973ecdc77092ec10fb2b4e4cc8d2e7810cbed79617b3828
        SOURCE_DIR fetched/benchmark
    )
    FetchContent_MakeAvailable(benchmark)
endif()

if (NOT OPENMW_USE_SYSTEM_YAML_CPP)
    if(YAML_CPP_STATIC)
        set(YAML_BUILD_SHARED_LIBS OFF)
    else()
        set(YAML_BUILD_SHARED_LIBS ON)
    endif()

    include(FetchContent)
    # One commit after the 0.8.0 tag for CMake 4.x compatibility
    FetchContent_Declare(yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/c2680200486572baf8221ba052ef50b58ecd816e.zip
        URL_HASH SHA512=9eb328e37ff547e7f681d64e3a8f268a8a5645cb1c258106d1d9f959eec498dfc7c466690c56f210ad7d5fb95b806a30c87ccbb019cccf5c3c6135cfc3c13fe1
        SOURCE_DIR fetched/yaml-cpp
    )
    FetchContent_MakeAvailable(yaml-cpp)

    if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
        target_precompile_headers(yaml-cpp PRIVATE <algorithm>)
    endif()
endif()

if ((BUILD_COMPONENTS_TESTS OR BUILD_OPENCS_TESTS OR BUILD_OPENMW_TESTS) AND NOT OPENMW_USE_SYSTEM_GOOGLETEST)

    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        URL_HASH SHA512=2712d6f6e7f38eb0c8d5ebba957588c70aff68ea433c7aa0b31bde8cacfbe47de908a6e5a99a021969da568f5075daa2c45fca602bbf56711d146d333b76562b
        SOURCE_DIR fetched/googletest
    )
    if (MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)

    add_library(GTest::GTest ALIAS gtest)
    add_library(GMock::GMock ALIAS gmock)
endif()

include(crashpad.cmake)
