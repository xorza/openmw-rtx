# The harness: a library the tests reach into, and one executable that links the game.

set(RTXTOOL_FILES
    compare.cpp
    compare.hpp
    film.cpp
    film.hpp
    harnessfolder.cpp
    harnessfolder.hpp
    homekey.cpp
    homekey.hpp
    instruments/amdgpu.cpp
    instruments/amdgpu.hpp
    instruments/cardwatch.cpp
    instruments/cardwatch.hpp
    instruments/contactsheet.cpp
    instruments/contactsheet.hpp
    instruments/digest.cpp
    instruments/digest.hpp
    instruments/drivercache.cpp
    instruments/drivercache.hpp
    instruments/framehashes.cpp
    instruments/framehashes.hpp
    instruments/frametimes.cpp
    instruments/frametimes.hpp
    instruments/gpuclock.cpp
    instruments/gpuclock.hpp
    instruments/nvml.cpp
    instruments/nvml.hpp
    instruments/scenedigest.cpp
    instruments/scenedigest.hpp
    instruments/threadcounters.cpp
    instruments/threadcounters.hpp
    model/benchrecord.cpp
    model/benchrecord.hpp
    model/benchrun.cpp
    model/benchrun.hpp
    model/benchspec.cpp
    model/benchspec.hpp
    model/blockfile.cpp
    model/blockfile.hpp
    model/camerapath.cpp
    model/camerapath.hpp
    model/cameratrack.cpp
    model/cameratrack.hpp
    model/cruise.cpp
    model/cruise.hpp
    model/measurewindow.cpp
    model/measurewindow.hpp
    model/runrecord.cpp
    model/runrecord.hpp
    model/skycrossing.cpp
    model/skycrossing.hpp
    model/wholenumber.hpp
    numbervalue.hpp
    options.cpp
    options.hpp
    picturemean.cpp
    picturemean.hpp
    run.cpp
    run.hpp
    skykeys.cpp
    skykeys.hpp
    verbs.cpp
    verbs.hpp
)

# What drives a hosted game: built into the executable alone, for the reason below.
set(RTXTOOL_HOSTED_FILES
    cameradriver.cpp
    cameradriver.hpp
    hosted.cpp
    measurer.cpp
    measurer.hpp
    session.cpp
    session.hpp
    stager.cpp
    stager.hpp
    standingnote.cpp
    standingnote.hpp
    stopwriter.cpp
    stopwriter.hpp
)

# The processor's counters of a thread are Linux's `perf_event_open`, which the POSIX half reads;
# no other system gives a process its own.
if (WIN32)
    list(APPEND RTXTOOL_FILES instruments/threadcounterswin32.cpp)
else()
    list(APPEND RTXTOOL_FILES instruments/threadcountersposix.cpp)
endif()

source_group(apps\\rtxtool FILES ${RTXTOOL_HOSTED_FILES} main.cpp)

# A library so the tests can reach into it, as `apps/openmw` builds `openmw-lib`.
add_library(openmw-rtxtool-lib STATIC ${RTXTOOL_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "apps\\rtxtool" FILES ${RTXTOOL_FILES})

# `PUBLIC`: the interface names Boost's option maps, an `SDLUtil` mode and the ray tracer's types.
target_link_libraries(openmw-rtxtool-lib PUBLIC
    Boost::program_options
    SDL3::SDL3
    openmw-rtx-vulkan
    components
)

target_compile_definitions(openmw-rtxtool-lib PRIVATE OPENMW_RTX_HARNESS_DIR="${RTX_HARNESS_DIR}")

# Named viewpoints and the suites that visit them, in the harness's folder (`RTX_HARNESS_DIR`).
copy_all_resource_files("${OpenMW_SOURCE_DIR}/files/rtx" "${RTX_HARNESS_DIR}" "." "views.cfg;benches.cfg")

# The keys a watched run answers to, as the harness's own data directory: `hosted.cpp` adds it
# wherever there is a window, and the played game never does.
copy_all_resource_files("${OpenMW_SOURCE_DIR}/files/rtx/vfs" "${RTX_HARNESS_DIR}" "vfs"
                        "rtxtool.omwscripts;scripts/rtx/hour.lua;scripts/rtx/keys.lua;scripts/rtx/sky.lua")

# The harness links the game, so a picture taken here is the picture played. Only the executable
# links it: `openmw-rtxtool-lib` stays engine-free for `components-tests`, so the hosted files are
# built here.
if (BUILD_OPENMW)
    openmw_add_executable(openmw-rtxtool main.cpp ${RTXTOOL_HOSTED_FILES})

    target_link_libraries(openmw-rtxtool openmw-rtxtool-lib openmw-lib)

    # Beside the bundle and not in it: on macOS every program is built into the bundle, which is
    # installed whole, and the harness only ever runs from the tree that built it. A multi-config
    # generator adds the configuration's directory to this as it does to every other program's.
    # No device on macOS traces rays, so no verb past `--help`, which reads no configuration, runs
    # there: the bundle's own `openmw.cfg`, which the binary's local path no longer finds, is not
    # read in its place.
    if (APPLE)
        set_target_properties(openmw-rtxtool PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}")
    endif()

endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool-lib PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool-lib PRIVATE gcov)
    if (BUILD_OPENMW)
        target_compile_options(openmw-rtxtool PRIVATE --coverage)
        target_link_libraries(openmw-rtxtool gcov)
    endif()
endif()
