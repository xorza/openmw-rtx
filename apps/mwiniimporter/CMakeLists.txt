set(MWINIIMPORT
    main.cpp
    importer.cpp
)

set(MWINIIMPORT_HEADER
    importer.hpp
)

source_group(launcher FILES ${MWINIIMPORT} ${MWINIIMPORT_HEADER})

openmw_add_executable(openmw-iniimporter
    ${MWINIIMPORT}
)

target_link_libraries(openmw-iniimporter
    Boost::program_options
    components
)

if (WIN32)
    target_sources(openmw-iniimporter PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

install(TARGETS openmw-iniimporter ${runtime_dependency_arguments})

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-iniimporter PRIVATE --coverage)
    target_link_libraries(openmw-iniimporter gcov)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-iniimporter REUSE_FROM components)
endif()
