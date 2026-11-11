# Every way a game ends, each in a process of its own with the real catcher: not a gtest
# binary, because each mode ends the process that runs it. `cmake/Tests.cmake` runs its matrix.
# With the half that knows the system it runs on, so the shared half never asks.
add_executable(crash-tests)
target_sources(
    crash-tests
    PRIVATE
        crashcatcher/crashtests.cpp
    PRIVATE
        FILE_SET HEADERS
            BASE_DIRS
                "${PROJECT_SOURCE_DIR}"
            FILES
                crashcatcher/crashtestssystem.hpp
)
if(WIN32)
    target_sources(crash-tests PRIVATE crashcatcher/crashtestswin32.cpp)
else()
    target_sources(crash-tests PRIVATE crashcatcher/crashtestsposix.cpp)
endif()
target_link_libraries(crash-tests PRIVATE openmw-components)

# The AppImage keeper's reads, where the components build one.
if(LINUX)
    target_sources(components-tests PRIVATE crashcatcher/crashimagelinux.cpp)
endif()
