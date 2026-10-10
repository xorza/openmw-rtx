# The ray tracer's tests, included once `components-tests` exists. `rtx-gpu-tests` holds what opens a
# device and fails where there is none, so a box without a driver cannot pass by skipping.
# `RTX_TEST_SUPPORT` goes into both binaries, `RTX_GPU_TEST_SUPPORT` into the second.
set(RTX_TEST_FILES
    myguirtx/sharedtexture.cpp
    platform/linuxtext.cpp
    rtx/common/hashstate.cpp
    rtx/common/job.cpp
    rtx/common/monitor.cpp
    rtx/common/namedenum.cpp
    rtx/common/parallel.cpp
    rtx/common/result.cpp
    rtx/common/runs.cpp
    rtx/common/slots.cpp
    rtx/common/stepped.cpp
    rtx/common/worker.cpp
    rtx/environment/atmospheremesh.cpp
    rtx/environment/cloudmesh.cpp
    rtx/environment/skybuilder.cpp
    rtx/environment/starmesh.cpp
    rtx/environment/wavecascade.cpp
    rtx/environment/wavespectrum.cpp
    rtx/frame/bluenoise.cpp
    rtx/frame/camera.cpp
    rtx/frame/framepast.cpp
    rtx/frame/framesampling.cpp
    rtx/frame/reconstruction.cpp
    rtx/frame/specularalbedo.cpp
    rtx/frame/spritelistsize.cpp
    rtx/image/alphaimage.cpp
    rtx/image/colour.cpp
    rtx/image/mipchain.cpp
    rtx/image/shadingmap.cpp
    rtx/image/spritelight.cpp
    rtx/image/texels.cpp
    rtx/mirror/cells/cellgrid.cpp
    rtx/mirror/cells/cellring.cpp
    rtx/mirror/cells/groundreader.cpp
    rtx/mirror/cells/templatewalk.cpp
    rtx/mirror/drawableposer.cpp
    rtx/mirror/extractor/fixture.hpp
    rtx/mirror/extractor/lights.cpp
    rtx/mirror/extractor/materials.cpp
    rtx/mirror/extractor/particles.cpp
    rtx/mirror/extractor/frozen.cpp
    rtx/mirror/extractor/retire.cpp
    rtx/mirror/extractor/skinning.cpp
    rtx/mirror/extractor/stats.cpp
    rtx/mirror/extractor/walk.cpp
    rtx/mirror/lightbuilder.cpp
    rtx/mirror/meshreader.cpp
    rtx/mirror/mirroridentity.cpp
    rtx/mirror/nifsurface.cpp
    rtx/mirror/nodekind.cpp
    rtx/mirror/shading.cpp
    rtx/mirror/statereading.cpp
    rtx/preprocess/contentkey.cpp
    rtx/preprocess/contentpreprocessor.cpp
    rtx/preprocess/shape/creasesplit.cpp
    rtx/preprocess/shape/shapefold.cpp
    rtx/renderer/frameimage.cpp
    rtx/renderer/memoryreport.cpp
    rtx/renderer/notfinite.cpp
    rtx/renderer/png.cpp
    rtx/renderer/sceneuploader.cpp
    rtx/renderer/shaderdirectory.cpp
    rtx/scene/compositequeue.cpp
    rtx/scene/instancerecord.cpp
    rtx/scene/lightgrid.cpp
    rtx/scene/refusals.cpp
    rtx/scene/scenedesc.cpp
    rtx/scene/scenetextures.cpp
    rtx/shaders/brdf.cpp
    rtx/shaders/lights.cpp
    rtx/shaders/pixelgrid.cpp
    rtx/shaders/sharedconstants.cpp
    rtx/shaders/tangent.cpp
    rtx/sourcetree.cpp
    rtx/support/halfstep.cpp
    rtx/view/offscreentrace.cpp
    rtx/world/fogbuilder.cpp
    rtx/world/frameworld.cpp
    rtx/world/moon.cpp
    rtx/world/skylight.cpp
    rtx/world/sun.cpp
    rtxtool/benchrecord.cpp
    rtxtool/benchrun.cpp
    rtxtool/benchspec.cpp
    rtxtool/blockfile.cpp
    rtxtool/camerapath.cpp
    rtxtool/cameratrack.cpp
    rtxtool/cardwatch.cpp
    rtxtool/threadcounters.cpp
    rtxtool/compare.cpp
    rtxtool/contactsheet.cpp
    rtxtool/cruise.cpp
    rtxtool/drivercache.cpp
    rtxtool/film.cpp
    rtxtool/framehashes.cpp
    rtxtool/frametimes.cpp
    rtxtool/gpuclock.cpp
    rtxtool/homekey.cpp
    rtxtool/noise.cpp
    rtxtool/numbervalue.cpp
    rtxtool/measurewindow.cpp
    rtxtool/options.cpp
    rtxtool/picturemean.cpp
    rtxtool/run.cpp
    rtxtool/runrecord.cpp
    rtxtool/scenedigest.cpp
    rtxtool/skycrossing.cpp
    rtxvulkan/device/buffermarkers.cpp
    rtxvulkan/device/instance.cpp
    rtxvulkan/device/memory/formats.cpp
    rtxvulkan/device/memory/memorypriority.cpp
    rtxvulkan/device/memory/memorytypes.cpp
    rtxvulkan/device/physicaldevice.cpp
    rtxvulkan/device/requirements.cpp
    rtxvulkan/device/stagingfit.cpp
    rtxvulkan/pipeline/dispatch.cpp
    rtxvulkan/scene/sceneslots.cpp
    rtxvulkan/shaders/accumulate.cpp
    rtxvulkan/shaders/exposure.cpp
    rtxvulkan/shaders/hitrecords.cpp
    rtxvulkan/shaders/medium.cpp
    rtxvulkan/shaders/sharedconstants.cpp
    rtxvulkan/shaders/shadow.cpp
    rtxvulkan/pipeline/passbindings.cpp
    rtxvulkan/spirv/spirvinterface.cpp
    rtxvulkan/spirv/spirvdigest.cpp
    rtxvulkan/spirv/spirvfile.cpp
    rtxvulkan/spirv/spirvpin.cpp
    rtxvulkan/trace/denoise/temporalturns.cpp
    rtxvulkan/upscale/fsrframe.cpp
    sky/skyclock.cpp
    sky/sundisc.cpp
)

