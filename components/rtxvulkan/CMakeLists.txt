# The Vulkan backend.
#
# Everything here is Vulkan's: the device, its allocations, its acceleration structures and its
# passes. What is true of the content rather than of the API lives in `openmw-rtx`, which this links
# and which links no graphics API at all.

# Ninja needs the NEW behaviour to consume glslc's depfile; upstream's policy version predates it.
cmake_policy(SET CMP0116 NEW)

# NVIDIA's NGX SDK, which is what DLSS is. Prebuilt, licensed by NVIDIA rather than by this tree,
# and 750 MB — so it is found rather than vendored, submoduled or fetched: `cmake/FindNGX.cmake`,
# pointed at a checkout by `NGX_ROOT` the way every package is. `extern/` is for source this build
# compiles, and none of that is true of a static library and a set of signed feature libraries.
#
# **A hard error and not a quiet disable.** Compiling the feature out would put "is DLSS really
# here" in a definition only this directory can see, which every caller would then have to guess
# at. Asking for it and not having it is one message instead.
#
# **The version is exact and not a floor.** What is linked here is a prebuilt library and what
# ships beside it is signed, so "whatever was newest that day" is not a version anything can be
# compared against; the feature libraries carry the tag in their file names, which is what the
# module reads.
if (OPENMW_RTX_DLSS)
    find_package(NGX 310.9.1 EXACT REQUIRED)
endif()

find_program(OPENMW_GLSLC NAMES glslc HINTS "${Vulkan_GLSLC_EXECUTABLE}" REQUIRED
    DOC "glslc, from shaderc — compiles the renderer's GLSL to SPIR-V")
find_program(OPENMW_SPIRV_VAL NAMES spirv-val REQUIRED
    DOC "spirv-val, from SPIRV-Tools — validates every module the build produces")
find_program(OPENMW_SPIRV_OPT NAMES spirv-opt REQUIRED
    DOC "spirv-opt, from SPIRV-Tools — takes the source out of the modules the driver is handed")
find_path(OPENMW_SPIRV_HEADERS NAMES spirv/unified1/spirv.hpp HINTS ${Vulkan_INCLUDE_DIRS} REQUIRED
    DOC "SPIRV-Headers, from Khronos — the opcodes every shader's float arithmetic is pinned by")
# Headers old enough to lack the fused multiply-add the pinning writes are a configure that says so,
# rather than a compile that stops inside `spirv/spirvpin.cpp`. The headers carry no version to
# ask.
file(STRINGS "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp" _openmw_spirv_fma REGEX "OpFmaKHR = ")
if (NOT _openmw_spirv_fma)
    message(FATAL_ERROR "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp names no OpFmaKHR: the shaders' "
                        "float arithmetic is pinned with SPV_KHR_fma, from the Vulkan SDK 1.4.329 on")
endif()
unset(_openmw_spirv_fma)

# **The step every shader takes between glslc and the validator**, `spirv/spirvpin.hpp` says why: a
# library the tests reach into, and the tool the build runs on each module. It links nothing of the
# renderer's, because every shader waits for the tool and the renderer waits for the shaders; and the
# tool stays in this directory's build tree, which no install and no archive reads. **Beside it, the
# digest `omw kernels` names each specialized kernel by** (`spirv/spirvdigest.hpp`), in the same
# library and the same tree for the same reasons.
set(RTX_SPIRV_PIN_FILES spirv/spirvfile.cpp spirv/spirvfile.hpp spirv/spirvpin.cpp spirv/spirvpin.hpp
    spirv/spirvdigest.cpp spirv/spirvdigest.hpp)
openmw_rtx_library(openmw-rtx-spirv ${RTX_SPIRV_PIN_FILES})
target_include_directories(openmw-rtx-spirv SYSTEM PRIVATE "${OPENMW_SPIRV_HEADERS}")
target_link_libraries(openmw-rtx-spirv PRIVATE smhasher)
add_executable(openmw-rtx-spirv-pin spirv/spirvpintool.cpp)
openmw_rtx_target(openmw-rtx-spirv-pin)
target_link_libraries(openmw-rtx-spirv-pin PRIVATE openmw-rtx-spirv)
set_target_properties(openmw-rtx-spirv-pin PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(openmw-rtx-spirv-digest spirv/spirvdigesttool.cpp)
openmw_rtx_target(openmw-rtx-spirv-digest)
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
    shaders/trace/wavecompose.comp
    shaders/trace/waveform.comp
    shaders/trace/waveline.comp
)

