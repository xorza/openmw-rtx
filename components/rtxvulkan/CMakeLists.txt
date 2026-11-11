# The Vulkan backend: the device, its memory, its acceleration structures and its passes. What is
# true of the content lives in `components/rtx`, which links no graphics API.

find_program(
    OPENMW_GLSLC
    NAMES
        glslc
    HINTS
        "${Vulkan_GLSLC_EXECUTABLE}"
    REQUIRED
    DOC "glslc, from shaderc — compiles the renderer's GLSL to SPIR-V"
)
find_program(
    OPENMW_SPIRV_VAL
    NAMES
        spirv-val
    REQUIRED
    DOC "spirv-val, from SPIRV-Tools — validates every module the build produces"
)
find_program(
    OPENMW_SPIRV_OPT
    NAMES
        spirv-opt
    REQUIRED
    DOC "spirv-opt, from SPIRV-Tools — takes the source out of the modules the driver is handed"
)
find_path(
    OPENMW_SPIRV_HEADERS
    NAMES
        spirv/unified1/spirv.hpp
    HINTS
        ${Vulkan_INCLUDE_DIRS}
    REQUIRED
    DOC "SPIRV-Headers, from Khronos — the opcodes every shader's float arithmetic is pinned by"
)
# Headers without the fused multiply-add the pinning refuses by name fail here, not in
# `spirv/spirvpin.cpp`; they carry no version to ask.
file(STRINGS "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp" _openmw_spirv_fma REGEX "CapabilityFMAKHR = ")
if(NOT _openmw_spirv_fma)
    message(
        FATAL_ERROR
        "${OPENMW_SPIRV_HEADERS}/spirv/unified1/spirv.hpp names no CapabilityFMAKHR, which "
        "the shaders' pinning refuses by name: SPIRV-Headers from the Vulkan SDK 1.4.329 on"
    )
endif()
unset(_openmw_spirv_fma)

# The guard and the pinning every shader takes around the optimizer (`spirv/spirvpin.hpp`), and the
# digest `omw kernels` names kernels by: a library the tests reach into, and tools the build runs.
# They link nothing of the renderer's, which waits for the shaders, and stay out of every install.
add_library(openmw-rtx-spirv STATIC)
target_sources(
    openmw-rtx-spirv
    PRIVATE
        spirv/spirvinterface.cpp
        spirv/spirvfile.cpp
        spirv/spirvpin.cpp
        spirv/spirvdigest.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                spirv/spirvinterface.hpp
                spirv/spirvfile.hpp
                spirv/spirvpin.hpp
                spirv/spirvdigest.hpp
)
# Public, because `spirvfile.hpp`'s one walk of a module's words names the opcodes in its interface.
target_include_directories(openmw-rtx-spirv SYSTEM PUBLIC "${OPENMW_SPIRV_HEADERS}")
target_link_libraries(
    openmw-rtx-spirv
    PRIVATE
        smhasher
        openmw-platform-io
)
add_executable(openmw-rtx-spirv-pin spirv/spirvpintool.cpp)
target_link_libraries(openmw-rtx-spirv-pin PRIVATE openmw-rtx-spirv)
set_target_properties(
    openmw-rtx-spirv-pin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}"
)
add_executable(openmw-rtx-spirv-digest spirv/spirvdigesttool.cpp)
target_link_libraries(openmw-rtx-spirv-digest PRIVATE openmw-rtx-spirv)
set_target_properties(
    openmw-rtx-spirv-digest
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}"
)

