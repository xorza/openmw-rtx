# The tree's libraries on Linux: static, release only (every flavour links the release runtime), with line
# tables and frame pointers as the tree's own code has them, so a profile and a crash read through them.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "-g1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")
set(VCPKG_CXX_FLAGS "-g1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")
# OpenSceneGraph's GL2 profile, which the rasterizer is written against.
set(osg_OPENGL_PROFILE GL2)
