add_executable(openmw-tests)
target_sources(openmw-tests
    PRIVATE
        main.cpp
        mwclass/preloadmodels.cpp
        mwdialogue/testkeywordsearch.cpp
        mwgui/bookpage.cpp
        mwgui/fontsize.cpp
        mwgui/tooltips.cpp
        mwgui/weightedsearch.cpp
        mwlua/surfacelight.cpp
        mwlua/testmwscriptbindings.cpp
        mwlua/testobjectcache.cpp
        mwrender/classmasks.cpp
        mwrender/debugwalk.cpp
        mwrender/frametimer.cpp
        mwrender/groundcovershapes.cpp
        mwrender/localmap.cpp
        mwrender/pendingpaints.cpp
        mwrender/pixels.cpp
        mwrender/precipitation.cpp
        mwrender/readworld.cpp
        mwrender/renderer.cpp
        mwrender/ripples.cpp
        mwrender/rtxrenderer.cpp
        mwrender/rtxsettings.cpp
        mwrender/rtxsupport.cpp
        mwrender/rtxwindow.cpp
        mwrender/searules.cpp
        mwrender/skystate.cpp
        mwrender/tracedgroundcover.cpp
        mwrender/tracedterrain.cpp
        mwrender/viewschedule.cpp
        mwrender/waterstate.cpp
        mwrender/worldmirror.cpp
        mwscript/testscripts.cpp
        mwscript/visibilitygates.cpp
        mwsound/testheadcache.cpp
        mwsound/testwarmqueue.cpp
        mwworld/testdatetimemanager.cpp
        mwworld/testduration.cpp
        mwworld/testlocalscripts.cpp
        mwworld/testlowestpoints.cpp
        mwworld/testpositioncellgrid.cpp
        mwworld/testptr.cpp
        mwworld/teststore.cpp
        mwworld/testtimestamp.cpp
        mwworld/testweather.cpp
        options.cpp
        "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/allocations.cpp"
        "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/fallbackseed.cpp"
        "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/firsttexel.cpp"
        "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/guiinstance.cpp"
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        mwscript/testutils.hpp
)
if (WIN32)
    target_sources(openmw-tests PRIVATE
        "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/testplatformwin32.cpp"
        "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
else ()
    target_sources(openmw-tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/components_tests/rtx/support/testplatformposix.cpp")
endif ()
# MSVC's four code generation threads a file take more than CI's 8 GB beside another `cl.exe` on this one.
set_property(SOURCE mwworld/teststore.cpp APPEND PROPERTY COMPILE_OPTIONS $<$<CXX_COMPILER_ID:MSVC>:/cgthreads1>)

target_link_libraries(openmw-tests PRIVATE GTest::gtest GTest::gmock openmw-lib)
target_compile_definitions(openmw-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

if (MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-tests PRIVATE
        <boost/program_options/options_description.hpp>

        <gtest/gtest.h>

        <sol/sol.hpp>

        <algorithm>
        <filesystem>
        <fstream>
        <functional>
        <memory>
        <string>
        <vector>
    )
endif ()
