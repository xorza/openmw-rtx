# What OpenGL does with what Vulkan exported. The only place that knows both.
#
# Its own component rather than a corner of `apps/openmw/mwrender/rtx/`, because the harness has to
# be able to prove the import works: a rendering change checked by opening the game window is a
# rendering change nobody checks.

set(RTXGL_FILES
    importedframe.cpp
    importedframe.hpp
)

source_group(components\\rtxgl FILES ${RTXGL_FILES})

add_library(openmw-rtx-gl STATIC ${RTXGL_FILES})

target_link_libraries(openmw-rtx-gl PUBLIC OpenGL::GL SDL2::SDL2)

target_compile_options(openmw-rtx-gl PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx-gl PRIVATE --coverage)
    target_link_libraries(openmw-rtx-gl gcov)
endif()
