# The Vulkan backend. Built only with -DOPENMW_RTX=ON.
#
# Everything here is Vulkan's: the device, its allocations, its acceleration structures and its
# passes. What is true of the content rather than of the API lives in `openmw-rtx`, which this links
# and which links no graphics API at all. See docs/rtx/backends.md.

# Ninja needs the NEW behaviour to consume glslc's depfile; the root project's minimum predates it.
if (POLICY CMP0116)
    cmake_policy(SET CMP0116 NEW)
endif()

find_package(Vulkan REQUIRED)

find_program(OPENMW_GLSLC NAMES glslc HINTS "${Vulkan_GLSLC_EXECUTABLE}" REQUIRED
    DOC "glslc, from shaderc — compiles the renderer's GLSL to SPIR-V")
find_program(OPENMW_SPIRV_VAL NAMES spirv-val REQUIRED
    DOC "spirv-val, from SPIRV-Tools — validates every module the build produces")

set(RTX_SHADERS
    shaders/atrous.comp
    shaders/composite.comp
    shaders/visibility.comp
)

# Compiling and validating in one custom command is deliberate: an invalid module must fail the
# build, never the frame. -MD gives ninja the include graph so editing a shared header rebuilds every
# shader that includes it.
set(RTX_SHADER_OUTPUTS)
set(RTX_SPIRV_DIR "${RTX_RESOURCES_ROOT}/resources/rtx/shaders")

# -g always: source correlation in Nsight is what makes a shader debuggable, and the debug info sits
# in the module rather than costing anything at runtime.
set(RTX_GLSLC_FLAGS -g $<IF:$<CONFIG:Debug>,-O0,-O>)

foreach (shader ${RTX_SHADERS})
    get_filename_component(name "${shader}" NAME)
    set(spv "${RTX_SPIRV_DIR}/${name}.spv")
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${name}.d")

    add_custom_command(
        OUTPUT "${spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RTX_SPIRV_DIR}"
        COMMAND "${OPENMW_GLSLC}" ${RTX_GLSLC_FLAGS} --target-env=vulkan1.4 -I "${RTX_SHADER_INCLUDE}"
                -MD -MF "${depfile}" -o "${spv}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        # --scalar-block-layout because the shared C++/GLSL headers rely on the feature the
        # device enables; without the flag the validator applies std430 straddling rules.
        COMMAND "${OPENMW_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout "${spv}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        DEPFILE "${depfile}"
        COMMENT "Compiling and validating ${name}"
        VERBATIM)

    list(APPEND RTX_SHADER_OUTPUTS "${spv}")
endforeach()

add_custom_target(openmw-rtx-vulkan-shaders ALL DEPENDS ${RTX_SHADER_OUTPUTS} SOURCES ${RTX_SHADERS})

set(RTX_VULKAN_FILES
    atrouspass.cpp
    atrouspass.hpp
    buffer.cpp
    buffer.hpp
    commands.cpp
    commands.hpp
    compositepass.cpp
    compositepass.hpp
    computepipeline.cpp
    computepipeline.hpp
    device.cpp
    device.hpp
    gbuffer.cpp
    gbuffer.hpp
    image.cpp
    image.hpp
    instance.cpp
    instance.hpp
    memory.cpp
    memory.hpp
    physicaldevice.cpp
    physicaldevice.hpp
    pipelinecache.cpp
    pipelinecache.hpp
    requirements.cpp
    requirements.hpp
    result.cpp
    result.hpp
    sceneacceleration.cpp
    sceneacceleration.hpp
    scenebuffers.cpp
    scenebuffers.hpp
    shadermodule.cpp
    shadermodule.hpp
    swapchain.cpp
    swapchain.hpp
    texture.cpp
    texture.hpp
    validation.cpp
    validation.hpp
    visibilitypass.cpp
    visibilitypass.hpp
    vulkanrenderer.cpp
    vulkanrenderer.hpp
)

source_group(components\\rtxvulkan FILES ${RTX_VULKAN_FILES})

add_library(openmw-rtx-vulkan STATIC ${RTX_VULKAN_FILES})

target_link_libraries(openmw-rtx-vulkan PUBLIC Vulkan::Vulkan openmw-rtx PRIVATE components)

target_compile_options(openmw-rtx-vulkan PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

add_dependencies(openmw-rtx-vulkan openmw-rtx-vulkan-shaders)

# Object names cost one call per object created, never per frame, and an unreadable capture is a
# debugging session that does not happen.
target_compile_definitions(openmw-rtx-vulkan PUBLIC $<$<NOT:$<CONFIG:Release>>:OPENMW_RTX_DEBUG_NAMES>)

target_compile_definitions(openmw-rtx-vulkan PUBLIC OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}")

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx-vulkan PRIVATE --coverage)
    target_link_libraries(openmw-rtx-vulkan gcov)
endif()
