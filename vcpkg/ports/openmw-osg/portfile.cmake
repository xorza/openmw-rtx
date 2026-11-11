set(VCPKG_POLICY_DLLS_WITHOUT_EXPORTS enabled)

set(OSG_VER 3.6.5)

set(PATCHES "")

if(VCPKG_TARGET_IS_OSX)
    list(APPEND PATCHES "macos-dedicated-image-plugin.patch")
endif()

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO OpenMW/osg
    REF e568aace1e62de021df9955c62429cbe55516fd3
    SHA512 3bac08eec41549e81dca176bf28c796dad488c25ea4c9a005f211db884c78468f3efd1823d716e4b54c4555cc32ff1d621317659edfdaa21c3cb5c4f5e8cca15
    HEAD_REF 3.6
    PATCHES ${PATCHES}
)

file(
    REMOVE
    "${SOURCE_PATH}/CMakeModules/FindFontconfig.cmake"
    "${SOURCE_PATH}/CMakeModules/FindFreetype.cmake"
    "${SOURCE_PATH}/CMakeModules/Findilmbase.cmake"
    "${SOURCE_PATH}/CMakeModules/FindOpenEXR.cmake"
    "${SOURCE_PATH}/CMakeModules/FindSDL2.cmake"
)

string(
    COMPARE EQUAL
    "${VCPKG_LIBRARY_LINKAGE}"
    "dynamic"
    OSG_DYNAMIC
)

set(OPTIONS "")

# Skip try_run checks
if(VCPKG_TARGET_IS_MINGW)
    list(
        APPEND OPTIONS
        -D_OPENTHREADS_ATOMIC_USE_WIN32_INTERLOCKED=0
        -D_OPENTHREADS_ATOMIC_USE_GCC_BUILTINS=1
    )
elseif(VCPKG_TARGET_IS_WINDOWS)
    list(
        APPEND OPTIONS
        -D_OPENTHREADS_ATOMIC_USE_WIN32_INTERLOCKED=1
        -D_OPENTHREADS_ATOMIC_USE_GCC_BUILTINS=0
        -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON
    )
elseif(VCPKG_TARGET_IS_OSX)
    list(APPEND OPTIONS -DOSG_WINDOWING_SYSTEM=Cocoa)
elseif(VCPKG_CROSSCOMPILING)
    message(WARNING "Atomics detection may fail for cross builds. You can set osg cmake variables in a custom triplet.")
endif()

# OpenThreads yields by `sched_yield` and not `pthread_yield`, which glibc deprecated in 2.34 and keeps for
# programs linked before alone: the set is built on glibc 2.28, which offers it, and a program linked on a
# newer glibc finds no such symbol.
if(VCPKG_TARGET_IS_LINUX)
    list(APPEND OPTIONS -DHAVE_PTHREAD_YIELD=0)
endif()

# The package osg can be configured to use different OpenGL profiles via a custom triplet file:
# Possible values are GLCORE, GL2, GL3, GLES1, GLES2, GLES3, and GLES2+GLES3
if(NOT DEFINED osg_OPENGL_PROFILE)
    set(osg_OPENGL_PROFILE "GL2")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DDYNAMIC_OPENSCENEGRAPH=${OSG_DYNAMIC}
        -DDYNAMIC_OPENTHREADS=${OSG_DYNAMIC}
        -DOSG_MSVC_VERSIONED_DLL=OFF
        -DOSG_DETERMINE_WIN_VERSION=OFF
        -DOSG_FIND_3RD_PARTY_DEPS=OFF
        -DOSG_DISABLE_MSVC_WARNINGS=OFF
        -DOPENGL_PROFILE=${osg_OPENGL_PROFILE}
        -DBUILD_DASHBOARD_REPORTS=OFF
        -DCMAKE_CXX_STANDARD=11
        -DOSG_TEXT_USE_FONTCONFIG=OFF
        -DOSGVIEWER_USE_XRANDR=OFF
        -DBUILD_OSG_PLUGINS_BY_DEFAULT=OFF
        -DBUILD_OSG_PLUGIN_OSG=ON
        -DBUILD_OSG_PLUGIN_DDS=ON
        -DBUILD_OSG_PLUGIN_TGA=ON
        -DBUILD_OSG_PLUGIN_BMP=ON
        -DBUILD_OSG_PLUGIN_JPEG=ON
        -DBUILD_OSG_PLUGIN_PNG=ON
        -DBUILD_OSG_PLUGIN_FREETYPE=ON
        -DBUILD_OSG_PLUGIN_DAE=ON
        -DBUILD_OSG_PLUGIN_KTX=ON
        -DBUILD_OSG_DEPRECATED_SERIALIZERS=OFF
        -DBUILD_OSG_APPLICATIONS=OFF
        -DBUILD_OSG_EXAMPLES=OFF
        -DBUILD_DOCUMENTATION=OFF
        ${OPTIONS}
    MAYBE_UNUSED_VARIABLES
        OSG_DETERMINE_WIN_VERSION
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    file(
        APPEND "${CURRENT_PACKAGES_DIR}/include/osg/Config"
        "#ifndef OSG_LIBRARY_STATIC\n#define OSG_LIBRARY_STATIC 1\n#endif\n"
    )
endif()

file(
    REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/openscenegraph.pc" "\\\n" " ")
if(NOT VCPKG_BUILD_TYPE)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/openscenegraph.pc" "\\\n" " ")
endif()
vcpkg_fixup_pkgconfig()

file(COPY "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(
    INSTALL
        "${SOURCE_PATH}/LICENSE.txt"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME
    copyright
)
