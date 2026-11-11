add_executable(esmtool)
target_sources(
    esmtool
    PRIVATE
        esmtool.cpp
        labels.cpp
        record.cpp
        tes4.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                arguments.hpp
                labels.hpp
                record.hpp
                tes4.hpp
)
target_link_libraries(
    esmtool
    PRIVATE
        Boost::program_options
        openmw-components
)

install(TARGETS esmtool)
