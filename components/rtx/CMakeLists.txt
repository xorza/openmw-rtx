# The ray tracing renderer's API-neutral core.
#
# **This library links no graphics API, and that is the point.** What belongs here is what is true
# of Morrowind's content, of light transport, or of what the scene *is* — written once and read by
# every backend. The linker is what keeps it that way: nothing here can include a Vulkan header,
# because it is not on the include path.
#
# **OpenSceneGraph is not that boundary and never was.** The scene arrives as an `osg::Node` graph
# and the maths types are OSG's, so half this library reads one; what a backend gets handed is
# `SceneDesc` either way. The PNG writer is here for the same reason: it is the one piece of this
# fork that needs an image library, OSG is the one the engine already has, and both the harness and
# the game want to write a frame to a file.

set(RTX_FILES
    common/clock.hpp
    common/contract.hpp
    common/error.hpp
    common/finite.hpp
    common/menu.hpp
    common/monitor.hpp
    common/namedenum.hpp
    common/parallel.hpp
    common/pointhash.hpp
    common/radicalinverse.hpp
    common/result.hpp
    common/runs.cpp
    common/runs.hpp
    common/scratch.hpp
    common/slots.cpp
    common/slots.hpp
    common/stepped.hpp
    common/worker.hpp
    environment/cloudshell.cpp
    environment/cloudshell.hpp
    environment/fogbuilder.cpp
    environment/fogbuilder.hpp
    environment/frameworld.cpp
    environment/frameworld.hpp
    environment/moonbuilder.cpp
    environment/moonbuilder.hpp
    environment/nightsky.cpp
    environment/nightsky.hpp
    environment/skybuilder.cpp
    environment/skybuilder.hpp
    environment/skylight.cpp
    environment/skylight.hpp
    environment/wavecascade.cpp
    environment/wavecascade.hpp
    environment/wavespectrum.cpp
    environment/wavespectrum.hpp
    frame/bluenoise.cpp
    frame/bluenoise.hpp
    frame/camera.cpp
    frame/camera.hpp
    frame/frameextents.hpp
    frame/frameoptions.hpp
    frame/framesampling.cpp
    frame/framesampling.hpp
    frame/reconstruction.hpp
    frame/specularalbedo.cpp
    frame/specularalbedo.hpp
    frame/spritelistsize.cpp
    frame/spritelistsize.hpp
    frame/sunglare.cpp
    frame/sunglare.hpp
    frame/surfaceview.hpp
    frame/upscale.hpp
    image/alphaimage.cpp
    image/alphaimage.hpp
    image/colour.cpp
    image/colour.hpp
    image/colourblock.cpp
    image/colourblock.hpp
    image/formatcensus.cpp
    image/formatcensus.hpp
    image/imagedescription.cpp
    image/imagedescription.hpp
    image/mipchain.cpp
    image/mipchain.hpp
    image/ownedtexture.cpp
    image/ownedtexture.hpp
    image/shadingmap.cpp
    image/shadingmap.hpp
    image/spritelight.cpp
    image/spritelight.hpp
    image/texels.cpp
    image/texels.hpp
    image/texturedata.cpp
    image/texturedata.hpp
    image/textureencoding.hpp
    image/texturewrap.hpp
    mirror/cells/cellgrid.cpp
    mirror/cells/cellgrid.hpp
    mirror/cells/cellplacer.cpp
    mirror/cells/cellplacer.hpp
    mirror/cells/cellreader.cpp
    mirror/cells/cellreader.hpp
    mirror/cells/cellring.cpp
    mirror/cells/cellring.hpp
    mirror/cells/cellsupply.cpp
    mirror/cells/cellsupply.hpp
    mirror/cells/cellworld.hpp
    mirror/cells/groundreader.cpp
    mirror/cells/groundreader.hpp
    mirror/cells/held.cpp
    mirror/cells/held.hpp
    mirror/cells/prepared.hpp
    mirror/cells/templatewalk.cpp
    mirror/cells/templatewalk.hpp
    mirror/emitterresolver.cpp
    mirror/emitterresolver.hpp
    mirror/extractionstats.cpp
    mirror/extractionstats.hpp
    mirror/materialresolver.cpp
    mirror/materialresolver.hpp
    mirror/meshreader.cpp
    mirror/meshreader.hpp
    mirror/meshresolver.cpp
    mirror/meshresolver.hpp
    mirror/mirroridentity.hpp
    mirror/mirrorpass.hpp
    mirror/nodekind.cpp
    mirror/nodekind.hpp
    mirror/poseupdate.hpp
    mirror/sceneadopter.hpp
    mirror/sceneextractor.cpp
    mirror/sceneextractor.hpp
    mirror/shading.cpp
    mirror/shading.hpp
    mirror/worlddescent.hpp
    preprocess/contentcache.hpp
    preprocess/contentkey.cpp
    preprocess/contentkey.hpp
    preprocess/contentpass.hpp
    preprocess/contentpreprocessor.cpp
    preprocess/contentpreprocessor.hpp
    preprocess/contentstats.cpp
    preprocess/contentstats.hpp
    preprocess/meantexels.cpp
    preprocess/meantexels.hpp
    preprocess/shape/creasesplit.cpp
    preprocess/shape/creasesplit.hpp
    preprocess/shape/pockettree.cpp
    preprocess/shape/pockettree.hpp
    preprocess/shape/shapefold.cpp
    preprocess/shape/shapefold.hpp
    preprocess/shape/shapepass.cpp
    preprocess/shape/shapepass.hpp
    preprocess/texture/finesttexels.cpp
    preprocess/texture/finesttexels.hpp
    preprocess/texture/solidreach.cpp
    preprocess/texture/solidreach.hpp
    preprocess/texture/texelmean.cpp
    preprocess/texture/texelmean.hpp
    renderer/framedigest.hpp
    renderer/frameimage.cpp
    renderer/frameimage.hpp
    renderer/framespend.hpp
    renderer/guirenderer.hpp
    renderer/kernelprogress.hpp
    renderer/memoryreport.cpp
    renderer/memoryreport.hpp
    renderer/png.cpp
    renderer/png.hpp
    renderer/renderer.hpp
    renderer/sceneuploader.cpp
    renderer/sceneuploader.hpp
    renderer/shaderdirectory.cpp
    renderer/shaderdirectory.hpp
    renderer/slot.hpp
    scene/compositequeue.cpp
    scene/compositequeue.hpp
    scene/debuglines.hpp
    scene/deformertable.cpp
    scene/deformertable.hpp
    scene/instancerecord.cpp
    scene/instancerecord.hpp
    scene/light.hpp
    scene/lightbuilder.cpp
    scene/lightbuilder.hpp
    scene/lightgrid.cpp
    scene/lightgrid.hpp
    scene/material.hpp
    scene/materialtable.cpp
    scene/materialtable.hpp
    scene/mesh.hpp
    scene/meshtable.cpp
    scene/meshtable.hpp
    scene/placementtable.cpp
    scene/placementtable.hpp
    scene/refusal.hpp
    scene/refusals.cpp
    scene/refusals.hpp
    scene/ripple.hpp
    scene/scenedesc.cpp
    scene/scenedesc.hpp
    scene/scenetextures.cpp
    scene/scenetextures.hpp
    scene/specularlayout.hpp
    scene/sprite.hpp
    scene/surface.cpp
    scene/surface.hpp
    scene/tangent.cpp
    scene/tangent.hpp
    scene/texturetable.cpp
    scene/texturetable.hpp
    view/offscreentrace.cpp
    view/offscreentrace.hpp
    view/viewscene.cpp
    view/viewscene.hpp
)

