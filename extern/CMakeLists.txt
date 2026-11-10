# SPDX-License-Identifier: GPL-3.0-or-later

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL on)
set(CMAKE_CXX_CLANG_TIDY "")

# sol3, the C++ binding to Lua, headers only, with the configuration the tree compiles it with: its
# `sol/config.hpp` ahead of sol's own, which the folder after it holds.
add_library(sol3 INTERFACE)
target_include_directories(sol3 SYSTEM INTERFACE sol_config sol3)
target_link_libraries(sol3 INTERFACE Lua::Lua)

add_subdirectory(smhasher)
add_subdirectory(rgbcx)

add_subdirectory(osg-ffmpeg-videoplayer)
add_subdirectory(oics)
add_subdirectory(Base64)
if (BUILD_OPENCS OR BUILD_OPENCS_TESTS)
    add_subdirectory(osgQt)
endif ()
