add_executable(openmw-bulletobjecttool)
target_sources(openmw-bulletobjecttool
    PRIVATE
        main.cpp
)
if (WIN32)
    target_sources(openmw-bulletobjecttool PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw-bulletobjecttool PRIVATE Boost::program_options openmw-components)

install(TARGETS openmw-bulletobjecttool ${runtime_dependency_arguments})

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-bulletobjecttool REUSE_FROM openmw-components)
endif ()
