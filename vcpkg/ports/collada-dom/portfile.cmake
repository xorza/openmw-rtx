vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO rdiankov/collada-dom
    REF d37ae7532e350b87c88712e9f6ab4b1f440d20cd
    SHA512 cb923d296219765096f5246cc7a2b69712931f58171ae885dbdbd215fca86d911c34d12748d3304d6a5a350dc737ff0caead2495acac488af5431b437cbacc7d
    HEAD_REF v2.5.0
    PATCHES
        vs-version-detection.patch
        use-uriparser.patch
        use-vcpkg-minizip.patch
        fix-shared-keyword.patch
        fix-emscripten.patch
        fix-compatibility-with-boost-1.85.patch
)
file(REMOVE_RECURSE "${SOURCE_PATH}/dom/external-libs")

# The DOMs of COLLADA 1.4 and 1.5 are static libraries of their own, which a static collada-dom links and no
# install carries, so whatever calls into them finds no such symbols: as object libraries, their objects
# are collada-dom's.
if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${SOURCE_PATH}/dom/src/1.4/CMakeLists.txt"
        "add_library(colladadom141 STATIC" "add_library(colladadom141 OBJECT")
    vcpkg_replace_string("${SOURCE_PATH}/dom/src/1.5/CMakeLists.txt"
        "add_library(colladadom150 STATIC" "add_library(colladadom150 OBJECT")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
      -DCMAKE_CXX_STANDARD=11
      -DVCPKG_LOCK_FIND_PACKAGE_Boost=ON
      -DVCPKG_LOCK_FIND_PACKAGE_LibXml2=ON
      -DVCPKG_LOCK_FIND_PACKAGE_PkgConfig=OFF
      -DVCPKG_LOCK_FIND_PACKAGE_ZLIB=ON
)
vcpkg_cmake_install()
vcpkg_fixup_pkgconfig()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/collada_dom-2.5)
# The config looks for Boost on Windows alone, at exactly the version collada-dom was built with and as
# dynamic libraries, which the set's static Boost, spelt 1.92.0 against the config's "1.92", never meets: a
# warning at every Windows configure, for include folders whoever links collada-dom takes from Boost's own
# targets.
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/collada_dom-config.cmake"
    "if\\( WIN32 \\)\n  # search for the boost version collada-dom was compiled with\n.*endif\\( WIN32 \\)\n"
    ""
    REGEX
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/licenses/license_e.txt")