set(RTX_TEST_SUPPORT
    rtx/support/cardmemory.hpp
    rtx/support/countingrenderer.hpp
    rtx/support/death.hpp
    rtx/support/displaycurve.hpp
    rtx/support/fakeland.hpp
    rtx/support/fallbackseed.cpp
    rtx/support/geometry.hpp
    rtx/support/graph.hpp
    rtx/support/graphlight.hpp
    rtx/support/guiquad.hpp
    rtx/support/halfstep.hpp
    rtx/support/heldimages.hpp
    rtx/support/instanceobstacle.cpp
    rtx/support/instanceobstacle.hpp
    rtx/support/layers.hpp
    rtx/support/lobeintegrals.hpp
    rtx/support/mipchain.cpp
    rtx/support/mipchain.hpp
    rtx/support/ownedtexture.cpp
    rtx/support/ownedtexture.hpp
    rtx/support/pngtext.hpp
    rtx/support/sceneholds.hpp
    rtx/support/spritelightbake.cpp
    rtx/support/spritelightbake.hpp
    rtx/support/statistics.hpp
    rtx/support/testplatform.hpp
    rtx/support/testcamera.hpp
    rtx/support/testtexture.hpp
    rtx/support/wavemoments.hpp
)

set(RTX_GPU_TEST_SUPPORT
    rtx/support/device/harness.cpp
    rtx/support/device/harness.hpp
    rtx/support/device/heldsubmit.cpp
    rtx/support/device/heldsubmit.hpp
    rtx/support/device/memorylimits.cpp
    rtx/support/device/memorylimits.hpp
    rtx/support/device/readback.cpp
    rtx/support/device/readback.hpp
)

