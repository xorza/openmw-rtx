# OpenSceneGraph in, `Rtx::SceneDesc` out. The only place that knows both worlds.

set(RTXBRIDGE_FILES
    fogbuilder.cpp
    fogbuilder.hpp
    lightbuilder.cpp
    lightbuilder.hpp
    sceneextractor.cpp
    sceneextractor.hpp
    texturebuilder.cpp
    texturebuilder.hpp
    waterbuilder.cpp
    waterbuilder.hpp
)

source_group(components\\rtxbridge FILES ${RTXBRIDGE_FILES})

add_library(openmw-rtx-bridge STATIC ${RTXBRIDGE_FILES})

# `openmw-rtx-vulkan` only because `buildTextures` still hands back a `TextureArray`. Nothing else
# here knows a graphics API, and docs/rtx/backends.md §5.5 is what takes this line away again.
target_link_libraries(openmw-rtx-bridge PUBLIC openmw-rtx openmw-rtx-vulkan components)

target_compile_options(openmw-rtx-bridge PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx-bridge PRIVATE --coverage)
    target_link_libraries(openmw-rtx-bridge gcov)
endif()

