add_executable(niftest)
target_sources(niftest PRIVATE niftest.cpp)
if(WIN32)
    target_sources(niftest PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif()
target_link_libraries(niftest PRIVATE openmw-components)

install(
    TARGETS
        niftest
        ${runtime_dependency_arguments}
)

if(MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(niftest REUSE_FROM openmw-components)
endif()
