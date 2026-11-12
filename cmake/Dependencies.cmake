# The third-party code the tree builds against, in one place: what it fetches and builds itself, each project at a
# pinned archive and never a system's, with the tree's own flags and sanitizer (ThreadSanitizer needs that of every
# library whose locks it is to see); then what it finds in the vcpkg set, OpenSceneGraph, Bullet, MyGUI and the rest.
# Every fetched project is a system's to the tree (`SYSTEM`), whose warnings are its own, and builds only what the
# tree links (`EXCLUDE_FROM_ALL`). `extern/` holds what is vendored.

include(FetchContent)

# Each project's own settings stay inside the block; Crashpad's folder leaves it, for
# `components/crashcatcher/CMakeLists.txt`, which builds one of its files into every executable.
block(SCOPE_FOR VARIABLES PROPAGATE CRASHPAD_SOURCE_DIR)
    # A third party's warnings are its own (`docs/build.md`): off, by `-w` on Clang, which wins over
    # any `-W` a project adds after it. Each project sets its own MSVC level, which `/w` would override with a
    # D9025 a file, so MSVC turns off the one warning a project raises, on its target.
    if(NOT MSVC)
        string(APPEND CMAKE_C_FLAGS " -w")
        string(APPEND CMAKE_CXX_FLAGS " -w")
    endif()
    set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

    # SDL3, the pinned release built static, so a package carries it inside the binaries. Ahead of the
    # tree's own definitions, which are not SDL's: Windows's `NOGDI` and `NOMB` take out what SDL's video
    # is built on.
    FetchContent_Declare(
        SDL3
        URL
            https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH
            SHA512=6ac2566e58f4db58112dcf328ee829f855f5f3c00329ebb358be706f8958fa980d0e0d7247678b76a8bc6e9137fc4a17424d84105f39de178af3a99680559d63
        SOURCE_DIR fetched/sdl3
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    FetchContent_MakeAvailable(SDL3)

    # Recast at the commit pinned here on every system, built in, and never a system's: the navigator's
    # tiles are its output, and `NavMeshTileStore::sTileVersion` keys them by it, which a distribution's
    # own build moves without a version to say so (`DetourNavigatorTileVersionTest`).
    FetchContent_Declare(
        recastnavigation
        URL
            https://github.com/OpenMW/recastnavigation/archive/03259f3287ff8330f0d66fcd98d022edddffaa97.zip
        URL_HASH
            SHA512=63db2ac90904764d7f1466d3c95994ec51d777cddd870b70f8ef674c09ed166b3d16ffc4ac1d18d0eab865fda4bf51b6ea17049025b66a338d3cadd0f48dc712
        SOURCE_DIR fetched/recastnavigation
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    set(BUILD_SHARED_LIBS OFF)
    set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
    set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
    set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")
    FetchContent_MakeAvailable(recastnavigation)

    # yaml-cpp, built static.
    FetchContent_Declare(
        yaml-cpp
        URL
            https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.9.0.tar.gz
        URL_HASH
            SHA512=59f730e8c5744f1ccd542c1144db8d4d949012f72aab0b84ba4a818db25a0f847569b61238ab72ed5b0b2e9482b8d5007651b7185f4ca9e99045d5160259b565
        SOURCE_DIR fetched/yaml-cpp
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    set(YAML_BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(yaml-cpp)
    target_compile_options(yaml-cpp PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4267>)

    if(BUILD_TESTING)
        FetchContent_Declare(
            googletest
            URL
                https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip
            URL_HASH
                SHA512=68cdce1c185237862e3bcf73a9b8f485fa611307228122d8f8863b7faf6354d8ff0d9d1551f3533bdc1b01655a731e8e72027b72d8e0b20e63a4582c06708f78
            SOURCE_DIR fetched/googletest
            SYSTEM
            EXCLUDE_FROM_ALL
        )
        set(INSTALL_GTEST OFF)
        FetchContent_MakeAvailable(googletest)

        # 1.10, whose header no longer reads `__COUNTER__` in an `#if`, which Clang 23 counts a C2y extension
        # and 1.8's own `-pedantic-errors` made an error.
        FetchContent_Declare(
            benchmark
            URL
                https://github.com/google/benchmark/archive/refs/tags/v1.10.0.zip
            URL_HASH
                SHA512=9aaa9f7bbaa0dfa3a34932f31c0dad5cef5b5c15a7da2ac4e6567c29428b43e791f1d9a7c2541127cbde199d3e7c4a61516ae4c06db2962503082bb851bae8d0
            SOURCE_DIR fetched/benchmark
            SYSTEM
            EXCLUDE_FROM_ALL
        )
        set(BENCHMARK_ENABLE_TESTING OFF)
        set(BENCHMARK_ENABLE_INSTALL OFF)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
        set(BENCHMARK_ENABLE_WERROR OFF)
        FetchContent_MakeAvailable(benchmark)
    endif()

    # volk, which loads the Vulkan loader when the first instance is made rather than when the program
    # starts: no program imports it, so the game starts on a machine with no loader or an old one and says
    # why it cannot draw there, and the ray tracer refuses the loader by name (`Instance`). At the SDK
    # version of the set's Vulkan headers.
    FetchContent_Declare(
        volk
        URL
            https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
        URL_HASH
            SHA512=c1d97fb4419effe08e84dc83e220229e1a070359b4d7187cfd54142df0dd0cee525d791d89e0deadb399d270772805ba1f48f8f6c6f3dd3820a44a947ca297db
        SOURCE_DIR fetched/volk
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    set(VOLK_HEADERS_ONLY ON)
    set(VOLK_PULL_IN_VULKAN OFF)
    FetchContent_MakeAvailable(volk)

    # Vulkan Memory Allocator, behind `device/memory/memory.hpp`.
    FetchContent_Declare(
        VulkanMemoryAllocator
        URL
            https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.5.0.tar.gz
        URL_HASH
            SHA512=ad6232fa175aea9cfd16f240eed541eb1fb86349aa98c94ae874de85dd53ae78b1db9b293556d8f178c0648e2287f63109caede8595a1ba9ff244c9ddb0b9c48
        SOURCE_DIR fetched/vma
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)

    include("${CMAKE_CURRENT_LIST_DIR}/../extern/crashpad.cmake")
endblock()

# What the tree finds and never fetches: the vcpkg set (`vcpkg.json`), static, through the toolchain file the presets
# name, and the system's OpenGL, X11 and Threads. Each package by its own CMake files where it installs them; a
# package whose files define no target gets an imported interface target here, carrying its headers, its libraries
# and the definitions its headers need, so that only what links it compiles against it; imported, so its headers are
# a system's. No version floors: the set is the one version the tree is built against.

# A library the set installs no CMake files for, or files that define no target: an imported target of its archive,
# found by `NAMES` (in the folder `IN` alone where it is given), with the folder of its `HEADER` (under one of
# `HEADER_SUFFIXES`) and `LINKS`, what its archive calls.
function(openmw_import_archive target)
    cmake_parse_arguments(PARSE_ARGV 1 archive "" "HEADER;IN" "NAMES;HEADER_SUFFIXES;LINKS")
    string(MAKE_C_IDENTIFIER "${target}" id)
    if(archive_IN)
        set(only NO_DEFAULT_PATH)
    endif()
    find_library(
        ${id}_LIBRARY
        NAMES
            ${archive_NAMES}
        PATHS
            ${archive_IN}
            ${only}
        REQUIRED
    )
    add_library(${target} STATIC IMPORTED)
    set_target_properties(
        ${target}
        PROPERTIES
            IMPORTED_LOCATION
                "${${id}_LIBRARY}"
    )
    if(archive_HEADER)
        find_path(${id}_INCLUDE_DIR ${archive_HEADER} PATH_SUFFIXES ${archive_HEADER_SUFFIXES} REQUIRED)
        set_target_properties(
            ${target}
            PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES
                    "${${id}_INCLUDE_DIR}"
        )
    endif()
    target_link_libraries(${target} INTERFACE ${archive_LINKS})
endfunction()

set(OpenGL_GL_PREFERENCE LEGACY) # GL2; GLVND's is for GL3 and up.
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(OpenAL CONFIG REQUIRED)
find_package(
    ICU
    REQUIRED
    COMPONENTS
        uc
        i18n
        data
)
find_package(
    Boost
    CONFIG
    REQUIRED
    COMPONENTS
        filesystem
        iostreams
        program_options
)
# MyGUI 3.4.3 installs no CMake files, and pkg-config's on Linux alone, which names neither the FreeType its
# archive calls nor the definitions its headers need: its header and its archive, found in the set.
openmw_import_archive(
    MyGUI::MyGUI
    NAMES
        MyGUIEngineStatic
    HEADER MyGUI.h
    HEADER_SUFFIXES
        MYGUI
    LINKS
        Freetype::Freetype
)
target_compile_definitions(
    MyGUI::MyGUI
    INTERFACE
        MYGUI_DONT_USE_OBSOLETE
        MYGUI_STATIC
)
find_package(VulkanHeaders CONFIG REQUIRED)

find_package(
    Qt6
    REQUIRED
    COMPONENTS
        Core
        Widgets
        Network
        OpenGL
        OpenGLWidgets
        LinguistTools
        Svg
        UiTools
)
# The windowing platforms a Qt program opens on, which the static Qt links into it: Qt's default, X11's on Linux and
# Windows' own, and on Linux Wayland's beside it, so a session with no XWayland opens it too.
function(openmw_qt_platforms target)
    if(LINUX)
        qt_import_plugins(${target} INCLUDE Qt6::QWaylandIntegrationPlugin)
    endif()
endfunction()

# libunshield for the wizard, which unpacks the installer's archives everywhere but on Windows, whose players run
# the installer itself. Its port installs no CMake files, and its pkg-config file names none of the zlib its
# archive calls.
if(NOT WIN32)
    openmw_import_archive(Unshield::Unshield NAMES unshield HEADER libunshield.h LINKS ZLIB::ZLIB)
endif()

# FFmpeg through the set's own find module, which names every library its static ones link.
find_package(FFMPEG REQUIRED)
add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
target_include_directories(FFmpeg::FFmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(FFmpeg::FFmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(FFmpeg::FFmpeg INTERFACE ${FFMPEG_LIBRARIES})
# Required for building the FFmpeg headers
target_compile_definitions(FFmpeg::FFmpeg INTERFACE __STDC_CONSTANT_MACROS)

# Bullet in double precision, the set's `bullet3[double-precision]`, whose config states the definition.
find_package(Bullet CONFIG REQUIRED)
add_library(Bullet::Bullet INTERFACE IMPORTED)
target_include_directories(Bullet::Bullet INTERFACE ${BULLET_INCLUDE_DIRS})
target_link_libraries(
    Bullet::Bullet
    INTERFACE
        BulletCollision
        LinearMath
)
if(NOT BULLET_DEFINITIONS STREQUAL "-DBT_USE_DOUBLE_PRECISION")
    message(FATAL_ERROR "Bullet is built in single precision (${BULLET_DEFINITIONS}): the tree's physics is double")
endif()
target_compile_definitions(Bullet::Bullet INTERFACE BT_USE_DOUBLE_PRECISION)

# OpenSceneGraph, OpenMW's fork, static: one target a library of its, each with the headers and with what
# its archive calls, which no file of the port states, and the headers alone for what names its types and
# calls nothing of it, the build's table makers. `OSG_HAS_MULTIVIEW` stays undefined: the fork at the commit
# the set pins has no multiview (`osg::Camera::FACE_CONTROLLED_BY_MULTIVIEW_SHADER`). The plugins the tree reads
# with are linked into every program that links the components and registered there
# (`components/resource/osgplugins.cpp.in`).
find_package(
    OpenSceneGraph
    REQUIRED
        osgAnimation
        osgDB
        osgGA
        osgFX
        osgParticle
        osgText
        osgUtil
        osgShadow
        osgSim
        osgViewer
)
add_library(OpenSceneGraph::headers INTERFACE IMPORTED)
target_include_directories(OpenSceneGraph::headers INTERFACE ${OPENSCENEGRAPH_INCLUDE_DIRS})
foreach(
    library
    OpenThreads
    osg
    osgAnimation
    osgDB
    osgGA
    osgFX
    osgParticle
    osgText
    osgUtil
    osgShadow
    osgSim
    osgViewer
)
    string(TOUPPER ${library} upper)
    add_library(OpenSceneGraph::${library} INTERFACE IMPORTED)
    target_link_libraries(
        OpenSceneGraph::${library}
        INTERFACE
            OpenSceneGraph::headers
            ${${upper}_LIBRARIES}
    )
endforeach()
target_link_libraries(OpenSceneGraph::OpenThreads INTERFACE Threads::Threads)
target_link_libraries(
    OpenSceneGraph::osg
    INTERFACE
        OpenSceneGraph::OpenThreads
        OpenGL::GL
        ${CMAKE_DL_LIBS}
)
target_link_libraries(
    OpenSceneGraph::osgDB
    INTERFACE
        OpenSceneGraph::osg
        ZLIB::ZLIB
)
if(WIN32)
    target_link_libraries(
        OpenSceneGraph::osgViewer
        INTERFACE
            OpenSceneGraph::osgText
            OpenSceneGraph::osgGA
    )
else()
    find_package(X11 REQUIRED COMPONENTS Xinerama)
    target_link_libraries(
        OpenSceneGraph::osgViewer
        INTERFACE
            OpenSceneGraph::osgText
            OpenSceneGraph::osgGA
            X11::X11
            X11::Xinerama
    )
endif()

# The plugins, each an archive in OSG's plugin folder with what it calls.
set(USED_OSG_PLUGINS
    osgdb_bmp
    osgdb_dae
    osgdb_dds
    osgdb_freetype
    osgdb_jpeg
    osgdb_osg
    osgdb_png
    osgdb_serializers_osg
    osgdb_tga
)
find_package(PNG REQUIRED)
find_package(JPEG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(minizip CONFIG REQUIRED)
find_package(uriparser CONFIG REQUIRED)
# By the set's own folder: the port installs its config under `share/collada-dom`, which CMake's search for
# `collada_dom` does not name, so it would take a system's instead.
find_package(
    collada_dom
    CONFIG
    REQUIRED
    PATHS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/collada-dom"
    NO_DEFAULT_PATH
)
# Named as its config means to name it and fails to, its version unset there: with MSVC's suffix on Windows,
# which the port's own patch fixes at `vc140-mt`.
set(collada_dom_name "collada-dom${collada_dom_VERSION_MAJOR}.${collada_dom_VERSION_MINOR}-dp")
find_library(
    collada_dom_library
    NAMES
        ${collada_dom_name}
        ${collada_dom_name}-vc140-mt
    PATHS
        ${COLLADA_DOM_LIBRARY_DIRS}
    NO_DEFAULT_PATH
    REQUIRED
)
set(osgdb_png_calls PNG::PNG)
set(osgdb_jpeg_calls JPEG::JPEG)
set(osgdb_freetype_calls Freetype::Freetype)
set(osgdb_dae_calls
    "${collada_dom_library}"
    LibXml2::LibXml2
    MINIZIP::minizipstatic
    uriparser::uriparser
    Boost::filesystem
)
cmake_path(GET OSGDB_LIBRARY PARENT_PATH osgdb_folder)
foreach(plugin IN LISTS USED_OSG_PLUGINS)
    openmw_import_archive(
        OpenSceneGraph::${plugin}
        NAMES
            ${plugin}
        IN "${osgdb_folder}/osgPlugins-${OPENSCENEGRAPH_VERSION}"
        LINKS
            OpenSceneGraph::osgDB
            ${${plugin}_calls}
    )
endforeach()

# LuaJIT, the Lua every build runs scripts on: its header and its archive, as the port installs no CMake files
# and pkg-config's on Linux alone. `lua51` is its archive's name on Windows.
openmw_import_archive(
    Lua::Lua
    NAMES
        luajit-5.1
        lua51
    HEADER luajit.h
    HEADER_SUFFIXES
        luajit
        luajit-2.1
    LINKS
        ${CMAKE_DL_LIBS}
        $<$<PLATFORM_ID:Linux>:m>
)
