add_executable(openmw-cs-tests)
target_sources(openmw-cs-tests
    PRIVATE
        main.cpp
        model/world/testinfocollection.cpp
        model/world/testuniversalid.cpp
)
if (WIN32)
    target_sources(openmw-cs-tests PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw-cs-tests PRIVATE openmw-cs-lib GTest::gtest GTest::gmock)

if (MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-cs-tests REUSE_FROM openmw-components)
endif ()