set(RTX_SHADERS
    shaders/digest.comp
    shaders/display/bloomdown.comp
    shaders/display/bloomup.comp
    shaders/display/exposure.comp
    shaders/display/histogram.comp
    shaders/display/line.frag
    shaders/display/line.vert
    shaders/display/sunglare.comp
    shaders/display/tone.comp
    shaders/gui/gui.frag
    shaders/gui/gui.vert
    shaders/scene/morph.comp
    shaders/scene/skin.comp
    shaders/scene/spriterects.comp
    shaders/scene/spriteruns.comp
    shaders/scene/spriteshade.comp
    shaders/scene/spritestarts.comp
    shaders/scene/toplevelpack.comp
    shaders/texture/bc7encode.comp
    shaders/texture/groundcomposite.comp
    shaders/texture/normalspread.comp
    shaders/texture/spritelight.comp
    shaders/trace/denoise/accumulate.comp
    shaders/trace/denoise/accumulateclamp.comp
    shaders/trace/denoise/atrous.comp
    shaders/trace/denoise/atrouscompose.comp
    shaders/trace/denoise/composite.comp
    shaders/trace/denoise/historyclamp.comp
    shaders/trace/denoise/pane.comp
    shaders/trace/denoise/shadowfilter.comp
    shaders/trace/denoise/shadowmask.comp
    shaders/trace/denoise/shadowtiles.comp
    shaders/trace/denoise/specular.comp
    shaders/upscale/rrguides.comp
    shaders/upscale/fsraccumulate.comp
    shaders/upscale/fsrchange.comp
    shaders/upscale/fsrchangepyramid.comp
    shaders/upscale/fsrinputs.comp
    shaders/upscale/fsrinstability.comp
    shaders/upscale/fsrlumapyramid.comp
    shaders/upscale/fsrreactivity.comp
    shaders/trace/bounce.comp
    shaders/trace/direct.comp
    shaders/trace/fogdepth.rgen
    shaders/trace/fogintegrate.comp
    shaders/trace/fogscatter.rgen
    shaders/trace/landing.comp
    shaders/trace/media.comp
    shaders/trace/panes.comp
    shaders/trace/ripplecompose.comp
    shaders/trace/ripplestep.comp
    shaders/trace/spritecomposite.rgen
    shaders/trace/spriteemitters.rgen
    shaders/trace/spriteshelter.rgen
    shaders/trace/stress.comp
    shaders/trace/unbounced.comp
    shaders/trace/visibility.comp
    shaders/trace/wavecolumns.comp
    shaders/trace/waverows.comp
)

# The device-behaviour probes, only where tests run them: an assumption about the hardware fails as
# a test rather than as a rendering mystery.
set(RTX_SHADER_PROBES
    shaders/probes/halfmean.comp
    shaders/probes/halfstore.comp
    shaders/probes/pinning.comp
    shaders/probes/probe.comp
    shaders/probes/shadowmoments.comp
    shaders/probes/share.comp
    shaders/probes/sharedexponent.comp
    shaders/probes/traceprobe.rgen
    shaders/probes/unormmean.comp
    shaders/probes/unormround.comp
)
if(BUILD_TESTING)
    list(APPEND RTX_SHADERS ${RTX_SHADER_PROBES})
endif()

# Compiled, pinned and validated in one command, so a bad module fails the build and never the frame.
# `-MD` gives Ninja the include graph, so editing a shared header rebuilds the shaders that read it.
set(RTX_SHADER_OUTPUTS)

