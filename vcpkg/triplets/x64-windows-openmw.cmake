# The tree's libraries on Windows: static, the C runtime static too (`/MT`), release only (every flavour
# links the release runtime), compiled as the tree's own code is (`vcpkg/flags.cmake`).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
include("${CMAKE_CURRENT_LIST_DIR}/../flags.cmake")
set(VCPKG_HASH_ADDITIONAL_FILES "${CMAKE_CURRENT_LIST_DIR}/../flags.cmake")
set(VCPKG_C_FLAGS "${openmw_codegen_msvc}")
set(VCPKG_CXX_FLAGS "${openmw_codegen_msvc}")
# OpenSceneGraph's GL2 profile, which the rasterizer is written against.
set(osg_OPENGL_PROFILE GL2)
