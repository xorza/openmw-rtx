cmake_minimum_required(VERSION 4.4)

# Every configuration's flags, set as CMake sets each language's defaults.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Flags.cmake")

project(
    OpenMW
    VERSION 0.53.0
    LANGUAGES
        C
        CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The tree has no C++20 modules, and CMP0155's scan would preprocess every file twice.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Every preset names ccache, and a box without it compiles without it rather than failing every
# compile: a release's runner keeps no cache and has none installed.
foreach(language C CXX)
    if(CMAKE_${language}_COMPILER_LAUNCHER STREQUAL "ccache")
        find_program(OPENMW_CCACHE ccache)
        if(NOT OPENMW_CCACHE)
            message(STATUS "No ccache: ${language} compiles without a launcher")
            set(CMAKE_${language}_COMPILER_LAUNCHER "" CACHE STRING "" FORCE)
        endif()
    endif()
endforeach()

include(cmake/Options.cmake)
include(cmake/Checks.cmake)
include(cmake/Links.cmake)

# The version the programs report: the release (`project`), the API revisions scripts and shaders check, and
# the commit, which `components/version` reads at every build. A build is of a Git clone, as every build's
# set is named from Git (`omw`), and the version names its commit.
set(OPENMW_LUA_API_REVISION 166)
set(OPENMW_POSTPROCESSING_API_REVISION 5)
set(OPENMW_DOC_BASEURL "https://openmw.readthedocs.io/en/")
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/.git")
    message(FATAL_ERROR "Build from a Git clone: the version names its commit")
endif()
find_package(Git REQUIRED)

# moc and uic one at a time inside each target: Ninja already runs the targets in parallel, and CI
# measured the nested parallelism slower.
set(CMAKE_AUTOGEN_PARALLEL 1)

# What `cmake/Flags.cmake` asks of the compiler, refused here by name where it has none: position-independent
# programs, which MSVC's always are (`/DYNAMICBASE`) and CMake has no flag of its for, and link-time optimisation
# where a configure asks for it with CMake's own `CMAKE_INTERPROCEDURAL_OPTIMIZATION` (the package preset, which a
# tagged release builds).
if(NOT MSVC)
    include(CheckPIESupported)
    check_pie_supported(OUTPUT_VARIABLE pie_output)
    foreach(language C CXX)
        if(NOT CMAKE_${language}_LINK_PIE_SUPPORTED)
            message(FATAL_ERROR "The ${language} linker makes no position-independent program: ${pie_output}")
        endif()
    endforeach()
endif()
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    include(CheckIPOSupported)
    check_ipo_supported()
endif()

include(cmake/Dependencies.cmake)

# Every program and its resources in one folder of the build, which the install copies from.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

add_subdirectory(extern)
add_subdirectory(files)

add_subdirectory(components)

# The harness's folder: its places, suites and scripts, the shader sets and the driver's caches. In the
# build tree, because no install carries the harness. The harness and the tests are built with each
# folder (`OPENMW_RTX_HARNESS_DIR` and its siblings), since the harness only ever runs from the tree
# that built it, and the driver reads them from `omw-folders.json` (`Build.folder`): this is the one
# place that names them.
set(RTX_HARNESS_DIR "${PROJECT_BINARY_DIR}/rtxtool")

# Two shader sets, because the driver keys its cache on the bytes: the renderers read modules without
# source, which an edited comment leaves unchanged (`RtxTool::DriverCache`), and a profiler reads the
# same modules with it (`--shader-source`). The first is the one the game compiles in
# (`Rtx::shippedVulkanShaders`), the probes the tests run beside it.
set(RTX_SPIRV_DIR "${RTX_HARNESS_DIR}/shaders")
set(RTX_SPIRV_SOURCE_DIR "${RTX_HARNESS_DIR}/shaders-source")

# The same two sets compiled with the census (`lib/census.glsl`), which every harness run but a
# measured one reads: the game's modules carry no census code at all, so they are not here.
set(RTX_SPIRV_CENSUS_DIR "${RTX_HARNESS_DIR}/shaders-census")
set(RTX_SPIRV_CENSUS_SOURCE_DIR "${RTX_HARNESS_DIR}/shaders-census-source")

# What the suites write, their times and the crash matrix's runs (`cmake/Tests.cmake`).
set(RTX_TEST_OUTPUT_DIR "${PROJECT_BINARY_DIR}/test-output")
add_subdirectory(components/rtxvulkan)
add_subdirectory(apps/rtxbuild)

add_subdirectory(apps/openmw)
add_subdirectory(apps/rtxtool)
add_subdirectory(apps/bsatool)
add_subdirectory(apps/esmtool)
add_subdirectory(apps/launcher)
add_subdirectory(apps/mwiniimporter)
add_subdirectory(apps/essimporter)
add_subdirectory(apps/opencs)
add_subdirectory(apps/wizard)
add_subdirectory(apps/niftest)
add_subdirectory(apps/navmeshtool)
add_subdirectory(apps/bulletobjecttool)
if(BUILD_TESTING)
    add_subdirectory(apps/components_tests)
    add_subdirectory(apps/openmw_tests)
    add_subdirectory(apps/opencs_tests)
    add_subdirectory(apps/benchmarks)
    # After every folder that defines a test binary, as each test names one.
    include(cmake/Tests.cmake)
endif()

include(cmake/Install.cmake)

# The folders the driver reads (`Build.folder`), written out of the variables that name them: values a
# build states for a tool outside it, where a cache entry would also be one more route between the
# tree's own folders.
file(
    CONFIGURE
    OUTPUT "${PROJECT_BINARY_DIR}/omw-folders.json"
    CONTENT
        [[{
    "RTX_SPIRV_DIR": "@RTX_SPIRV_DIR@",
    "RTX_SPIRV_SOURCE_DIR": "@RTX_SPIRV_SOURCE_DIR@",
    "RTX_TEST_OUTPUT_DIR": "@RTX_TEST_OUTPUT_DIR@"
}
]]
    @ONLY
)

openmw_link_tree("${PROJECT_SOURCE_DIR}")