# The device-behaviour probes, compiled only where something is going to run them. Nothing the
# renderer draws reaches these: they state an assumption about the hardware so that it can fail as a
# test rather than as a rendering mystery.
set(RTX_SHADER_PROBES shaders/probes/pinning.comp shaders/probes/probe.comp shaders/probes/traceprobe.rgen
    shaders/probes/traceprobe.rmiss)
if (BUILD_COMPONENTS_TESTS)
    list(APPEND RTX_SHADERS ${RTX_SHADER_PROBES})
endif()

# Compiling, pinning and validating in one custom command is deliberate: an invalid module, and one
# whose arithmetic cannot be pinned, must fail the build, never the frame. -MD gives ninja the include graph so editing a shared header rebuilds every
# shader that includes it.
set(RTX_SHADER_OUTPUTS)

# The shaders' own root as well as the shared structures', so `lib/` is spelled the same from
# every folder a shader sits in.
set(RTX_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# -g always, into the source set: source correlation in Nsight is what makes a shader debuggable.
# The set every renderer reads is the same module with the source stripped, for the reason
# `RTX_SPIRV_DIR` gives — and stripped rather than compiled twice, so the two can differ in nothing
# else. Pinned before either is validated, so the validator reads what the driver is handed.
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
                -I "${RTX_SHADER_ROOT}" -MD -MF "${depfile}" -o "${sourced}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
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

# A module of a shader the list no longer names is removed, so the two directories hold exactly the
# modules this build makes: a renderer never reads the rest, and the digest of a directory
# (`Rtx::digestShaders`) is what the driver's cache of it is named by.
foreach (directory "${RTX_SPIRV_DIR}" "${RTX_SPIRV_SOURCE_DIR}")
    file(GLOB built "${directory}/*.spv")
    foreach (module ${built})
        if (NOT module IN_LIST RTX_SHADER_OUTPUTS)
            file(REMOVE "${module}")
        endif()
    endforeach()
endforeach()

# What the trace's five shaders are made of. Included rather than compiled — each has its own entry
# point and these have none, and glslc's depfile is what makes editing one rebuild every shader that
# reads it. Named here so an IDE lists them beside the shaders they belong to, which is the only
# thing the build wants them for.
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
    shaders/lib/runs.glsl
    shaders/lib/sea.glsl
    shaders/lib/shading.glsl
    shaders/lib/sky.glsl
    shaders/lib/spritelist.glsl
    shaders/lib/sprites.glsl
    shaders/lib/starfield.glsl
    shaders/lib/texturearray.glsl
    shaders/lib/texturing.glsl
    shaders/lib/traversal.glsl
    shaders/lib/turns.glsl
    shaders/lib/underwater.glsl
    shaders/lib/variants.glsl
    shaders/lib/water.glsl
)

add_custom_target(openmw-rtx-vulkan-shaders ALL DEPENDS ${RTX_SHADER_OUTPUTS}
                  SOURCES ${RTX_SHADERS} ${RTX_SHADER_LIB})

set(RTX_VULKAN_FILES
    createrenderer.cpp
    createrenderer.hpp
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
    device/upscalerextensions.hpp
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
    pipeline/pipeline.hpp
    pipeline/tracepipeline.cpp
    pipeline/tracepipeline.hpp
    present/latencypacer.cpp
    present/latencypacer.hpp
    present/pacedmodes.cpp
    present/pacedmodes.hpp
    present/presenter.cpp
    present/presenter.hpp
    present/presenttargets.cpp
    present/presenttargets.hpp
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
    scene/spritebin.cpp
    scene/spritebin.hpp
    scene/spritepasses.cpp
    scene/spritepasses.hpp
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
    trace/denoise/accumulatehistory.cpp
    trace/denoise/accumulatehistory.hpp
    trace/denoise/accumulatepass.cpp
    trace/denoise/accumulatepass.hpp
    trace/denoise/atrouspass.cpp
    trace/denoise/atrouspass.hpp
    trace/denoise/compositepass.cpp
    trace/denoise/compositepass.hpp
    trace/fogvolume.cpp
    trace/fogvolume.hpp
    trace/gbuffer.cpp
    trace/gbuffer.hpp
    trace/ripplepass.cpp
    trace/ripplepass.hpp
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
    upscale/upscaler.hpp
    vulkanrenderer.cpp
    vulkanrenderer.hpp
)

