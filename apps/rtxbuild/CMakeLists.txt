# What the build runs to make the ray tracer's sources: the shaders' guard and pinning, the source that compiles
# the game's shaders in, the tables a start would otherwise compute, and the digest `omw kernels` names kernels by.
# One program with a verb a step (`run.cpp`), which no install carries. Every shader step depends on it, so any
# edit to it compiles every shader again. The verbs are a library the program and `components-tests` link.
add_library(openmw-rtx-buildverbs STATIC)
target_sources(
    openmw-rtx-buildverbs
    PRIVATE
        run.cpp
        spirvverbs.cpp
        tableverbs.cpp
        verbs.hpp
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                run.hpp
)
target_link_libraries(
    openmw-rtx-buildverbs
    PRIVATE
        openmw-rtx-spirv
        openmw-rtx-tablemakers
        openmw-platform-io
        smhasher
)

add_executable(openmw-rtx-build main.cpp)
target_link_libraries(openmw-rtx-build PRIVATE openmw-rtx-buildverbs)
# Beside its sources in the build tree, apart from the programs a run starts.
set_target_properties(
    openmw-rtx-build
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}"
)
