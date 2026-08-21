# The Metal backend. Built only with -DOPENMW_RTX=ON and -DOPENMW_RTX_METAL=ON, and only on Apple.
#
# Everything Metal is Objective-C++ and stays inside the implementation: the one header here is
# plain C++ so the dispatcher can include it without the rest of the tree learning what an `id<>` is.
# See docs/rtx/backends.md §7.

enable_language(OBJCXX)

find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

set(RTXMETAL_FILES
    metalrenderer.hpp
    metalrenderer.mm
)

source_group(components\\rtxmetal FILES ${RTXMETAL_FILES})

add_library(openmw-rtx-metal STATIC ${RTXMETAL_FILES})

target_link_libraries(openmw-rtx-metal
    PUBLIC openmw-rtx
    PRIVATE components ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})

# ARC, so the Objective-C objects a renderer holds are released with it and nothing here writes a
# retain or a release by hand.
target_compile_options(openmw-rtx-metal PRIVATE ${OPENMW_RTX_COMPILE_OPTIONS} -fobjc-arc)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-rtx-metal PRIVATE --coverage)
    target_link_libraries(openmw-rtx-metal gcov)
endif()
