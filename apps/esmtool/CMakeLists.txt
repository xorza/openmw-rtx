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
if(WIN32)
    target_sources(esmtool PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif()
target_link_libraries(
    esmtool
    PRIVATE
        Boost::program_options
        openmw-components
)

install(
    TARGETS
        esmtool
        ${runtime_dependency_arguments}
)

if(MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(
        esmtool
        PRIVATE
            <fstream>
            <string>
            <vector>
    )
endif()