set(RTX_GPU_TEST_FILES
    rtxvulkan/device/commands.cpp
    rtxvulkan/device/device.cpp
    rtxvulkan/device/gputimer.cpp
    rtxvulkan/device/memory/barriers.cpp
    rtxvulkan/device/memory/buffer.cpp
    rtxvulkan/device/memory/memory.cpp
    rtxvulkan/device/memory/slottable.cpp
    rtxvulkan/device/memory/structurestorage.cpp
    rtxvulkan/device/pipelinecache.cpp
    rtxvulkan/device/halfstore.cpp
    rtxvulkan/device/probe.cpp
    rtxvulkan/device/readstamp.cpp
    rtxvulkan/device/unormround.cpp
    rtxvulkan/display/bloompass.cpp
    rtxvulkan/display/exposurepass.cpp
    rtxvulkan/digestpass.cpp
    rtxvulkan/framering.cpp
    rtxvulkan/frames.cpp
    rtxvulkan/present/presentfence.cpp
    rtxvulkan/gui/guipass.cpp
    rtxvulkan/gui/guitextures.cpp
    rtxvulkan/pipeline/computepipeline.cpp
    rtxvulkan/pipeline/shadercode.cpp
    rtxvulkan/pipeline/tracepipeline.cpp
    rtxvulkan/scene/bottomlevelstore.cpp
    rtxvulkan/scene/skinpass.cpp
    rtxvulkan/scene/toplevelpackpass.cpp
    rtxvulkan/spirv/pinnedarithmetic.cpp
    rtxvulkan/texture/bc7encodepass.cpp
    rtxvulkan/texture/groundcompositepass.cpp
    rtxvulkan/texture/mipchainpass.cpp
    rtxvulkan/texture/normalspreadpass.cpp
    rtxvulkan/texture/shadingpass.cpp
    rtxvulkan/texture/spritelightpass.cpp
    rtxvulkan/texture/texturearrival.cpp
    rtxvulkan/texture/texturearray.cpp
    rtxvulkan/trace/denoise/denoisehistory.cpp
    rtxvulkan/trace/ripplepass.cpp
    rtxvulkan/trace/spritepasses.cpp
    rtxvulkan/trace/stresspass.cpp
    rtxvulkan/trace/tracechain.cpp
    rtxvulkan/trace/visibility/filter.cpp
    rtxvulkan/trace/visibility/fixture.hpp
    rtxvulkan/trace/visibility/fog.cpp
    rtxvulkan/trace/visibility/frame.cpp
    rtxvulkan/trace/visibility/framecost.cpp
    rtxvulkan/trace/visibility/kernels.cpp
    rtxvulkan/trace/visibility/light.cpp
    rtxvulkan/trace/visibility/pane.cpp
    rtxvulkan/trace/visibility/sea.cpp
    rtxvulkan/trace/visibility/shadow.cpp
    rtxvulkan/trace/visibility/specular.cpp
    rtxvulkan/trace/visibility/trail.cpp
    rtxvulkan/trace/visibility/sky.cpp
    rtxvulkan/trace/visibility/sprites.cpp
    rtxvulkan/trace/visibility/surfaces.cpp
    rtxvulkan/trace/visibility/upscale.cpp
    rtxvulkan/trace/visibility/water.cpp
    rtxvulkan/trace/wavefield.cpp
    rtxvulkan/trace/wavepass.cpp
)

# One `Platform::Process` test per system, and the fifo and sysfs where the system has them.
if (WIN32)
    list(APPEND RTX_TEST_FILES platform/processwin32.cpp)
else()
    list(APPEND RTX_TEST_FILES platform/processposix.cpp rtxtool/perffifoposix.cpp)
endif()
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND RTX_TEST_FILES platform/kernelfile.cpp)
endif()

target_sources(components-tests PRIVATE ${RTX_TEST_FILES} ${RTX_TEST_SUPPORT})

# Vulkan too, for the tests that reach into the backend's headers, and the SPIR-V library, whose
# headers carry the opcodes the tests that write a module by hand spell.
target_link_libraries(components-tests openmw-rtx-vulkan openmw-rtxtool-lib openmw-rtx-spirv openmw-rtx-vulkan-api)

# Where the build wrote the shaders, told to the two test binaries alone: a build-tree path is no
# fact about the backend.
target_compile_definitions(components-tests PRIVATE OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}"
    OPENMW_RTX_CENSUS_SHADER_DIR="${RTX_SPIRV_CENSUS_DIR}")

# The same `main.cpp`, which sets up the settings' defaults both binaries read.
openmw_add_executable(rtx-gpu-tests main.cpp rtx/support/allocations.cpp
                      ${RTX_GPU_TEST_FILES} ${RTX_GPU_TEST_SUPPORT} ${RTX_TEST_SUPPORT})
target_sources(rtx-gpu-tests PRIVATE rtx/support/testplatform$<IF:$<BOOL:${WIN32}>,win32,posix>.cpp)
target_link_libraries(rtx-gpu-tests
    GTest::GTest
    GMock::GMock
    components
    openmw-rtx-vulkan openmw-rtxtool-lib openmw-rtx-spirv openmw-rtx-vulkan-api
)
target_compile_definitions(rtx-gpu-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}"
            OPENMW_RTX_SHADER_DIR="${RTX_SPIRV_DIR}"
            OPENMW_RTX_CENSUS_SHADER_DIR="${RTX_SPIRV_CENSUS_DIR}")
if (UNIX AND NOT APPLE)
    target_link_libraries(rtx-gpu-tests ${CMAKE_THREAD_LIBS_INIT})
endif()

# Beside the bundle and not in it, for the reason `apps/rtxtool` gives for the harness.
if (APPLE)
    set_target_properties(rtx-gpu-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}")
endif()
if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(rtx-gpu-tests PRIVATE --coverage)
    target_link_libraries(rtx-gpu-tests gcov)
endif()
