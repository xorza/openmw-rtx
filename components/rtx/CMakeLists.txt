# The ray tracer's API-neutral core: one library a layer, in the order `docs/architecture.md` lists them,
# each linking the one before it, so a file that reaches a later layer fails to link and not only
# `RtxSourceTreeTest`. No graphics API is on these libraries' include path; the Vulkan backend is its own,
# `components/rtxvulkan`. `shaders/` holds what C++ and GLSL share, headers alone.

# One layer: its sources, its headers as a file set, and the layer below it.
function(openmw_rtx_layer name below)
    cmake_parse_arguments(PARSE_ARGV 2 layer "" "" "SOURCES;HEADERS")
    if(layer_SOURCES)
        add_library(openmw-rtx-${name} STATIC)
        target_sources(
            openmw-rtx-${name}
            PRIVATE
                ${layer_SOURCES}
            PUBLIC
                FILE_SET HEADERS
                    BASE_DIRS
                        "${PROJECT_SOURCE_DIR}"
                    FILES
                        ${layer_HEADERS}
        )
        target_link_libraries(openmw-rtx-${name} PUBLIC ${below})
    else()
        add_library(openmw-rtx-${name} INTERFACE)
        target_sources(
            openmw-rtx-${name}
            INTERFACE
                FILE_SET HEADERS
                    BASE_DIRS
                        "${PROJECT_SOURCE_DIR}"
                    FILES
                        ${layer_HEADERS}
        )
        target_link_libraries(openmw-rtx-${name} INTERFACE ${below})
    endif()
endfunction()

openmw_rtx_layer(
    shaders
    openmw-components
    HEADERS
        shaders/brdf.h
        shaders/camera.h
        shaders/colour.h
        shaders/digest.h
        shaders/gbuffer.h
        shaders/halving.h
        shaders/hosttypes.h
        shaders/look.h
        shaders/octahedral.h
        shaders/portable.h
        shaders/scene.h
        shaders/skinning.h
        shaders/sky.h
        shaders/storageformat.h
        shaders/tangent.h
        shaders/visibility.h
        shaders/wave.h
)

openmw_rtx_layer(
    common
    openmw-rtx-shaders
    SOURCES
        common/frametimehistogram.cpp
        common/runs.cpp
        common/slots.cpp
    HEADERS
        common/asserts.hpp
        common/cardaddress.hpp
        common/clock.hpp
        common/error.hpp
        common/finite.hpp
        common/frametimehistogram.hpp
        common/index.hpp
        common/job.hpp
        common/jobprogress.hpp
        common/mebibytes.hpp
        common/menu.hpp
        common/namedenum.hpp
        common/ownedby.hpp
        common/parallel.hpp
        common/pointhash.hpp
        common/radicalinverse.hpp
        common/runs.hpp
        common/scratch.hpp
        common/slots.hpp
        common/stepped.hpp
)

openmw_rtx_layer(
    image
    openmw-rtx-common
    SOURCES
        image/alphaimage.cpp
        image/blockdecode.cpp
        image/chainbuild.cpp
        image/colour.cpp
        image/colourblock.cpp
        image/formatcensus.cpp
        image/imagedescription.cpp
        image/madeimage.cpp
        image/shadingmap.cpp
        image/texels.cpp
        image/texturedata.cpp
        image/textureformat.cpp
    HEADERS
        image/alphaimage.hpp
        image/blockdecode.hpp
        image/chainbuild.hpp
        image/colour.hpp
        image/colourblock.hpp
        image/formatcensus.hpp
        image/imagedescription.hpp
        image/madeimage.hpp
        image/shadingmap.hpp
        image/specularlayout.hpp
        image/texels.hpp
        image/texturedata.hpp
        image/textureencoding.hpp
        image/textureformat.hpp
        image/texturewrap.hpp
)

openmw_rtx_layer(
    scene
    openmw-rtx-image
    SOURCES
        scene/compositequeue.cpp
        scene/deformertable.cpp
        scene/instancerecord.cpp
        scene/lightgrid.cpp
        scene/materialtable.cpp
        scene/meshtable.cpp
        scene/placementtable.cpp
        scene/refusals.cpp
        scene/scenedesc.cpp
        scene/texturetable.cpp
    HEADERS
        scene/compositequeue.hpp
        scene/deformertable.hpp
        scene/foldedshape.hpp
        scene/instancerecord.hpp
        scene/light.hpp
        scene/lightgrid.hpp
        scene/material.hpp
        scene/materialtable.hpp
        scene/mesh.hpp
        scene/meshtable.hpp
        scene/placementtable.hpp
        scene/refusal.hpp
        scene/refusals.hpp
        scene/ripple.hpp
        scene/rowhold.hpp
        scene/scenedesc.hpp
        scene/sceneroom.hpp
        scene/sprite.hpp
        scene/surface.hpp
        scene/texturetable.hpp
)

