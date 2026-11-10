add_executable(openmw-navmeshtool)
target_sources(openmw-navmeshtool
    PRIVATE
        main.cpp
        navmesh.cpp
        worldspacedata.cpp
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        navmesh.hpp
        worldspacedata.hpp
)
if (WIN32)
    target_sources(openmw-navmeshtool PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw-navmeshtool PRIVATE Boost::program_options openmw-components)

install(TARGETS openmw-navmeshtool ${runtime_dependency_arguments})

if (MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-navmeshtool REUSE_FROM openmw-components)
endif ()
