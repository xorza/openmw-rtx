set(RTXTOOL_FILES
    main.cpp
    placement.cpp
    placement.hpp
    png.cpp
    png.hpp
    shot.cpp
    shot.hpp
    view.cpp
    view.hpp
    views.cpp
    views.hpp
    window.cpp
    window.hpp
    world.cpp
    world.hpp
)

source_group(apps\\rtxtool FILES ${RTXTOOL_FILES})

openmw_add_executable(openmw-rtxtool ${RTXTOOL_FILES})

target_link_libraries(openmw-rtxtool
    Boost::program_options
    SDL2::SDL2
    openmw-rtx
    openmw-rtx-bridge
    components
)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool gcov)
endif()
