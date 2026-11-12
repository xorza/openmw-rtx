# SPDX-License-Identifier: GPL-3.0-or-later

set_directory_properties(
    PROPERTIES
        EXCLUDE_FROM_ALL
            on
)
# Vendored code, whose warnings are its own (`docs/build.md`): off, whatever the tree's preset asks. No project
# here sets a warning level of its own, so MSVC's `/w` overrides none.
if(MSVC)
    string(APPEND CMAKE_C_FLAGS " /w")
    string(APPEND CMAKE_CXX_FLAGS " /w")
else()
    string(APPEND CMAKE_C_FLAGS " -w")
    string(APPEND CMAKE_CXX_FLAGS " -w")
endif()

# sol3, the C++ binding to Lua, headers only, with the configuration the tree compiles it with: its
# `sol/config.hpp` ahead of sol's own, which the folder after it holds.
add_library(sol3 INTERFACE)
target_include_directories(
    sol3
    SYSTEM
    INTERFACE
        sol_config
        sol3
)
target_link_libraries(sol3 INTERFACE Lua::Lua)

add_subdirectory(smhasher)
add_subdirectory(rgbcx)

add_subdirectory(osg-ffmpeg-videoplayer)
add_subdirectory(oics)
add_subdirectory(Base64)
add_subdirectory(osgQt)
