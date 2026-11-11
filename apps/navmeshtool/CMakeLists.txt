add_executable(openmw-navmeshtool)
target_sources(
    openmw-navmeshtool
    PRIVATE
        main.cpp
        navmesh.cpp
        worldspacedata.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                navmesh.hpp
                worldspacedata.hpp
)
target_link_libraries(
    openmw-navmeshtool
    PRIVATE
        Boost::program_options
        openmw-components
)

install(
    TARGETS
        openmw-navmeshtool
        ${runtime_dependency_arguments}
)
