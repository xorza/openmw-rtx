# The tree's libraries on Linux: static, release only (every flavour links the release runtime), compiled as
# the tree's own code is, by the same Clang and lld (`vcpkg/flags.cmake`, `vcpkg/linux.cmake`).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)
include("${CMAKE_CURRENT_LIST_DIR}/../flags.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../linux.cmake")
set(VCPKG_HASH_ADDITIONAL_FILES
    "${CMAKE_CURRENT_LIST_DIR}/../flags.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/../linux.cmake"
)
set(VCPKG_C_FLAGS "${openmw_toolchain_linux} ${openmw_codegen_gnu} ${openmw_codegen_linux}")
set(VCPKG_CXX_FLAGS "${openmw_toolchain_linux} ${openmw_codegen_gnu} ${openmw_codegen_linux}")
set(VCPKG_LINKER_FLAGS "${openmw_toolchain_linux} -fuse-ld=lld")
# OpenSceneGraph's GL2 profile, which the rasterizer is written against.
set(osg_OPENGL_PROFILE GL2)
