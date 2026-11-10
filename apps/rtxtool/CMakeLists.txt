# The harness: a library the tests reach into, and one executable that links the game.

add_library(openmw-rtxtool-lib STATIC)
target_sources(openmw-rtxtool-lib
    PRIVATE
        compare.cpp
        framed.cpp
        harnessfolder.cpp
        homekey.cpp
        instruments/amdgpu.cpp
        instruments/cardwatch.cpp
        instruments/contactsheet.cpp
        instruments/digest.cpp
        instruments/drivercache.cpp
        instruments/framehashes.cpp
        instruments/frametimes.cpp
        instruments/gpuclock.cpp
        instruments/jsontext.cpp
        instruments/nvml.cpp
        instruments/png.cpp
        instruments/reportfile.cpp
        instruments/scenedigest.cpp
        instruments/threadcounters.cpp
        model/benchrecord.cpp
        model/benchrun.cpp
        model/benchspec.cpp
        model/blockfile.cpp
        model/camerapath.cpp
        model/cameratrack.cpp
        model/cruise.cpp
        model/film.cpp
        model/measurewindow.cpp
        model/pictureshelf.cpp
        model/runrecord.cpp
        model/skycrossing.cpp
        model/stopdescription.cpp
        model/viewfile.cpp
        noise.cpp
        options.cpp
        picturemean.cpp
        runfolder.cpp
        skykeys.cpp
        verbs.cpp
    PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        compare.hpp
        framed.hpp
        harnessfolder.hpp
        homekey.hpp
        instruments/amdgpu.hpp
        instruments/cardwatch.hpp
        instruments/contactsheet.hpp
        instruments/digest.hpp
        instruments/drivercache.hpp
        instruments/framehashes.hpp
        instruments/frametimes.hpp
        instruments/gpuclock.hpp
        instruments/jsontext.hpp
        instruments/nvml.hpp
        instruments/png.hpp
        instruments/reportfile.hpp
        instruments/scenedigest.hpp
        instruments/threadcounters.hpp
        instruments/wholenumber.hpp
        model/benchrecord.hpp
        model/benchrun.hpp
        model/benchspec.hpp
        model/blockfile.hpp
        model/camerapath.hpp
        model/cameratrack.hpp
        model/cruise.hpp
        model/film.hpp
        model/maprules.hpp
        model/measurewindow.hpp
        model/pictureshelf.hpp
        model/runrecord.hpp
        model/skycrossing.hpp
        model/stopdescription.hpp
        model/viewfile.hpp
        noise.hpp
        numbervalue.hpp
        options.hpp
        picturemean.hpp
        runfolder.hpp
        skykeys.hpp
        verbs.hpp
)

# The processor's counters of a thread are Linux's `perf_event_open`; no other system gives a
# process its own, and reads none.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(openmw-rtxtool-lib PRIVATE instruments/threadcounterslinux.cpp)
else()
    target_sources(openmw-rtxtool-lib PRIVATE instruments/threadcountersnone.cpp)
endif()

# `PUBLIC`: the interface names Boost's option maps, an `SDLUtil` mode and the ray tracer's types.
target_link_libraries(openmw-rtxtool-lib PUBLIC
    Boost::program_options
    SDL3::SDL3
    openmw-rtx-vulkan
    openmw-components
)

# Named viewpoints and the suites that visit them, in the harness's folder (`RTX_HARNESS_DIR`).
set(RTX_VIEWS_FILE views.cfg)
set(RTX_SUITES_FILE benches.cfg)
copy_all_resource_files("${PROJECT_SOURCE_DIR}/files/rtx" "${RTX_HARNESS_DIR}" "." "${RTX_VIEWS_FILE};${RTX_SUITES_FILE}")

# The keys a watched run answers to, as the harness's own data directory: `hosted.cpp` adds it
# wherever there is a window, and the played game never does.
set(RTX_KEYS_FOLDER vfs)
copy_all_resource_files("${PROJECT_SOURCE_DIR}/files/rtx/vfs" "${RTX_HARNESS_DIR}" "${RTX_KEYS_FOLDER}"
                        "rtxtool.omwscripts;scripts/rtx/hour.lua;scripts/rtx/keys.lua;scripts/rtx/sky.lua")

target_compile_definitions(openmw-rtxtool-lib PRIVATE
    OPENMW_RTX_HARNESS_DIR="${RTX_HARNESS_DIR}"
    OPENMW_RTX_VIEWS_FILE="${RTX_HARNESS_DIR}/${RTX_VIEWS_FILE}"
    OPENMW_RTX_SUITES_FILE="${RTX_HARNESS_DIR}/${RTX_SUITES_FILE}"
    OPENMW_RTX_KEYS_DIR="${RTX_HARNESS_DIR}/${RTX_KEYS_FOLDER}"
    OPENMW_RTX_SPIRV_SOURCE_DIR="${RTX_SPIRV_SOURCE_DIR}"
    OPENMW_RTX_SPIRV_CENSUS_DIR="${RTX_SPIRV_CENSUS_DIR}"
    OPENMW_RTX_SPIRV_CENSUS_SOURCE_DIR="${RTX_SPIRV_CENSUS_SOURCE_DIR}")

# The harness links the game, so a picture taken here is the picture played. Only the executable
# links it: `openmw-rtxtool-lib` stays engine-free for `components-tests`, so the hosted files are
# built here.
if (BUILD_OPENMW)
    openmw_add_executable(openmw-rtxtool main.cpp)
    target_sources(openmw-rtxtool
        PRIVATE
            cameradriver.cpp
            hosted.cpp
            measurer.cpp
            memorykey.cpp
            session.cpp
            stager.cpp
            standingnote.cpp
            stopwriter.cpp
        PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
            cameradriver.hpp
            hosted.hpp
            measurer.hpp
            memorykey.hpp
            session.hpp
            stager.hpp
            standingnote.hpp
            stopwriter.hpp
    )

    target_link_libraries(openmw-rtxtool openmw-rtxtool-lib openmw-lib)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool-lib PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool-lib PRIVATE gcov)
    if (BUILD_OPENMW)
        target_compile_options(openmw-rtxtool PRIVATE --coverage)
        target_link_libraries(openmw-rtxtool gcov)
    endif()
endif()
