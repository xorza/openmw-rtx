# The harness: a library the tests reach into, and one executable that links the game.

set(RTXTOOL_FILES
    compare.cpp
    compare.hpp
    film.cpp
    film.hpp
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

# Named viewpoints and the suites that visit them, beside the shaders so one `--resources` finds
# both.
copy_all_resource_files("${OpenMW_SOURCE_DIR}/files/rtx" "${RTX_RESOURCES_ROOT}" "resources/rtx" "views.cfg;benches.cfg")

# The keys a watched run answers to, as the harness's own data directory: `hosted.cpp` adds it
# wherever there is a window, and the played game never does.
copy_all_resource_files("${OpenMW_SOURCE_DIR}/files/rtx/vfs" "${RTX_RESOURCES_ROOT}" "resources/rtx/vfs"
                        "rtxtool.omwscripts;scripts/rtx/keys.lua;scripts/rtx/sky.lua")

# The harness links the game, so a picture taken here is the picture played. Only the executable
# links it: `openmw-rtxtool-lib` stays engine-free for `components-tests`, so the hosted files are
# built here.
if (BUILD_OPENMW)
    openmw_add_executable(openmw-rtxtool main.cpp ${RTXTOOL_HOSTED_FILES})

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
