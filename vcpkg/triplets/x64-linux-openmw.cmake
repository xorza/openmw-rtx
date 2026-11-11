# The tree's libraries on Linux: static, release only (every flavour links the release runtime), compiled as
# the tree's own code is (`vcpkg/flags.cmake`).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
include("${CMAKE_CURRENT_LIST_DIR}/../flags.cmake")
set(VCPKG_HASH_ADDITIONAL_FILES "${CMAKE_CURRENT_LIST_DIR}/../flags.cmake")
set(VCPKG_C_FLAGS "${openmw_codegen_gnu}")
set(VCPKG_CXX_FLAGS "${openmw_codegen_gnu}")
# OpenSceneGraph's GL2 profile, which the rasterizer is written against.
set(osg_OPENGL_PROFILE GL2)