# The shaders' root as well as the core's shared structures', so `lib/` and `shared/` are spelled
# alike in every folder; and the source root, from which a header in `shared/` spells the core's
# headers, because C++ reads it too and finds nothing beside it of the core's.
set(RTX_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(RTX_SHADER_INCLUDE "${PROJECT_SOURCE_DIR}/components/rtx/shaders")

# AMD's FSR headers, which the upscaler's passes include unchanged: `extern/fidelityfx/README.md`.
set(RTX_SHADER_VENDORED "${PROJECT_SOURCE_DIR}/extern/fidelityfx/gpu")

# `-g` always, into the source set, for Nsight's source view. The renderers' set strips it rather than
# compiling twice, so the two differ in nothing else; both are pinned before validation, so the
# validator reads what the driver gets.
#
# **Unoptimized by glslc, guarded, then optimized**, the same level for every flavour, so the debug
# build that runs every test computes the arithmetic of the release build a figure is quoted from.
# And the optimizer is handed the module guarded
# (`Rtx::guardFloatArithmetic`), because what `glslc -O` optimized it folded first: arithmetic the
# source does not state, before anything pinned it.
set(RTX_GLSLC_FLAGS
    -g
    -O0
)

# Every shader twice: the renderers' set, which carries no census, and the harness's, which counts
# into it (`lib/census.glsl`, `RTX_CENSUS`). A specialization constant cannot take the census out,
# because Vulkan holds a pipeline to every binding a module's code names, reached or not.
function(openmw_rtx_shader shader census sourcedDir strippedDir)
    cmake_path(GET shader FILENAME name)
    set(compiled "${CMAKE_CURRENT_BINARY_DIR}/compiled/census${census}/${name}.spv")
    set(sourced "${sourcedDir}/${name}.spv")
    set(spv "${strippedDir}/${name}.spv")
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/${name}.census${census}.d")

    add_custom_command(
        # The module glslc writes first, because it is the one its depfile names.
        OUTPUT
            "${compiled}"
            "${sourced}"
            "${spv}"
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory "${strippedDir}" "${sourcedDir}"
            "${CMAKE_CURRENT_BINARY_DIR}/compiled/census${census}"
        COMMAND
            "${OPENMW_GLSLC}" ${RTX_GLSLC_FLAGS} --target-env=vulkan1.4 -DRTX_CENSUS=${census} -I
            "${RTX_SHADER_INCLUDE}" -I "${RTX_SHADER_ROOT}" -I "${RTX_SHADER_VENDORED}" -I "${PROJECT_SOURCE_DIR}" -MD
            -MF "${depfile}" -o "${compiled}" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        COMMAND
            openmw-rtx-spirv-pin --guard "${compiled}"
        COMMAND
            "${OPENMW_SPIRV_OPT}" -O --target-env=vulkan1.4 --scalar-block-layout -o "${sourced}" "${compiled}"
        COMMAND
            openmw-rtx-spirv-pin "${sourced}"
        # --scalar-block-layout because the shared C++/GLSL headers rely on the feature the
        # device enables; without the flag the validator applies std430 straddling rules.
        COMMAND
            "${OPENMW_SPIRV_VAL}" --target-env vulkan1.4 --scalar-block-layout "${sourced}"
        # The validator has just read this module, so the optimizer need not read it again.
        COMMAND
            "${OPENMW_SPIRV_OPT}" --skip-validation --strip-debug --strip-nonsemantic -o "${spv}" "${sourced}"
        # The tool as well as the shader, so a change to what pinning does compiles every shader again.
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
            openmw-rtx-spirv-pin
        DEPFILE "${depfile}"
        COMMENT "Compiling, guarding, optimizing, pinning and validating ${name}, census ${census}"
        VERBATIM
    )

    set(RTX_SHADER_OUTPUTS
        ${RTX_SHADER_OUTPUTS}
        "${spv}"
        "${sourced}"
        PARENT_SCOPE
    )
endfunction()

foreach(shader ${RTX_SHADERS})
    openmw_rtx_shader("${shader}" 0 "${RTX_SPIRV_SOURCE_DIR}" "${RTX_SPIRV_DIR}")
    openmw_rtx_shader("${shader}" 1 "${RTX_SPIRV_CENSUS_SOURCE_DIR}" "${RTX_SPIRV_CENSUS_DIR}")
endforeach()

# A module no listed shader makes is removed: a directory's digest (`Rtx::digestShaders`) names the
# driver's cache of it.
foreach(
    directory
    "${RTX_SPIRV_DIR}"
    "${RTX_SPIRV_SOURCE_DIR}"
    "${RTX_SPIRV_CENSUS_DIR}"
    "${RTX_SPIRV_CENSUS_SOURCE_DIR}"
)
    file(GLOB built "${directory}/*.spv")
    foreach(module ${built})
        if(NOT module IN_LIST RTX_SHADER_OUTPUTS)
            file(REMOVE "${module}")
        endif()
    endforeach()
endforeach()

# The shaders' includes, named so an IDE lists them; glslc's depfile is what rebuilds on an edit.
set(RTX_SHADER_LIB
    shaders/lib/answer.glsl
    shaders/lib/basis.glsl
    shaders/lib/bindings.glsl
    shaders/lib/blocklevel.glsl
    shaders/lib/bloom.glsl
    shaders/lib/bluenoise.glsl
    shaders/lib/bounced.glsl
    shaders/lib/census.glsl
    shaders/lib/eyeshading.glsl
    shaders/lib/compose.glsl
    shaders/lib/counts.glsl
    shaders/lib/finite.glsl
    shaders/lib/fog.glsl
    shaders/lib/footprint.glsl
    shaders/lib/frame.glsl
    shaders/lib/froxel.glsl
    shaders/lib/geometry.glsl
    shaders/lib/gloss.glsl
    shaders/lib/ground.glsl
    shaders/lib/halfround.glsl
    shaders/lib/hash.glsl
    shaders/lib/historyclamp.glsl
    shaders/lib/lights.glsl
    shaders/lib/medium.glsl
    shaders/lib/normaldecode.glsl
    shaders/lib/peel.glsl
    shaders/lib/pixels.glsl
    shaders/lib/random.glsl
    shaders/lib/records.glsl
    shaders/lib/reproject.glsl
    shaders/lib/resolve.glsl
    shaders/lib/runningmean.glsl
    shaders/lib/runs.glsl
    shaders/lib/sharedexponent.glsl
    shaders/lib/sea.glsl
    shaders/lib/shading.glsl
    shaders/lib/share.glsl
    shaders/lib/shadowed.glsl
    shaders/lib/shadowword.glsl
    shaders/lib/sky.glsl
    shaders/lib/spritelist.glsl
    shaders/lib/sprites.glsl
    shaders/lib/stage.glsl
    shaders/lib/stagerecord.glsl
    shaders/lib/starfield.glsl
    shaders/lib/surfacematch.glsl
    shaders/lib/texturearray.glsl
    shaders/lib/texturing.glsl
    shaders/lib/traversal.glsl
    shaders/lib/turns.glsl
    shaders/lib/underwater.glsl
    shaders/lib/upscaledepth.glsl
    shaders/lib/variants.glsl
    shaders/lib/water.glsl
    shaders/lib/wavelines.glsl
    shaders/lib/wavetile.glsl
    shaders/trace/denoise/atrouslevel.glsl
    shaders/upscale/fsrcallbacks.glsl
)

add_custom_target(
    openmw-rtx-vulkan-shaders
    ALL
    DEPENDS
        ${RTX_SHADER_OUTPUTS}
    SOURCES
        ${RTX_SHADERS}
        ${RTX_SHADER_LIB}
)

add_library(openmw-rtx-vulkan STATIC)
target_sources(
    openmw-rtx-vulkan
    PRIVATE
        createrenderer.cpp
        device/buffermarkers.cpp
        device/commands.cpp
        device/device.cpp
        device/extensionfilter.cpp
        device/driverprofile.cpp
        device/gputimer.cpp
        device/graveyard.cpp
        device/handles.cpp
        device/instance.cpp
        device/loaderlayers.cpp
        device/notfinitecensus.cpp
        device/memory/accelerationstructure.cpp
        device/memory/barriers.cpp
        device/memory/blockedbuffer.cpp
        device/memory/buffer.cpp
        device/memory/descriptorsets.cpp
        device/memory/formats.cpp
        device/memory/growablebuffer.cpp
        device/memory/image.cpp
        device/memory/imagearena.cpp
        device/memory/joinedblocks.cpp
        device/memory/memory.cpp
        device/memory/structurestorage.cpp
        device/memory/uploads.cpp
        device/physicaldevice.cpp
        device/pipelinecache.cpp
        device/pipelinecode.cpp
        device/requirements.cpp
        device/result.cpp
        device/timeline.cpp
        device/zonecounters.cpp
        device/validation.cpp
        frame/digestpass.cpp
        display/bloompass.cpp
        display/displaychain.cpp
        display/exposurepass.cpp
        display/linepass.cpp
        display/sunglarepass.cpp
        display/tonepass.cpp
        framering.cpp
        frametracer.cpp
        gui/guidrawer.cpp
        gui/guipass.cpp
        gui/guitextures.cpp
        picturetracer.cpp
        pipeline/computepipeline.cpp
        pipeline/dispatch.cpp
        pipeline/graphicspipeline.cpp
        pipeline/pipeline.cpp
        pipeline/shadercode.cpp
        pipeline/tracepipeline.cpp
        present/presentfence.cpp
        present/presenter.cpp
        present/presenttarget.cpp
        present/surface.cpp
        present/swapchain.cpp
        scene/bottomlevelstore.cpp
        scene/devicescene.cpp
        scene/sceneacceleration.cpp
        scene/scenebuffers.cpp
        scene/sceneslots.cpp
        scene/skinpass.cpp
        scene/skintables.cpp
        scene/structurebuild.cpp
        scene/toplevelpackpass.cpp
        texture/bc7encodepass.cpp
        texture/blockencode.cpp
        texture/groundcompositepass.cpp
        texture/normalspreadpass.cpp
        texture/spritelightpass.cpp
        texture/texture.cpp
        texture/texturearrival.cpp
        texture/texturecost.cpp
        denoise/accumulatepass.cpp
        denoise/atrouspass.cpp
        denoise/compositepass.cpp
        denoise/denoisehistory.cpp
        denoise/denoisepasses.cpp
        denoise/historyclamppass.cpp
        denoise/panepass.cpp
        denoise/shadowpass.cpp
        denoise/specularpass.cpp
        trace/fogvolume.cpp
        frame/gbuffer.cpp
        trace/ripplepass.cpp
        trace/spritebin.cpp
        trace/spritepasses.cpp
        trace/stresspass.cpp
        trace/tracechain.cpp
        trace/tracemedia.cpp
        trace/tracepasses.cpp
        trace/tuplequeue.cpp
        trace/visibilitypass.cpp
        trace/wavepass.cpp
        upscale/fsrframe.cpp
        upscale/fsrupscaler.cpp
        upscale/upscaler.cpp
        vulkanrenderer.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                createrenderer.hpp
                device/bindingtable.hpp
                device/buffermarkers.hpp
                device/commands.hpp
                device/device.hpp
                upscale/dlssapplication.hpp
                device/extensionfilter.hpp
                device/driverprofile.hpp
                device/gputimer.hpp
                device/graveyard.hpp
                device/handles.hpp
                device/instance.hpp
                device/loaderlayers.hpp
                device/notfinitecensus.hpp
                device/memory/accelerationstructure.hpp
                device/memory/barriers.hpp
                device/memory/blockedbuffer.hpp
                device/memory/blocklist.hpp
                device/memory/buffer.hpp
                device/memory/bufferusage.hpp
                device/memory/descriptorsets.hpp
                device/memory/formats.hpp
                device/memory/frameslots.hpp
                device/memory/growablebuffer.hpp
                device/memory/image.hpp
                device/memory/imagearena.hpp
                device/memory/imageuse.hpp
                device/memory/joinedblocks.hpp
                device/memory/memory.hpp
                device/memory/slottable.hpp
                device/memory/structurestorage.hpp
                device/memory/uploads.hpp
                device/owned.hpp
                device/physicaldevice.hpp
                device/pipelinecache.hpp
                device/pipelinecode.hpp
                device/readstamp.hpp
                device/requirements.hpp
                device/result.hpp
                device/retiring.hpp
                device/timeline.hpp
                device/zonecounters.hpp
                device/validation.hpp
                frame/digestpass.hpp
                display/bloompass.hpp
                display/displaychain.hpp
                display/exposurepass.hpp
                display/linepass.hpp
                display/sunglarepass.hpp
                display/tonepass.hpp
                framering.hpp
                frametracer.hpp
                gui/guidrawer.hpp
                gui/guipass.hpp
                gui/guitextures.hpp
                picturetracer.hpp
                pipeline/computepipeline.hpp
                pipeline/dispatch.hpp
                pipeline/graphicspipeline.hpp
                pipeline/pipeline.hpp
                pipeline/shadercode.hpp
                pipeline/tracepipeline.hpp
                present/presentfence.hpp
                present/presenter.hpp
                present/presenttarget.hpp
                present/surface.hpp
                present/swapchain.hpp
                scene/bottomlevelstore.hpp
                scene/devicescene.hpp
                scene/placing.hpp
                scene/sceneacceleration.hpp
                scene/scenebuffers.hpp
                scene/scenepasses.hpp
                scene/sceneslots.hpp
                scene/skinpass.hpp
                scene/skintables.hpp
                scene/spritesource.hpp
                scene/structurebuild.hpp
                scene/toplevelpackpass.hpp
                shaders/shared/accumulate.h
                shaders/shared/atrous.h
                shaders/shared/bc7.h
                shaders/shared/blockencode.h
                shaders/shared/bindings.h
                shaders/shared/bloom.h
                shaders/shared/channeldigest.h
                shaders/shared/composite.h
                shaders/shared/counts.h
                shaders/shared/dlss.h
                shaders/shared/exposure.h
                shaders/shared/fields.h
                shaders/shared/fogvolume.h
                shaders/shared/fsr.h
                shaders/shared/glare.h
                shaders/shared/ground.h
                shaders/shared/gui.h
                shaders/shared/halfmean.h
                shaders/shared/halfstore.h
                shaders/shared/historyclamp.h
                shaders/shared/line.h
                shaders/shared/medium.h
                shaders/shared/normalspread.h
                shaders/shared/pane.h
                shaders/shared/pinning.h
                shaders/shared/probe.h
                shaders/shared/ripple.h
                shaders/shared/sets.h
                shaders/shared/shadow.h
                shaders/shared/shadowmoments.h
                shaders/shared/share.h
                shaders/shared/sharedexponent.h
                shaders/shared/skin.h
                shaders/shared/specular.h
                shaders/shared/spritebin.h
                shaders/shared/spritelight.h
                shaders/shared/spriteshade.h
                shaders/shared/stress.h
                shaders/shared/tables.h
                shaders/shared/tone.h
                shaders/shared/toplevelpack.h
                shaders/shared/traceconstants.h
                shaders/shared/unormmean.h
                shaders/shared/unormround.h
                shaders/shared/upscaleinputs.h
                shaders/shared/wavetransform.h
                texture/bc7encodepass.hpp
                texture/blockchain.hpp
                texture/blockencode.hpp
                texture/groundcompositepass.hpp
                texture/normalspreadpass.hpp
                texture/spritelightpass.hpp
                texture/texture.hpp
                texture/texturearrival.hpp
                texture/texturecost.hpp
                texture/texturepasses.hpp
                denoise/accumulatepass.hpp
                denoise/atrouspass.hpp
                denoise/compositepass.hpp
                denoise/denoised.hpp
                denoise/denoiseframe.hpp
                denoise/denoisehistory.hpp
                denoise/denoisepasses.hpp
                denoise/historyclamppass.hpp
                denoise/panepass.hpp
                denoise/shadowpass.hpp
                denoise/specularpass.hpp
                denoise/temporalturns.hpp
                trace/fogvolume.hpp
                frame/framestep.hpp
                frame/gbuffer.hpp
                trace/ripplepass.hpp
                trace/spritebin.hpp
                trace/spritepasses.hpp
                trace/stresspass.hpp
                trace/tracechain.hpp
                trace/tracemedia.hpp
                trace/tracepasses.hpp
                frame/tracepast.hpp
                trace/tracerecording.hpp
                trace/tracesubject.hpp
                trace/tuplequeue.hpp
                trace/visibilitypass.hpp
                trace/wavepass.hpp
                upscale/dlssupscaler.hpp
                upscale/fsrframe.hpp
                upscale/fsrupscaler.hpp
                upscale/upscaleinputs.hpp
                upscale/upscaler.hpp
                vulkanrenderer.hpp
)

target_sources(
    openmw-rtx-vulkan
    PRIVATE
        upscale/dlssapplication.cpp
        upscale/ngx.cpp
        upscale/dlssupscaler.cpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                upscale/ngx.hpp
                upscale/ngxlibrary.hpp
)
if(WIN32)
    target_sources(openmw-rtx-vulkan PRIVATE upscale/ngxlibrarywin32.cpp)
else()
    target_sources(openmw-rtx-vulkan PRIVATE upscale/ngxlibraryposix.cpp)
endif()

# How everything that calls Vulkan reaches it: the headers' types and volk's function pointers, and
# no prototypes, so a call that bypasses volk fails to compile rather than importing the loader. In
# `namespace volk`, because a pointer named `vkCreateDevice` in C is a symbol a layer the loader opens
# can bind its own import of that name to, where the game exports its symbols.
add_library(openmw-rtx-vulkan-api INTERFACE)
target_link_libraries(
    openmw-rtx-vulkan-api
    INTERFACE
        Vulkan::Headers
        volk::volk_headers
)
target_compile_definitions(
    openmw-rtx-vulkan-api
    INTERFACE
        VK_NO_PROTOTYPES
        VOLK_NAMESPACE
)

# Vulkan is private: a host reaches the backend through `createrenderer.hpp`, which names no Vulkan
# type. The tests link Vulkan themselves. Windows is where a breach fails; Linux's headers hide it.
target_link_libraries(
    openmw-rtx-vulkan
    PUBLIC
        openmw-rtx
    PRIVATE
        openmw-rtx-spirv
        openmw-rtx-vulkan-api
        SDL3::SDL3
)
if(WIN32)
    # `RegGetValueW`, which names the DriverStore folder NGX's core lives in.
    target_link_libraries(openmw-rtx-vulkan PRIVATE advapi32)
endif()

add_dependencies(openmw-rtx-vulkan openmw-rtx-vulkan-shaders)

target_link_libraries(openmw-rtx-vulkan PRIVATE GPUOpen::VulkanMemoryAllocator)

# Object names cost one call per object, never per frame, and make a capture readable. 0 or 1, so
# `Rtx::sDebugNames` needs no `#ifdef`.
target_compile_definitions(openmw-rtx-vulkan PUBLIC OPENMW_RTX_DEBUG_NAMES=$<IF:$<CONFIG:Release>,0,1>)
