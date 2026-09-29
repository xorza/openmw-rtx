# What this fork adds to the test binaries, listed here so that upstream's list stays upstream's.
# Paths are relative to `apps/components_tests`, which is where this is included from.
#
# Two binaries. `components-tests` holds what runs on any machine; `rtx-gpu-tests` holds what
# opens a device, and fails outright where there is none, so a run on a box without a driver
# cannot pass by skipping half the suite. `RTX_TEST_SUPPORT` is compiled into both and
# `RTX_GPU_TEST_SUPPORT` into the second alone, and two more lists at the end name the tests that
# take the flags differently.
set(RTX_TEST_FILES
    myguirtx/sharedtexture.cpp
    rtx/common/monitor.cpp
    rtx/common/namedenum.cpp
    rtx/common/parallel.cpp
    rtx/common/result.cpp
    rtx/common/runs.cpp
    rtx/common/slots.cpp
    rtx/common/stepped.cpp
    rtx/common/worker.cpp
    rtx/environment/cloudshell.cpp
    rtx/environment/fogbuilder.cpp
    rtx/environment/frameworld.cpp
    rtx/environment/moonbuilder.cpp
    rtx/environment/skybuilder.cpp
    rtx/environment/skylight.cpp
    rtx/environment/sun.cpp
    rtx/environment/wavecascade.cpp
    rtx/environment/wavespectrum.cpp
    rtx/frame/bluenoise.cpp
    rtx/frame/camera.cpp
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
    rtx/mirror/extractor/fixture.hpp
    rtx/mirror/extractor/lights.cpp
    rtx/mirror/extractor/materials.cpp
    rtx/mirror/extractor/particles.cpp
    rtx/mirror/extractor/retire.cpp
    rtx/mirror/extractor/skinning.cpp
    rtx/mirror/extractor/stats.cpp
    rtx/mirror/extractor/walk.cpp
    rtx/mirror/meshreader.cpp
    rtx/mirror/mirroridentity.cpp
    rtx/mirror/nodekind.cpp
    rtx/mirror/shading.cpp
    rtx/preprocess/contentkey.cpp
    rtx/preprocess/contentpreprocessor.cpp
    rtx/preprocess/shape/creasesplit.cpp
    rtx/preprocess/shape/shapefold.cpp
    rtx/renderer/frameimage.cpp
    rtx/renderer/memoryreport.cpp
    rtx/renderer/png.cpp
    rtx/renderer/sceneuploader.cpp
    rtx/renderer/shaderdirectory.cpp
    rtx/scene/compositequeue.cpp
    rtx/scene/instancerecord.cpp
    rtx/scene/lightbuilder.cpp
    rtx/scene/lightgrid.cpp
    rtx/scene/refusals.cpp
    rtx/scene/scenedesc.cpp
    rtx/scene/scenetextures.cpp
    rtx/scene/surface.cpp
    rtx/scene/tangent.cpp
    rtx/shaders/brdf.cpp
    rtx/shaders/exposure.cpp
    rtx/shaders/hitrecords.cpp
    rtx/shaders/pixelgrid.cpp
    rtx/sourcetree.cpp
    rtx/support/halfstep.cpp
    rtx/view/offscreentrace.cpp
    rtxtool/benchrecord.cpp
    rtxtool/benchrun.cpp
    rtxtool/benchspec.cpp
    rtxtool/blockfile.cpp
    rtxtool/camerapath.cpp
    rtxtool/cameratrack.cpp
    rtxtool/cardwatch.cpp
    rtxtool/compare.cpp
    rtxtool/contactsheet.cpp
    rtxtool/cruise.cpp
    rtxtool/drivercache.cpp
    rtxtool/film.cpp
    rtxtool/framehashes.cpp
    rtxtool/frametimes.cpp
    rtxtool/gpuclock.cpp
    rtxtool/homekey.cpp
    rtxtool/options.cpp
    rtxtool/run.cpp
    rtxtool/scenedigest.cpp
    rtxtool/skycrossing.cpp
    rtxvulkan/device/checkpoint.cpp
    rtxvulkan/device/instance.cpp
    rtxvulkan/device/memory/formats.cpp
    rtxvulkan/device/physicaldevice.cpp
    rtxvulkan/device/requirements.cpp
    rtxvulkan/pipeline/dispatch.cpp
    rtxvulkan/spirv/spirvdigest.cpp
    rtxvulkan/spirv/spirvfile.cpp
    rtxvulkan/spirv/spirvpin.cpp
    rtxvulkan/upscale/fsrframe.cpp
    sky/skyclock.cpp
    sky/sundisc.cpp
)

