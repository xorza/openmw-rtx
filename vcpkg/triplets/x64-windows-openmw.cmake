# The tree's libraries on Windows: static, the C runtime static too (`/MT`), release only (every flavour
# links the release runtime), with frame pointers as the tree's own code has them.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "/Oy-")
set(VCPKG_CXX_FLAGS "/Oy-")
# OpenSceneGraph's GL2 profile, which the rasterizer is written against.
set(osg_OPENGL_PROFILE GL2)