# Only where the SDK is there to compile against: these are nothing but NGX, so a build without
# DLSS leaves them out rather than hollowing them with a guard. `upscale/upscaler.hpp` is the one
# seam the renderer reaches them through, and the build picks which of its two definitions links.
set(RTX_VULKAN_DLSS_FILES upscale/dlss.cpp upscale/dlss.hpp upscale/dlsspass.cpp upscale/dlsspass.hpp
    upscale/dlssupscaler.cpp upscale/dlssupscaler.hpp upscale/ngx.hpp upscale/ngxdispatch.cpp upscale/ngxdispatch.hpp)
set(RTX_VULKAN_NO_DLSS_FILES upscale/noupscaler.cpp)
if (OPENMW_RTX_DLSS)
    list(APPEND RTX_VULKAN_FILES ${RTX_VULKAN_DLSS_FILES})
else()
    list(APPEND RTX_VULKAN_FILES ${RTX_VULKAN_NO_DLSS_FILES})
endif()

openmw_rtx_library(openmw-rtx-vulkan ${RTX_VULKAN_FILES})
openmw_rtx_expect_listed("${CMAKE_CURRENT_SOURCE_DIR}" MATCHING *.cpp *.hpp shaders/*
    LISTED ${RTX_VULKAN_FILES} ${RTX_VULKAN_DLSS_FILES} ${RTX_VULKAN_NO_DLSS_FILES} ${RTX_SPIRV_PIN_FILES}
           spirv/spirvpintool.cpp spirv/spirvdigesttool.cpp ${RTX_SHADERS} ${RTX_SHADER_PROBES} ${RTX_SHADER_LIB})

# Vulkan is private: a host reaches the backend through `createrenderer.hpp`, which names no
# Vulkan type, so nothing a host compiles needs the headers.
# The tests reach into the backend and link Vulkan themselves. Linux hides a breach of this, its
# SDK headers sitting under `/usr/include`, and Windows is where one fails.
target_link_libraries(openmw-rtx-vulkan PUBLIC openmw-rtx PRIVATE openmw-rtx-spirv Vulkan::Vulkan SDL2::SDL2 components)

add_dependencies(openmw-rtx-vulkan openmw-rtx-vulkan-shaders)

# Vulkan Memory Allocator, the sub-allocator behind `device/memory/memory.hpp`: one header, reached
# by `device/memory/memory.cpp` and nothing else. The system's where a package is installed, which
# the declaration asks `find_package` for first, and the pinned release fetched into the build tree otherwise, the
# way `extern/` takes Bullet and MyGUI when the system has none. `SYSTEM`, because the library's
# warnings are not this tree's to fix; a package found is an imported target and system already.
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

# Object names cost one call per object created, never per frame, and an unreadable capture is a
# debugging session that does not happen. Defined in every build, as 0 or 1, so the one place that
# reads it, `Rtx::sDebugNames`, needs no `#ifdef`.
target_compile_definitions(openmw-rtx-vulkan PUBLIC OPENMW_RTX_DEBUG_NAMES=$<IF:$<CONFIG:Release>,0,1>)

if (OPENMW_RTX_DLSS)
    # The static half is the target; the shared half is the feature library it loads at runtime,
    # from the directory of the running executable (`upscale/dlss.cpp` says why that one), so a copy
    # sits beside every binary of the build and goes with the binaries an install ships. The SDK's own
    # name is kept, version and all: it is what NGX asks the directory for.
    target_link_libraries(openmw-rtx-vulkan PRIVATE NGX::NGX)
    get_filename_component(_ngx_feature_name "${NGX_FEATURE_LIBRARY}" NAME)
    copy_resource_file("${NGX_FEATURE_LIBRARY}" "${OpenMW_BINARY_DIR}" "${_ngx_feature_name}")
    install(FILES "${NGX_FEATURE_LIBRARY}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    unset(_ngx_feature_name)
endif()