openmw_rtx_layer(
    preprocess
    openmw-rtx-scene
    SOURCES
        preprocess/contentpreprocessor.cpp
        preprocess/contentstats.cpp
        preprocess/imagefactcache.cpp
        preprocess/preparedimages.cpp
        preprocess/shape/creasesplit.cpp
        preprocess/shape/pockettree.cpp
        preprocess/shape/shapefold.cpp
        preprocess/shape/shapepass.cpp
        preprocess/texture/chainpass.cpp
        preprocess/texture/conversionpass.cpp
        preprocess/texture/texturepass.cpp
    HEADERS
        preprocess/contentcache.hpp
        preprocess/contentcodec.hpp
        preprocess/contentpass.hpp
        preprocess/contentpreprocessor.hpp
        preprocess/contentstats.hpp
        preprocess/imagefactcache.hpp
        preprocess/preparedimages.hpp
        preprocess/shape/creasesplit.hpp
        preprocess/shape/facecross.hpp
        preprocess/shape/pockettree.hpp
        preprocess/shape/probetable.hpp
        preprocess/shape/shapefold.hpp
        preprocess/shape/shapepass.hpp
        preprocess/texture/chainpass.hpp
        preprocess/texture/chainscratch.hpp
        preprocess/texture/conversionpass.hpp
        preprocess/texture/texturepass.hpp
        preprocess/threadcontent.hpp
)

openmw_rtx_layer(
    frame
    openmw-rtx-preprocess
    SOURCES
        frame/camera.cpp
        frame/framepast.cpp
        frame/framesampling.cpp
        frame/specularalbedo.cpp
        frame/spritelistsize.cpp
        frame/upscale.cpp
    HEADERS
        frame/bluenoise.hpp
        frame/camera.hpp
        frame/debuglines.hpp
        frame/frameextents.hpp
        frame/frameoptions.hpp
        frame/framepast.hpp
        frame/framesampling.hpp
        frame/reconstruction.hpp
        frame/specularalbedo.hpp
        frame/spritelistsize.hpp
        frame/surfaceview.hpp
        frame/upscale.hpp
)

openmw_rtx_layer(
    world
    openmw-rtx-frame
    SOURCES
        world/atmosphere.cpp
        world/fogbuilder.cpp
        world/frameworld.cpp
        world/moon.cpp
        world/skycontent.cpp
        world/skylight.cpp
        world/sunglare.cpp
        world/weather.cpp
    HEADERS
        world/atmosphere.hpp
        world/fogbuilder.hpp
        world/fognoise.hpp
        world/frameworld.hpp
        world/moon.hpp
        world/skycontent.hpp
        world/skylight.hpp
        world/sunglare.hpp
        world/weather.hpp
)

openmw_rtx_layer(
    renderer
    openmw-rtx-world
    SOURCES
        renderer/framedescriber.cpp
        renderer/frameimage.cpp
        renderer/memoryreport.cpp
        renderer/nullrenderer.cpp
        renderer/scenetextures.cpp
        renderer/sceneuploader.cpp
    HEADERS
        renderer/channel.hpp
        renderer/denoiserimage.hpp
        renderer/framedescriber.hpp
        renderer/framedigest.hpp
        renderer/frameimage.hpp
        renderer/framesinflight.hpp
        renderer/framespend.hpp
        renderer/framezone.hpp
        renderer/guirenderer.hpp
        renderer/memoryreport.hpp
        renderer/nullrenderer.hpp
        renderer/picturerenderer.hpp
        renderer/renderer.hpp
        renderer/sceneholder.hpp
        renderer/scenetextures.hpp
        renderer/sceneuploader.hpp
        renderer/shaderset.hpp
        renderer/slot.hpp
        renderer/verticalsync.hpp
)

