add_executable(openmw-iniimporter)
target_sources(
    openmw-iniimporter
    PRIVATE
        importer.cpp
        main.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                importer.hpp
)
target_link_libraries(
    openmw-iniimporter
    PRIVATE
        Boost::program_options
        openmw-components
)

install(TARGETS openmw-iniimporter)
