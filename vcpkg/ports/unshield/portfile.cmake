vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO twogood/unshield
  REF 1.4.2
  SHA512 3ddd5df47198adade66acf89644efe3a1c87d60fcdf511f8e5c1fb29bbb07e77a9abc57a05b161ecb54a26a2a86242bbcba6e90df770b1325965ff5aa8eddf58
)
set(UNSHIELD_CMAKE_ARGS "-DUSE_OUR_OWN_MD5=ON")
# unshield chooses its library's type by an option of its own, which `BUILD_SHARED_LIBS` does not reach;
# and its MD5 and UTF conversion are static libraries of their own, which a static libunshield links and no
# install carries: as object libraries, their objects are libunshield's.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    list(APPEND UNSHIELD_CMAKE_ARGS "-DBUILD_STATIC=ON")
    vcpkg_replace_string("${SOURCE_PATH}/lib/md5/CMakeLists.txt" "add_library(md5 STATIC" "add_library(md5 OBJECT")
    vcpkg_replace_string("${SOURCE_PATH}/lib/convert_utf/CMakeLists.txt"
        "add_library(convert_utf STATIC" "add_library(convert_utf OBJECT")
endif()
# The bundled MD5 declares its functions in K&R style, which C23, GCC 15's default, reads as taking no
# arguments.
if(NOT VCPKG_TARGET_IS_WINDOWS)
    string(APPEND VCPKG_C_FLAGS " -std=gnu17")
endif()

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS ${UNSHIELD_CMAKE_ARGS}
)
vcpkg_cmake_install()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)
vcpkg_fixup_pkgconfig()
