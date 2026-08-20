# OpenSceneGraph in, `Rtx::SceneDesc` out. The only place that knows both worlds.

set(RTXBRIDGE_FILES
    sceneextractor.cpp
    sceneextractor.hpp
    texturebuilder.cpp
    texturebuilder.hpp
)

source_group(components\\rtxbridge FILES ${RTXBRIDGE_FILES})

add_library(openmw-rtx-bridge STATIC ${RTXBRIDGE_FILES})

target_link_libraries(openmw-rtx-bridge PUBLIC openmw-rtx components)

target_compile_options(openmw-rtx-bridge PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx-bridge PRIVATE --coverage)
    target_link_libraries(openmw-rtx-bridge gcov)
endif()

