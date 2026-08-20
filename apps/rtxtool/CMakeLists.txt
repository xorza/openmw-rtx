set(RTXTOOL_FILES
    placement.cpp
    placement.hpp
    png.cpp
    png.hpp
    shot.cpp
    shot.hpp
    terrainstorage.cpp
    terrainstorage.hpp
    view.cpp
    view.hpp
    views.cpp
    views.hpp
    window.cpp
    window.hpp
    world.cpp
    world.hpp
)

source_group(apps\\rtxtool FILES ${RTXTOOL_FILES} main.cpp)

# A library so the tests can reach into it, the way apps/openmw builds openmw-lib.
add_library(openmw-rtxtool-lib STATIC ${RTXTOOL_FILES})

target_link_libraries(openmw-rtxtool-lib
    Boost::program_options
    SDL2::SDL2
    openmw-rtx
    openmw-rtx-bridge
    components
)

# The same call `components/rtx` makes, for the same reason: Vulkan's create-info structs are filled
# with designated initializers, which value-initialise every field not named, and GCC's warning does
# not distinguish that from an accidentally short aggregate.
if (CMAKE_CXX_COMPILER_ID STREQUAL GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
    target_compile_options(openmw-rtxtool-lib PRIVATE -Wno-missing-field-initializers)
endif()

openmw_add_executable(openmw-rtxtool main.cpp)

target_link_libraries(openmw-rtxtool openmw-rtxtool-lib)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtxtool-lib PRIVATE --coverage)
    target_link_libraries(openmw-rtxtool-lib gcov)
endif()
