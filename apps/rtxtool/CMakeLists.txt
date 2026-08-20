set(RTXTOOL_FILES
    main.cpp
    shot.cpp
    shot.hpp
    world.cpp
    world.hpp
)

source_group(apps\\rtxtool FILES ${RTXTOOL_FILES})

openmw_add_executable(openmw-rtxtool ${RTXTOOL_FILES})

target_link_libraries(openmw-rtxtool
    Boost::program_options
    openmw-rtx
    openmw-rtx-bridge
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool gcov)
endif()
