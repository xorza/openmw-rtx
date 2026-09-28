# The renderer seam: `MWRender::Renderer`, what a frame is described as, and the rasterizer behind
# the seam. Built whichever renderer is, and named once so the block below can find them.
set(OPENMW_RENDERER_SEAM
    renderer sceneframe framedescriber skystate ripplerules objectstorage ground glground glrenderer glworld gloffscreenview glmapoverlay mapoverlay pixels
    )
add_openmw_dir (mwrender ${OPENMW_RENDERER_SEAM})

# Gated, because these are the only files in the game that name a graphics API other than OpenGL.
if (OPENMW_RTX)
    set(OPENMW_RTX_DIR
        classmasks debugwalk framereport frametimer rippleemitters rtxrenderer rtxrun rtxsettings rtxwindow skyreader tracedground
        tracedoverlay tracedterrain tracedview viewqueue worldmirror
        )
    add_openmw_dir (mwrender/rtx ${OPENMW_RTX_DIR})

    # The seam and the ray tracer's own directory are this fork's files in upstream's target, and
    # every one of them reads the game's headers: the errors alone, for the reason
    # `components/rtx/build.cmake` gives. A name with a header alone names a file nothing compiles,
    # and a property on it is read by nothing.
    set(OPENMW_RTX_SOURCES)
    set(OPENMW_RTX_NAMED)
    foreach (name ${OPENMW_RTX_DIR})
        list(APPEND OPENMW_RTX_SOURCES mwrender/rtx/${name}.cpp)
        list(APPEND OPENMW_RTX_NAMED ${name}.cpp ${name}.hpp)
    endforeach()
    openmw_rtx_expect_listed("${CMAKE_CURRENT_SOURCE_DIR}/mwrender/rtx" MATCHING *.cpp *.hpp LISTED ${OPENMW_RTX_NAMED})
    foreach (name ${OPENMW_RENDERER_SEAM})
        list(APPEND OPENMW_RTX_SOURCES mwrender/${name}.cpp)
    endforeach()
    openmw_rtx_sources(ERRORS_ONLY ${OPENMW_RTX_SOURCES})
else()
    # `createRtxRenderer` for a build without the ray tracer, so the factory names no build flag.
    add_openmw_dir (mwrender nortxrenderer)
endif()
