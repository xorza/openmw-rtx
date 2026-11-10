add_executable(bsatool)
target_sources(bsatool PRIVATE bsatool.cpp)
if(WIN32)
    target_sources(bsatool PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif()
target_link_libraries(
    bsatool
    PRIVATE
        Boost::program_options
        openmw-components
)

install(
    TARGETS
        bsatool
        ${runtime_dependency_arguments}
)

if(MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(bsatool REUSE_FROM openmw-components)
endif()