openmw_rtx_layer(
    mirror
    openmw-rtx-renderer
    SOURCES
        mirror/cells/cellgrid.cpp
        mirror/cells/cellplacer.cpp
        mirror/cells/cellreader.cpp
        mirror/cells/cellring.cpp
        mirror/cells/cellsupply.cpp
        mirror/cells/groundreader.cpp
        mirror/cells/held.cpp
        mirror/cells/templatewalk.cpp
        mirror/chainkeys.cpp
        mirror/drawableposer.cpp
        mirror/emitterresolver.cpp
        mirror/extractionstats.cpp
        mirror/frozenroots.cpp
        mirror/lightbuilder.cpp
        mirror/materialresolver.cpp
        mirror/meshreader.cpp
        mirror/meshresolver.cpp
        mirror/nodekind.cpp
        mirror/posecull.cpp
        mirror/sceneextractor.cpp
        mirror/shading.cpp
        mirror/statereading.cpp
        mirror/surfacedescription.cpp
    HEADERS
        mirror/cells/cellgrid.hpp
        mirror/cells/cellplacer.hpp
        mirror/cells/cellreader.hpp
        mirror/cells/cellring.hpp
        mirror/cells/cellsupply.hpp
        mirror/cells/cellworld.hpp
        mirror/cells/groundcoversource.hpp
        mirror/cells/groundreader.hpp
        mirror/cells/held.hpp
        mirror/cells/nightday.hpp
        mirror/cells/prepared.hpp
        mirror/cells/readermemory.hpp
        mirror/cells/templatewalk.hpp
        mirror/chainkeys.hpp
        mirror/contentmemory.hpp
        mirror/drawableposer.hpp
        mirror/emitterresolver.hpp
        mirror/extractionstats.hpp
        mirror/frozenroots.hpp
        mirror/lightbuilder.hpp
        mirror/materialresolver.hpp
        mirror/meshreader.hpp
        mirror/meshresolver.hpp
        mirror/mirroridentity.hpp
        mirror/mirrorpass.hpp
        mirror/nodekind.hpp
        mirror/posecull.hpp
        mirror/poseupdate.hpp
        mirror/sceneadopter.hpp
        mirror/sceneextractor.hpp
        mirror/shading.hpp
        mirror/statereading.hpp
        mirror/surfacedescription.hpp
        mirror/walkcontext.hpp
        mirror/worlddescent.hpp
)

openmw_rtx_layer(
    prepare
    openmw-rtx-mirror
    SOURCES
        prepare/contentpreparation.cpp
        prepare/filestamps.cpp
        prepare/preparationdispatcher.cpp
        prepare/preparationmanifest.cpp
    HEADERS
        prepare/contentpreparation.hpp
        prepare/filestamps.hpp
        prepare/preparationdispatcher.hpp
        prepare/preparationmanifest.hpp
)

openmw_rtx_layer(
    environment
    openmw-rtx-prepare
    SOURCES
        environment/atmospheremesh.cpp
        environment/cloudmesh.cpp
        environment/moonfaces.cpp
        environment/skybuilder.cpp
        environment/skymesh.cpp
        environment/skysheet.cpp
        environment/starmesh.cpp
        environment/wavecascade.cpp
        environment/wavespectrum.cpp
    HEADERS
        environment/atmospheremesh.hpp
        environment/cloudmesh.hpp
        environment/moonfaces.hpp
        environment/skybuilder.hpp
        environment/skymesh.hpp
        environment/skysheet.hpp
        environment/starmesh.hpp
        environment/wavecascade.hpp
        environment/wavespectrum.hpp
)

openmw_rtx_layer(
    view
    openmw-rtx-environment
    SOURCES
        view/offscreentrace.cpp
        view/viewscene.cpp
    HEADERS
        view/offscreentrace.hpp
        view/viewscene.hpp
)

# The whole core, which the backend and the game link.
add_library(openmw-rtx INTERFACE)
target_link_libraries(openmw-rtx INTERFACE openmw-rtx-view)

# What the ray tracer would otherwise compute at every start from no content, made by the build and
# compiled in: each table's maker, a verb of `openmw-rtx-build` that writes the table as a source, and the
# source, compiled into the layer whose header names it. The makers are a library of their own, which the
# build tool and the test holding each table to its maker link, so the game carries no maker.
add_library(
    openmw-rtx-tablemakers
    STATIC
    tables/tablesource.cpp
    tables/tablesource.hpp
    tables/bluenoisemaker.cpp
    tables/bluenoisemaker.hpp
    tables/specularalbedomaker.cpp
    tables/specularalbedomaker.hpp
    tables/fognoisemaker.cpp
    tables/fognoisemaker.hpp
)
# OpenSceneGraph's headers alone, for the vector types `shaders/hosttypes.h` and the makers name: the makers
# run in the build, where no OSG library need be found.
target_link_libraries(
    openmw-rtx-tablemakers
    PUBLIC
        openmw-platform-io
        OpenSceneGraph::headers
)

function(openmw_rtx_table verb table layer)
    set(made "${CMAKE_CURRENT_BINARY_DIR}/${table}")
    add_custom_command(
        OUTPUT
            "${made}"
        COMMAND
            openmw-rtx-build ${verb} "${made}"
        DEPENDS
            openmw-rtx-build
        COMMENT "Making ${table}"
        VERBATIM
    )
    target_sources(${layer} PRIVATE "${made}")
endfunction()

openmw_rtx_table(blue-noise rtxbluenoise.cpp openmw-rtx-frame)
openmw_rtx_table(fog-noise rtxfognoise.cpp openmw-rtx-world)
openmw_rtx_table(specular-albedo rtxspecularalbedo.cpp openmw-rtx-frame)
