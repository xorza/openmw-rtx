add_executable(openmw-iniimporter)
target_sources(openmw-iniimporter
    PRIVATE
        importer.cpp
        main.cpp
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        importer.hpp
)
if (WIN32)
    target_sources(openmw-iniimporter PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw-iniimporter PRIVATE Boost::program_options openmw-components)

install(TARGETS openmw-iniimporter ${runtime_dependency_arguments})

if (MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-iniimporter REUSE_FROM openmw-components)
endif ()
