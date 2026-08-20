# The experimental ray tracing renderer. Built only with -DOPENMW_RTX=ON; nothing here is reachable
# from the rest of the tree without that. See plan.md.

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
    shaders/visibility.comp
)

# Compiling and validating in one custom command is deliberate: an invalid module must fail the
# build, never the frame. -MD gives ninja the include graph so editing a shared .glsl rebuilds every
# shader that includes it.
set(RTX_SHADER_OUTPUTS)
set(RTX_SPIRV_DIR "${OPENMW_RESOURCES_ROOT}/resources/rtx/shaders")

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
        COMMAND "${OPENMW_GLSLC}" ${RTX_GLSLC_FLAGS} --target-env=vulkan1.4 -MD -MF "${depfile}"
                -o "${spv}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        # --scalar-block-layout because the shared C++/GLSL headers rely on the feature the
        # device enables; without the flag the validator applies std430 straddling rules.
        COMMAND "${OPENMW_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout "${spv}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        DEPFILE "${depfile}"
        COMMENT "Compiling and validating ${name}"
        VERBATIM)

    list(APPEND RTX_SHADER_OUTPUTS "${spv}")
endforeach()

add_custom_target(openmw-rtx-shaders ALL DEPENDS ${RTX_SHADER_OUTPUTS} SOURCES ${RTX_SHADERS})

# Named viewpoints, beside the shaders under resources/rtx so one --resources finds both.
copy_resource_file("${OpenMW_SOURCE_DIR}/files/rtx/views.cfg" "${OPENMW_RESOURCES_ROOT}" "resources/rtx/views.cfg")

set(RTX_FILES
    buffer.cpp
    buffer.hpp
    commands.cpp
    commands.hpp
    device.cpp
    device.hpp
    error.cpp
    error.hpp
    image.cpp
    image.hpp
    instance.cpp
    instance.hpp
    memory.cpp
    memory.hpp
    physicaldevice.cpp
    physicaldevice.hpp
    requirements.cpp
    requirements.hpp
    sceneacceleration.cpp
    sceneacceleration.hpp
    scenedesc.cpp
    scenedesc.hpp
    shadermodule.cpp
    shadermodule.hpp
    swapchain.cpp
    swapchain.hpp
    validation.cpp
    validation.hpp
    visibilitypass.cpp
    visibilitypass.hpp
)

source_group(components\\rtx FILES ${RTX_FILES})

add_library(openmw-rtx STATIC ${RTX_FILES})

target_link_libraries(openmw-rtx PUBLIC Vulkan::Vulkan PRIVATE components)

target_compile_options(openmw-rtx PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS})

add_dependencies(openmw-rtx openmw-rtx-shaders)

# Object names cost one call per object created, never per frame, and an unreadable capture is a
# debugging session that does not happen.
target_compile_definitions(openmw-rtx PUBLIC $<$<NOT:$<CONFIG:Release>>:OPENMW_RTX_DEBUG_NAMES>)

target_compile_definitions(openmw-rtx PUBLIC OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}")

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx PRIVATE --coverage)
    target_link_libraries(openmw-rtx gcov)
endif()