# Structures and constants shared verbatim with the shader language, through `#ifdef __cplusplus`.
# They stay with the core rather than following the GLSL, because the host reads them as well.
set(RTX_SHADER_HEADERS
    shaders/accumulate.h
    shaders/atrous.h
    shaders/bindings.h
    shaders/bloom.h
    shaders/brdf.h
    shaders/camera.h
    shaders/colour.h
    shaders/composite.h
    shaders/counts.h
    shaders/digest.h
    shaders/exposure.h
    shaders/fogvolume.h
    shaders/gbuffer.h
    shaders/glare.h
    shaders/ground.h
    shaders/hosttypes.h
    shaders/line.h
    shaders/look.h
    shaders/mipchain.h
    shaders/normalspread.h
    shaders/octahedral.h
    shaders/portable.h
    shaders/pinning.h
    shaders/probe.h
    shaders/ripple.h
    shaders/scene.h
    shaders/sets.h
    shaders/shadingmap.h
    shaders/shadow.h
    shaders/skinning.h
    shaders/sky.h
    shaders/spritebin.h
    shaders/spritelight.h
    shaders/spriteshade.h
    shaders/storageformat.h
    shaders/stress.h
    shaders/tangent.h
    shaders/tone.h
    shaders/visibility.h
    shaders/wave.h
)

openmw_rtx_library(openmw-rtx ${RTX_FILES} ${RTX_SHADER_HEADERS})
openmw_rtx_expect_listed("${CMAKE_CURRENT_SOURCE_DIR}" MATCHING *.cpp *.hpp *.h LISTED ${RTX_FILES} ${RTX_SHADER_HEADERS})

# **`PUBLIC`, because the interface is not self-contained.** `mirror/sceneextractor.hpp` takes an
# `osg::NodeVisitor` and an `osgParticle::ParticleSystem`, and `scene/scenedesc.hpp` has always
# included `<osg/Vec3f>` — so every consumer needs the include paths this carries.
target_link_libraries(openmw-rtx PUBLIC components)

# **`PUBLIC`, because the constant it decides is in a header both hosts read.** The layers are on
# unless this is a Release build, the same condition the debug object names use, and
# `Rtx::sValidationByDefault` is where that lands. Here rather than on each host, so the game and the
# harness cannot come to disagree about when a frame is being checked. Defined in every build, as 0
# or 1, so the constant that reads it needs no `#ifdef`.
target_compile_definitions(openmw-rtx PUBLIC OPENMW_RTX_VALIDATION_BY_DEFAULT=$<IF:$<CONFIG:Release>,0,1>)
