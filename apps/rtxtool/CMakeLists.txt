set(RTXTOOL_FILES
    placement.cpp
    placement.hpp
    png.cpp
    png.hpp
    shot.cpp
    shot.hpp
    terrainstorage.cpp
    terrainstorage.hpp
    lighting.cpp
    lighting.hpp
    view.cpp
    view.hpp
    views.cpp
    views.hpp
    window.cpp
    window.hpp
    world.cpp
    world.hpp
)

source_group(apps\\rtxtool FILES ${RTXTOOL_FILES} main.cpp)

# A library so the tests can reach into it, the way apps/openmw builds openmw-lib.
add_library(openmw-rtxtool-lib STATIC ${RTXTOOL_FILES})

target_link_libraries(openmw-rtxtool-lib
    Boost::program_options
    SDL2::SDL2
    openmw-rtx
    openmw-rtx-vulkan
    openmw-rtx-bridge
    components
)

target_compile_options(openmw-rtxtool-lib PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

# Named viewpoints. Copied here because this is the only thing that reads them, and beside the
# backend's shaders under resources/rtx so one --resources finds both.
copy_resource_file("${OpenMW_SOURCE_DIR}/files/rtx/views.cfg" "${RTX_RESOURCES_ROOT}" "resources/rtx/views.cfg")

openmw_add_executable(openmw-rtxtool main.cpp)

target_link_libraries(openmw-rtxtool openmw-rtxtool-lib)

# The layers are on unless this is a Release build, the same condition the debug object names use.
# A frame that breaks a rule should say so the first time someone sees it rather than the first time
# someone thinks to ask: every one of this fork's own Vulkan bugs so far has been one the layers name
# outright once they are asked to look.
#
# On the executable rather than the library, because the default is `main.cpp`'s to apply and only
# the command line downstream of it ever sees one.
target_compile_definitions(openmw-rtxtool PRIVATE
                           $<$<NOT:$<CONFIG:Release>>:OPENMW_RTX_VALIDATION_BY_DEFAULT>)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool-lib PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool-lib gcov)
endif()

