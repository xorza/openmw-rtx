if (OPENMW_RTX)
    set(RTX_TEST_FILES
        ${OpenMW_SOURCE_DIR}/apps/components_tests/rtx/support/allocations.cpp
        ${OpenMW_SOURCE_DIR}/apps/components_tests/rtx/support/fallbackseed.cpp
        mwrender/debugwalk.cpp
        mwrender/frametimer.cpp
        mwrender/precipitation.cpp
        mwrender/readworld.cpp
        mwrender/rtxrenderer.cpp
        mwrender/rtxsettings.cpp
        mwrender/tracedterrain.cpp
        mwrender/waterstate.cpp
        mwrender/worldmirror.cpp
    )
    list(APPEND UNITTEST_SRC_FILES ${RTX_TEST_FILES})

    # The seam's tests stand in upstream's list in `CMakeLists.txt`, because a build without the ray
    # tracer has the seam too, and take the fork's flags here.
    openmw_rtx_sources(${RTX_TEST_FILES} mwrender/renderer.cpp)
endif()
