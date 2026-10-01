# The Vulkan backend: the device, its memory, its acceleration structures and its passes. What is
# true of the content lives in `components/rtx`, which links no graphics API.

find_program(OPENMW_GLSLC NAMES glslc HINTS "${Vulkan_GLSLC_EXECUTABLE}" REQUIRED
    DOC "glslc, from shaderc — compiles the renderer's GLSL to SPIR-V")
find_program(OPENMW_SPIRV_VAL NAMES spirv-val REQUIRED
    DOC "spirv-val, from SPIRV-Tools — validates every module the build produces")
find_program(OPENMW_SPIRV_OPT NAMES spirv-opt REQUIRED
    DOC "spirv-opt, from SPIRV-Tools — takes the source out of the modules the driver is handed")
find_path(OPENMW_SPIRV_HEADERS NAMES spirv/unified1/spirv.hpp HINTS ${Vulkan_INCLUDE_DIRS} REQUIRED
    DOC "SPIRV-Headers, from Khronos — the opcodes every shader's float arithmetic is pinned by")
# Headers without the fused multiply-add the pinning writes fail here, not in `spirv/spirvpin.cpp`;
# they carry no version to ask.
file(STRINGS "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp" _openmw_spirv_fma REGEX "OpFmaKHR = ")
if (NOT _openmw_spirv_fma)
    message(FATAL_ERROR "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp names no OpFmaKHR: the shaders' "
                        "float arithmetic is pinned with SPV_KHR_fma, from the Vulkan SDK 1.4.329 on")
endif()
unset(_openmw_spirv_fma)

# The pinning every shader takes between glslc and the validator (`spirv/spirvpin.hpp`), and the
# digest `omw kernels` names kernels by: a library the tests reach into, and tools the build runs.
# They link nothing of the renderer's, which waits for the shaders, and stay out of every install.
set(RTX_SPIRV_PIN_FILES spirv/spirvfile.cpp spirv/spirvfile.hpp spirv/spirvpin.cpp spirv/spirvpin.hpp
    spirv/spirvdigest.cpp spirv/spirvdigest.hpp)