set(RTX_TEST_SUPPORT
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
    rtx/support/pngtext.hpp
    rtx/support/spritelightbake.cpp
    rtx/support/spritelightbake.hpp
    rtx/support/statistics.hpp
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
    rtxvulkan/device/memory/buffer.cpp
    rtxvulkan/device/memory/memory.cpp
    rtxvulkan/device/memory/slottable.cpp
    rtxvulkan/device/memory/structurestorage.cpp
    rtxvulkan/device/pipelinecache.cpp
    rtxvulkan/device/probe.cpp
    rtxvulkan/device/readstamp.cpp
    rtxvulkan/display/bloompass.cpp
    rtxvulkan/display/digestpass.cpp
    rtxvulkan/display/exposurepass.cpp
    rtxvulkan/framering.cpp
    rtxvulkan/frames.cpp
    rtxvulkan/gui/guipass.cpp
    rtxvulkan/gui/guitextures.cpp
    rtxvulkan/pipeline/computepipeline.cpp
    rtxvulkan/pipeline/tracepipeline.cpp
    rtxvulkan/scene/bottomlevelstore.cpp
    rtxvulkan/scene/skinpass.cpp
    rtxvulkan/scene/spritepasses.cpp
    rtxvulkan/spirv/pinnedarithmetic.cpp
    rtxvulkan/texture/groundcompositepass.cpp
    rtxvulkan/texture/mipchainpass.cpp
    rtxvulkan/texture/normalspreadpass.cpp
    rtxvulkan/texture/shadingpass.cpp
    rtxvulkan/texture/spritelightpass.cpp
    rtxvulkan/texture/texturearray.cpp
    rtxvulkan/trace/ripplepass.cpp
    rtxvulkan/trace/stresspass.cpp
    rtxvulkan/trace/visibility/filter.cpp
    rtxvulkan/trace/visibility/fixture.hpp
    rtxvulkan/trace/visibility/fog.cpp
    rtxvulkan/trace/visibility/frame.cpp
    rtxvulkan/trace/visibility/framecost.cpp
    rtxvulkan/trace/visibility/kernels.cpp
    rtxvulkan/trace/visibility/light.cpp
    rtxvulkan/trace/visibility/sea.cpp
    rtxvulkan/trace/visibility/shadow.cpp
    rtxvulkan/trace/visibility/specular.cpp
    rtxvulkan/trace/visibility/sky.cpp
    rtxvulkan/trace/visibility/sprites.cpp
    rtxvulkan/trace/visibility/surfaces.cpp
    rtxvulkan/trace/visibility/upscale.cpp
    rtxvulkan/trace/visibility/water.cpp
    rtxvulkan/trace/wavefield.cpp
    rtxvulkan/trace/waveline.cpp
    rtxvulkan/trace/wavepass.cpp
)


# What is read off a fifo, where the platform has one.
# `Platform::Process` as each system's shell answers it: one file a system, so neither asks which
# system it is on.
set(RTX_TEST_FILES_PROCESS_POSIX platform/processposix.cpp)
set(RTX_TEST_FILES_PROCESS_WIN32 platform/processwin32.cpp)
if (WIN32)
    list(APPEND RTX_TEST_FILES ${RTX_TEST_FILES_PROCESS_WIN32})
else()
    list(APPEND RTX_TEST_FILES ${RTX_TEST_FILES_PROCESS_POSIX})
endif()

set(RTX_TEST_FILES_FIFO rtxtool/perffifo.cpp)
if (NOT WIN32)
    list(APPEND RTX_TEST_FILES ${RTX_TEST_FILES_FIFO})
endif()

# Reads a NIF through upstream's loader, whose headers are not warning-free under the extra
# checks, so it takes the errors alone.
set(RTX_TEST_FILES_UPSTREAM
    rtx/scene/nifsurface.cpp
)

# This fork's tests of what a build without the ray tracer compiles as well, and the allocation
# counter they read. They stand in upstream's own list, so that build runs them too, and are named
# here for the fork's flags.
set(RTX_TEST_FILES_EITHER
    misc/frameclock.cpp
    rtx/support/allocations.cpp
    rtx/support/allocations.hpp
    sceneutil/paintedtexture.cpp
    terrain/refstack.cpp
)

openmw_rtx_expect_listed("${CMAKE_CURRENT_SOURCE_DIR}"
    MATCHING rtx/*.cpp rtx/*.hpp rtxvulkan/*.cpp rtxvulkan/*.hpp myguirtx/*.cpp myguirtx/*.hpp rtxtool/*.cpp
             rtxtool/*.hpp
    LISTED ${RTX_TEST_FILES} ${RTX_TEST_SUPPORT} ${RTX_GPU_TEST_FILES} ${RTX_GPU_TEST_SUPPORT}
           ${RTX_TEST_FILES_FIFO} ${RTX_TEST_FILES_UPSTREAM} ${RTX_TEST_FILES_EITHER})
list(APPEND UNITTEST_SRC_FILES ${RTX_TEST_FILES} ${RTX_TEST_SUPPORT} ${RTX_TEST_FILES_UPSTREAM})
