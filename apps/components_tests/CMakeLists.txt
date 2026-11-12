# Upstream's component tests and the ray tracer's host tests in one binary; what opens a device in
# `rtx-gpu-tests` (`rtx/tests.cmake`), and the crash catcher's own process in `crash-tests`
# (`crashcatcher/tests.cmake`).

add_executable(components-tests)
target_sources(
    components-tests
    PRIVATE
        bsa/testbsafile.cpp
        bsa/testcompressedbsafile.cpp
        contentstore/key.cpp
        contentstore/store.cpp
        crashcatcher/crashhangwatch.cpp
        crashcatcher/crashmonitor.cpp
        crashcatcher/crashnote.cpp
        crashcatcher/crashpackage.cpp
        crashcatcher/crashsummary.cpp
        detournavigator/asyncnavmeshupdater.cpp
        detournavigator/gettilespositions.cpp
        detournavigator/navigator.cpp
        detournavigator/navmeshtilescache.cpp
        detournavigator/navmeshtilestore.cpp
        detournavigator/recastmeshbuilder.cpp
        detournavigator/recastmeshobject.cpp
        detournavigator/settingsutils.cpp
        detournavigator/tilecachedrecastmeshmanager.cpp
        esm/testfixedstring.cpp
        esm/testgameformat.cpp
        esm/testrefid.cpp
        esm/variant.cpp
        esm3/readerscache.cpp
        esm3/testcstringids.cpp
        esm3/testesmwriter.cpp
        esm3/testinfoorder.cpp
        esm3/testsaveload.cpp
        esm4/includes.cpp
        esmloader/esmdata.cpp
        esmloader/load.cpp
        esmloader/record.cpp
        esmterrain/testgridsampling.cpp
        files/conversiontests.cpp
        files/hash.cpp
        fx/lexer.cpp
        fx/technique.cpp
        lua/testasync.cpp
        lua/testconfiguration.cpp
        lua/testinputactions.cpp
        lua/testl10n.cpp
        lua/testlua.cpp
        lua/testscriptscontainer.cpp
        lua/testserialization.cpp
        lua/teststorage.cpp
        lua/testuicontent.cpp
        lua/testutilpackage.cpp
        lua/testyaml.cpp
        main.cpp
        misc/compression.cpp
        misc/frameclock.cpp
        misc/hashstate.cpp
        misc/monitor.cpp
        misc/presentation.cpp
        misc/progressreporter.cpp
        misc/testendianness.cpp
        misc/testmathutil.cpp
        misc/testresourcehelpers.cpp
        misc/teststringops.cpp
        nif/testphysics.cpp
        nifloader/testbulletnifloader.cpp
        nifosg/testcontroller.cpp
        nifosg/testnifloader.cpp
        platform/appendfile.cpp
        platform/folder.cpp
        platform/localtime.cpp
        platform/sharedmemory.cpp
        platform/thread.cpp
        platform/uniquehold.cpp
        platform/userhome.cpp
        platform/wholefile.cpp
        resource/testcolladastream.cpp
        resource/testmultiobjectcache.cpp
        resource/testobjectcache.cpp
        resource/testresourcesystem.cpp
        rtx/support/allocations.cpp
        sceneutil/osgacontroller.cpp
        sceneutil/paintedtexture.cpp
        sceneutil/userdata.cpp
        sdlutil/sdlcursormanager.cpp
        sdlutil/sdlinputwrapper.cpp
        sdlutil/sdlvideowrapper.cpp
        serialization/binaryreader.cpp
        serialization/binarywriter.cpp
        serialization/integration.cpp
        serialization/sizeaccumulator.cpp
        settings/migration.cpp
        settings/parser.cpp
        settings/shadermanager.cpp
        settings/testvalues.cpp
        shader/automaps.cpp
        shader/parsedefines.cpp
        shader/parsefors.cpp
        shader/parselinks.cpp
        shader/shadermanager.cpp
        terrain/refstack.cpp
        toutf8/toutf8.cpp
        vfs/testpathutil.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                nif/node.hpp
                rtx/support/allocations.hpp
)
# The system's half of the test support, one file a system (`rtx/support/testplatform.hpp`), for every test
# binary that reads it.
add_library(openmw-test-platform OBJECT)
if(WIN32)
    target_sources(openmw-test-platform PRIVATE rtx/support/testplatformwin32.cpp)
else()
    target_sources(openmw-test-platform PRIVATE rtx/support/testplatformposix.cpp)
endif()
target_link_libraries(
    components-tests
    PRIVATE
        openmw-test-platform
        openmw-test-options
)
target_sources(components-tests PRIVATE config/testlaunchersettings.cpp)
target_link_libraries(components-tests PRIVATE openmw-components-qt)
target_sources(
    components-tests
    PRIVATE
        graphite/colour.cpp
        graphite/look.cpp
        graphite/offscreen.cpp
        graphite/palette.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                graphite/offscreen.hpp
)
target_link_libraries(components-tests PRIVATE openmw-graphite)
# The platform the look's tests draw on, in memory: the static Qt links the platforms it is told of.
qt_import_plugins(components-tests INCLUDE Qt6::QOffscreenIntegrationPlugin)

target_link_libraries(
    components-tests
    PRIVATE
        GTest::gtest
        GTest::gmock
        openmw-components
        openmw-myguirtx
        openmw-rtx-tablemakers
)

set(OPENMW_TEMPLATE_OMWGAME_URL
    https://gitlab.com/OpenMW/example-suite/-/raw/8966dab24692555eec720c854fb0f73d108070cd/data/template.omwgame
    CACHE STRING
    "URL for template.omwgame. Use file:// URL for local file in offline builds."
)
file(
    DOWNLOAD ${OPENMW_TEMPLATE_OMWGAME_URL}
    ${CMAKE_CURRENT_BINARY_DIR}/data/template.omwgame
    EXPECTED_HASH
        SHA512=6e38642bcf013c5f496a9cb0bf3ec7c9553b6e86b836e7844824c5a05f556c9391167214469b6318401684b702d7569896bf743c85aee4198612b3315ba778d6
)

target_compile_definitions(
    components-tests
    PRIVATE
        OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
        OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}"
)

include(rtx/tests.cmake)
include(crashcatcher/tests.cmake)
