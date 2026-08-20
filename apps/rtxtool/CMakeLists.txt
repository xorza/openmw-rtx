source_group(apps\\rtxtool FILES main.cpp)

openmw_add_executable(openmw-rtxtool main.cpp)

target_link_libraries(openmw-rtxtool
    Boost::program_options
    openmw-rtx
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool gcov)
endif()
