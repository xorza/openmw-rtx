cmake_minimum_required(VERSION 4.4)

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
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

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
include(OpenMWMacros)
include(escape_regex_control_characters)

# The version the programs report: the release, the API revisions scripts and shaders check, and on a
# Git checkout the commit, which `components/version` reads at every build.
set(OPENMW_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(OPENMW_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(OPENMW_VERSION_RELEASE ${PROJECT_VERSION_PATCH})
set(OPENMW_VERSION ${PROJECT_VERSION})
set(OPENMW_LUA_API_REVISION 166)
set(OPENMW_POSTPROCESSING_API_REVISION 5)
set(OPENMW_DOC_BASEURL "https://openmw.readthedocs.io/en/")
set(OPENMW_VERSION_COMMITHASH "")
set(OPENMW_VERSION_TAGHASH "")
set(OPENMW_VERSION_COMMITDATE "")
set(GIT_CHECKOUT FALSE)
if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
    find_package(Git)
    if(GIT_FOUND)
        set(GIT_CHECKOUT TRUE)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} log -1 --format=%as
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE failed
            OUTPUT_VARIABLE OPENMW_VERSION_COMMITDATE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(failed)
            set(OPENMW_VERSION_COMMITDATE "")
        endif()
    else()
        message(WARNING "Git executable not found")
    endif()
endif()

# moc and uic one at a time inside each target: Ninja already runs the targets in parallel, and CI
# measured the nested parallelism slower.
set(CMAKE_AUTOGEN_PARALLEL 1)

# `/Z7`: ccache cannot cache a compile that writes a shared PDB.
if(MSVC AND (CMAKE_C_COMPILER_LAUNCHER STREQUAL "ccache" OR CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "ccache"))
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

if(OPENMW_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT HAVE_IPO OUTPUT HAVE_IPO_OUTPUT)
    if(HAVE_IPO)
        message(STATUS "LTO enabled for Release configuration.")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "Requested option OPENMW_LTO not supported by this compiler: ${HAVE_IPO_OUTPUT}")
    endif()
endif()

include(cmake/Dependencies.cmake)

# Every program and its resources in one folder of the build, which the install copies from.
set(OPENMW_RESOURCES_ROOT "${PROJECT_BINARY_DIR}")
get_generator_is_multi_config(multi_config)
if(multi_config)
    set(RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>")
else()
    set(RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory(extern)
add_subdirectory(files)

add_subdirectory(components)

# The ray tracer's resources, where `components` places the rest. Two shader sets, because the
# driver keys its cache on the bytes: the renderers read modules without source, which an edited
# comment leaves unchanged (`RtxTool::DriverCache`), and a profiler reads the same modules with it
# (`--shader-source`).
set(RTX_SPIRV_DIR "${OPENMW_RESOURCES_ROOT}/resources/rtx/shaders")

# The harness's folder: its places, suites and scripts, the shaders with their source and the
# driver's caches. In the build tree, because no install carries the harness. The harness and the
# tests are built with each folder (`OPENMW_RTX_HARNESS_DIR` and its siblings), since the harness
# only ever runs from the tree that built it, and the driver reads them from `omw-folders.json`
# (`Build.folder`): this is the one place that names them.
set(RTX_HARNESS_DIR "${PROJECT_BINARY_DIR}/rtxtool")
set(RTX_SPIRV_SOURCE_DIR "${RTX_HARNESS_DIR}/shaders-source")

# The same two sets compiled with the census (`lib/census.glsl`), which every harness run but a
# measured one reads: the game's modules carry no census code at all, so they are not here.
set(RTX_SPIRV_CENSUS_DIR "${RTX_HARNESS_DIR}/shaders-census")
set(RTX_SPIRV_CENSUS_SOURCE_DIR "${RTX_HARNESS_DIR}/shaders-census-source")
add_subdirectory(components/rtxvulkan)

if(OPENMW_BUILD_GAME OR OPENMW_BUILD_GAME_TESTS)
    add_subdirectory(apps/openmw)
endif()
add_subdirectory(apps/rtxtool)
if(OPENMW_BUILD_BSATOOL)
    add_subdirectory(apps/bsatool)
endif()
if(OPENMW_BUILD_ESMTOOL)
    add_subdirectory(apps/esmtool)
endif()
if(OPENMW_BUILD_LAUNCHER)
    add_subdirectory(apps/launcher)
endif()
if(OPENMW_BUILD_INIIMPORTER)
    add_subdirectory(apps/mwiniimporter)
endif()
if(OPENMW_BUILD_ESSIMPORTER)
    add_subdirectory(apps/essimporter)
endif()
if(OPENMW_BUILD_CS OR OPENMW_BUILD_CS_TESTS)
    add_subdirectory(apps/opencs)
endif()
if(OPENMW_BUILD_WIZARD)
    add_subdirectory(apps/wizard)
endif()
if(OPENMW_BUILD_NIFTEST)
    add_subdirectory(apps/niftest)
endif()
if(OPENMW_BUILD_NAVMESHTOOL)
    add_subdirectory(apps/navmeshtool)
endif()
if(OPENMW_BUILD_BULLETOBJECTTOOL)
    add_subdirectory(apps/bulletobjecttool)
endif()
if(OPENMW_BUILD_COMPONENTS_TESTS)
    add_subdirectory(apps/components_tests)
endif()
if(OPENMW_BUILD_GAME_TESTS)
    add_subdirectory(apps/openmw_tests)
endif()
if(OPENMW_BUILD_CS_TESTS)
    add_subdirectory(apps/opencs_tests)
endif()
if(OPENMW_BUILD_BENCHMARKS)
    add_subdirectory(apps/benchmarks)
endif()
if(OPENMW_BUILD_DOCS)
    add_subdirectory(docs)
endif()

include(cmake/Tests.cmake)
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