add_library(openmw-rtx-spirv STATIC ${RTX_SPIRV_PIN_FILES})
target_include_directories(openmw-rtx-spirv SYSTEM PRIVATE "${OPENMW_SPIRV_HEADERS}")
target_link_libraries(openmw-rtx-spirv PRIVATE smhasher)
add_executable(openmw-rtx-spirv-pin spirv/spirvpintool.cpp)
target_link_libraries(openmw-rtx-spirv-pin PRIVATE openmw-rtx-spirv)
set_target_properties(openmw-rtx-spirv-pin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(openmw-rtx-spirv-digest spirv/spirvdigesttool.cpp)
target_link_libraries(openmw-rtx-spirv-digest PRIVATE openmw-rtx-spirv)
set_target_properties(openmw-rtx-spirv-digest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

set(RTX_SHADERS
    shaders/display/bloomdown.comp
    shaders/display/bloomup.comp
    shaders/display/digest.comp
    shaders/display/exposure.comp
    shaders/display/histogram.comp
    shaders/display/line.frag
    shaders/display/line.vert
    shaders/display/tone.comp
    shaders/gui/gui.frag
    shaders/gui/gui.vert
    shaders/scene/morph.comp
    shaders/scene/skin.comp
    shaders/scene/spriterects.comp
    shaders/scene/spriteruns.comp
    shaders/scene/spriteshade.comp
    shaders/scene/spritestarts.comp
    shaders/texture/groundcomposite.comp
    shaders/texture/mipchain.comp
    shaders/texture/normalspread.comp
    shaders/texture/shadingmap.comp
    shaders/texture/shadingsum.comp
    shaders/texture/spritelight.comp
    shaders/trace/denoise/accumulate.comp
    shaders/trace/denoise/atrous.comp
    shaders/trace/denoise/composite.comp
    shaders/trace/denoise/pane.comp
    shaders/trace/denoise/shadowfilter.comp
    shaders/trace/denoise/shadowmask.comp
    shaders/trace/denoise/shadowtiles.comp
    shaders/trace/denoise/specular.comp
    shaders/upscale/fsraccumulate.comp
    shaders/upscale/fsrchange.comp
    shaders/upscale/fsrchangepyramid.comp
    shaders/upscale/fsrinputs.comp
    shaders/upscale/fsrinstability.comp
    shaders/upscale/fsrlumapyramid.comp
    shaders/upscale/fsrreactivity.comp
    shaders/trace/fogdepth.rgen
    shaders/trace/fogintegrate.comp
    shaders/trace/fogscatter.rgen
    shaders/trace/ripplecompose.comp
    shaders/trace/ripplestep.comp
    shaders/trace/spritecomposite.rgen
    shaders/trace/spriteemitters.rgen
    shaders/trace/spriteshelter.rgen
    shaders/trace/stress.comp
    shaders/trace/sunglare.comp
    shaders/trace/visibility.rahit
    shaders/trace/visibility.rgen
    shaders/trace/visibility.rmiss
    shaders/trace/visibilityhit.rchit
    shaders/trace/visibilityunshaded.rmiss
    shaders/trace/wavecompose.comp
    shaders/trace/waveform.comp
    shaders/trace/waveline.comp
)

# The device-behaviour probes, only where tests run them: an assumption about the hardware fails as
# a test rather than as a rendering mystery.
set(RTX_SHADER_PROBES shaders/probes/pinning.comp shaders/probes/probe.comp shaders/probes/traceprobe.rgen)
if (BUILD_COMPONENTS_TESTS)
    list(APPEND RTX_SHADERS ${RTX_SHADER_PROBES})
endif()

# Compiled, pinned and validated in one command, so a bad module fails the build and never the frame.
# `-MD` gives Ninja the include graph, so editing a shared header rebuilds the shaders that read it.
set(RTX_SHADER_OUTPUTS)

# The shaders' root as well as the shared structures', so `lib/` is spelled alike in every folder.
set(RTX_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(RTX_SHADER_INCLUDE "${OpenMW_SOURCE_DIR}/components/rtx/shaders")

# AMD's FSR headers, which the upscaler's passes include unchanged: `extern/fidelityfx/README.md`.
set(RTX_SHADER_VENDORED "${OpenMW_SOURCE_DIR}/extern/fidelityfx/gpu")

# `-g` always, into the source set, for Nsight's source view. The renderers' set strips it rather than
# compiling twice, so the two differ in nothing else; both are pinned before validation, so the
# validator reads what the driver gets.
set(RTX_GLSLC_FLAGS -g $<IF:$<CONFIG:Debug>,-O0,-O>)

foreach (shader ${RTX_SHADERS})
    get_filename_component(name "${shader}" NAME)
    set(sourced "${RTX_SPIRV_SOURCE_DIR}/${name}.spv")
    set(spv "${RTX_SPIRV_DIR}/${name}.spv")
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${name}.d")

    add_custom_command(
        # The module glslc writes first, because it is the one its depfile names.
        OUTPUT "${sourced}" "${spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${RTX_SPIRV_DIR}" "${RTX_SPIRV_SOURCE_DIR}"
        COMMAND "${OPENMW_GLSLC}" ${RTX_GLSLC_FLAGS} --target-env=vulkan1.4 -I "${RTX_SHADER_INCLUDE}"
                -I "${RTX_SHADER_ROOT}" -I "${RTX_SHADER_VENDORED}" -MD -MF "${depfile}" -o "${sourced}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        COMMAND openmw-rtx-spirv-pin "${sourced}"
        # --scalar-block-layout because the shared C++/GLSL headers rely on the feature the
        # device enables; without the flag the validator applies std430 straddling rules.
        COMMAND "${OPENMW_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout "${sourced}"
        # The validator has just read this module, so the optimizer need not read it again.
        COMMAND "${OPENMW_SPIRV_OPT}" --skip-validation --strip-debug --strip-nonsemantic -o "${spv}" "${sourced}"
        # The tool as well as the shader, so a change to what pinning does compiles every shader again.
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" openmw-rtx-spirv-pin
        DEPFILE "${depfile}"
        COMMENT "Compiling, pinning and validating ${name}"
        VERBATIM)

    list(APPEND RTX_SHADER_OUTPUTS "${spv}" "${sourced}")
endforeach()

# A module no listed shader makes is removed: a directory's digest (`Rtx::digestShaders`) names the
# driver's cache of it.
foreach (directory "${RTX_SPIRV_DIR}" "${RTX_SPIRV_SOURCE_DIR}")
    file(GLOB built "${directory}/*.spv")
    foreach (module ${built})
        if (NOT module IN_LIST RTX_SHADER_OUTPUTS)
            file(REMOVE "${module}")
        endif()
    endforeach()
endforeach()

# The shaders' includes, named so an IDE lists them; glslc's depfile is what rebuilds on an edit.
set(RTX_SHADER_LIB
    shaders/lib/basis.glsl
    shaders/lib/bindings.glsl
    shaders/lib/bloom.glsl
    shaders/lib/compose.glsl
    shaders/lib/counts.glsl
    shaders/lib/fog.glsl
    shaders/lib/footprint.glsl
    shaders/lib/frame.glsl
    shaders/lib/froxel.glsl
    shaders/lib/geometry.glsl
    shaders/lib/gloss.glsl
    shaders/lib/ground.glsl
    shaders/lib/hitrecord.glsl
    shaders/lib/lights.glsl
    shaders/lib/medium.glsl
    shaders/lib/normaldecode.glsl
    shaders/lib/payload.glsl
    shaders/lib/pixels.glsl
    shaders/lib/random.glsl
    shaders/lib/records.glsl
    shaders/lib/reproject.glsl
    shaders/lib/runningmean.glsl
    shaders/lib/runs.glsl
    shaders/lib/sea.glsl
    shaders/lib/shading.glsl
    shaders/lib/sky.glsl
    shaders/lib/spritelist.glsl
    shaders/lib/sprites.glsl
    shaders/lib/starfield.glsl
    shaders/lib/surfacematch.glsl
    shaders/lib/texturearray.glsl
    shaders/lib/texturing.glsl
    shaders/lib/traversal.glsl
    shaders/lib/turns.glsl
    shaders/lib/underwater.glsl
    shaders/lib/variants.glsl
    shaders/lib/water.glsl
    shaders/upscale/fsrcallbacks.glsl
)

add_custom_target(openmw-rtx-vulkan-shaders ALL DEPENDS ${RTX_SHADER_OUTPUTS}
                  SOURCES ${RTX_SHADERS} ${RTX_SHADER_LIB})

set(RTX_VULKAN_FILES
    createrenderer.cpp
    createrenderer.hpp
    device/bindingtable.hpp
    device/buffermarkers.cpp
    device/buffermarkers.hpp
    device/commands.cpp
    device/commands.hpp
    device/device.cpp
    device/device.hpp
    device/gputimer.cpp
    device/gputimer.hpp
    device/graveyard.cpp
    device/graveyard.hpp
    device/handles.cpp
    device/handles.hpp
    device/instance.cpp
    device/instance.hpp
    device/memory/accelerationstructure.cpp
    device/memory/accelerationstructure.hpp
    device/memory/barriers.cpp
    device/memory/barriers.hpp
    device/memory/blockedbuffer.cpp
    device/memory/blockedbuffer.hpp
    device/memory/blocklist.hpp
    device/memory/buffer.cpp
    device/memory/buffer.hpp
    device/memory/bufferusage.hpp
    device/memory/descriptorsets.cpp
    device/memory/descriptorsets.hpp
    device/memory/formats.cpp
    device/memory/formats.hpp
    device/memory/frameslots.hpp
    device/memory/growablebuffer.cpp
    device/memory/growablebuffer.hpp
    device/memory/image.cpp
    device/memory/image.hpp
    device/memory/imageuse.hpp
    device/memory/memory.cpp
    device/memory/memory.hpp
    device/memory/slottable.hpp
    device/memory/structurestorage.cpp
    device/memory/structurestorage.hpp
    device/owned.hpp
    device/physicaldevice.cpp
    device/physicaldevice.hpp
    device/pipelinecache.cpp
    device/pipelinecache.hpp
    device/readstamp.hpp
    device/requirements.cpp
    device/requirements.hpp
    device/result.cpp
    device/result.hpp
    device/retiring.hpp
    device/timeline.cpp
    device/timeline.hpp
    device/validation.cpp
    device/validation.hpp
    display/bloompass.cpp
    display/bloompass.hpp
    display/digestpass.cpp
    display/digestpass.hpp
    display/displaychain.cpp
    display/displaychain.hpp
    display/exposurepass.cpp
    display/exposurepass.hpp
    display/linepass.cpp
    display/linepass.hpp
    display/tonepass.cpp
    display/tonepass.hpp
    framering.cpp
    framering.hpp
    gui/guidrawer.cpp
    gui/guidrawer.hpp
    gui/guipass.cpp
    gui/guipass.hpp
    gui/guitextures.cpp
    gui/guitextures.hpp
    picturetracer.cpp
    picturetracer.hpp
    pipeline/computepipeline.cpp
    pipeline/computepipeline.hpp
    pipeline/dispatch.hpp
    pipeline/graphicspipeline.cpp
    pipeline/graphicspipeline.hpp
    pipeline/pipeline.cpp
    pipeline/pipeline.hpp
    pipeline/tracepipeline.cpp
    pipeline/tracepipeline.hpp
    present/presenter.cpp
    present/presenter.hpp
    present/presenttarget.cpp
    present/presenttarget.hpp
    present/swapchain.cpp
    present/swapchain.hpp
    scene/bottomlevelstore.cpp
    scene/bottomlevelstore.hpp
    scene/devicescene.cpp
    scene/devicescene.hpp
    scene/placing.hpp
    scene/sceneacceleration.cpp
    scene/sceneacceleration.hpp
    scene/scenebuffers.cpp
    scene/scenebuffers.hpp
    scene/scenepasses.hpp
    scene/sceneslots.cpp
    scene/sceneslots.hpp
    scene/skinpass.cpp
    scene/skinpass.hpp
    scene/skintables.cpp
    scene/skintables.hpp
    scene/spritesource.hpp
    scene/structurebuild.cpp
    scene/structurebuild.hpp
    texture/groundcompositepass.cpp
    texture/groundcompositepass.hpp
    texture/mipchainpass.cpp
    texture/mipchainpass.hpp
    texture/normalspreadpass.cpp
    texture/normalspreadpass.hpp
    texture/shadingpass.cpp
    texture/shadingpass.hpp
    texture/spritelightpass.cpp
    texture/spritelightpass.hpp
    texture/texture.cpp
    texture/texture.hpp
    texture/texturepasses.hpp
    trace/denoise/accumulatepass.cpp
    trace/denoise/accumulatepass.hpp
    trace/denoise/atrouspass.cpp
    trace/denoise/atrouspass.hpp
    trace/denoise/compositepass.cpp
    trace/denoise/compositepass.hpp
    trace/denoise/denoised.hpp
    trace/denoise/denoiseframe.hpp
    trace/denoise/denoisehistory.cpp
    trace/denoise/denoisehistory.hpp
    trace/denoise/denoisepasses.cpp
    trace/denoise/denoisepasses.hpp
    trace/denoise/panepass.cpp
    trace/denoise/panepass.hpp
    trace/denoise/shadowpass.cpp
    trace/denoise/shadowpass.hpp
    trace/denoise/specularpass.cpp
    trace/denoise/specularpass.hpp
    trace/denoise/temporalturns.hpp
    trace/fogvolume.cpp
    trace/fogvolume.hpp
    trace/gbuffer.cpp
    trace/gbuffer.hpp
    trace/ripplepass.cpp
    trace/ripplepass.hpp
    trace/spritebin.cpp
    trace/spritebin.hpp
    trace/spritepasses.cpp
    trace/spritepasses.hpp
    trace/stresspass.cpp
    trace/stresspass.hpp
    trace/sunglarepass.cpp
    trace/sunglarepass.hpp
    trace/tracechain.cpp
    trace/tracechain.hpp
    trace/tracemedia.cpp
    trace/tracemedia.hpp
    trace/tracepasses.cpp
    trace/tracepasses.hpp
    trace/tracerecording.hpp
    trace/visibilitypass.cpp
    trace/visibilitypass.hpp
    trace/wavepass.cpp
    trace/wavepass.hpp
    upscale/fsrframe.cpp
    upscale/fsrframe.hpp
    upscale/upscaler.cpp
    upscale/upscaler.hpp
    vulkanrenderer.cpp
    vulkanrenderer.hpp
)

add_library(openmw-rtx-vulkan STATIC ${RTX_VULKAN_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "components\\rtxvulkan"
    FILES ${RTX_VULKAN_FILES} ${RTX_SPIRV_PIN_FILES})

# Vulkan is private: a host reaches the backend through `createrenderer.hpp`, which names no Vulkan
# type. The tests link Vulkan themselves. Windows is where a breach fails; Linux's headers hide it.
target_link_libraries(openmw-rtx-vulkan PUBLIC components PRIVATE openmw-rtx-spirv Vulkan::Vulkan SDL2::SDL2)

add_dependencies(openmw-rtx-vulkan openmw-rtx-vulkan-shaders)

# Vulkan Memory Allocator, behind `device/memory/memory.hpp`: the system's package if installed, the
# pinned release otherwise, as `extern/` takes Bullet and MyGUI. `SYSTEM`: its warnings are not ours.
include(FetchContent)
FetchContent_Declare(VulkanMemoryAllocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA512=563acbcd8912d10d92c23715eba7acf0e7c1683af36021f415b36f359c2cce065f3906e395c32282a8410ec5c8179fbcb6412935c6629a49357475d4b4410e2a
    SOURCE_DIR fetched/vma
    SYSTEM
    FIND_PACKAGE_ARGS CONFIG QUIET
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)
target_link_libraries(openmw-rtx-vulkan PRIVATE GPUOpen::VulkanMemoryAllocator)

# Object names cost one call per object, never per frame, and make a capture readable. 0 or 1, so
# `Rtx::sDebugNames` needs no `#ifdef`.
target_compile_definitions(openmw-rtx-vulkan PUBLIC OPENMW_RTX_DEBUG_NAMES=$<IF:$<CONFIG:Release>,0,1>)

if (BUILD_WITH_CODE_COVERAGE)
    foreach (target openmw-rtx-spirv openmw-rtx-spirv-pin openmw-rtx-spirv-digest openmw-rtx-vulkan)
        target_compile_options(${target} PRIVATE --coverage)
        target_link_libraries(${target} PRIVATE gcov)
    endforeach()
endif()
