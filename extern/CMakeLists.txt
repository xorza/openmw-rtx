# SPDX-License-Identifier: GPL-3.0-or-later

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL on)
set(CMAKE_CXX_CLANG_TIDY "")

add_subdirectory(smhasher)
add_subdirectory(rgbcx)

add_subdirectory(osg-ffmpeg-videoplayer)
add_subdirectory(oics)
add_subdirectory(Base64)
if (BUILD_OPENCS OR BUILD_OPENCS_TESTS)
    add_subdirectory(osgQt)
endif ()
