# The ray tracing renderer's API-neutral core. Built only with -DOPENMW_RTX=ON.
#
# **This library links no graphics API, and that is the point.** What belongs here is what is true
# of Morrowind's content, of light transport, or of what the scene *is* — written once and read by
# every backend. The linker is what keeps it that way: nothing here can include a Vulkan or Metal
# header, because neither is on the include path. See docs/rtx/backends.md.

set(RTX_FILES
    bluenoise.cpp
    bluenoise.hpp
    camera.cpp
    camera.hpp
    error.hpp
    instancerecord.cpp
    instancerecord.hpp
    lightgrid.cpp
    lightgrid.hpp
    renderer.hpp
    scenedesc.cpp
    scenedesc.hpp
    texturedata.hpp
    wavespectrum.cpp
    wavespectrum.hpp
)

# Structures and constants shared verbatim with every shader language, through `#ifdef __cplusplus`.
# They stay with the core rather than following the GLSL, because a Metal shader includes them too.
set(RTX_SHADER_HEADERS
    shaders/scene.h
    shaders/visibility.h
)

source_group(components\\rtx FILES ${RTX_FILES} ${RTX_SHADER_HEADERS})

add_library(openmw-rtx STATIC ${RTX_FILES} ${RTX_SHADER_HEADERS})

target_link_libraries(openmw-rtx PRIVATE components)

target_compile_options(openmw-rtx PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx PRIVATE --coverage)
    target_link_libraries(openmw-rtx gcov)
endif()
