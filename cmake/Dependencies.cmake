# The third-party code the tree builds against, in one place. What it fetches: each project at a pinned
# archive, and the system's own where one is installed and the declaration names `FIND_PACKAGE_ARGS`,
# which FetchContent tries first. A preset that must build the same everywhere sets
# `FETCHCONTENT_TRY_FIND_PACKAGE_MODE` to `NEVER`, as Windows does: upstream's prebuilt set carries none
# of these. Every fetched project is a system's to the tree (`SYSTEM`), whose warnings are its own, and
# builds only what the tree links (`EXCLUDE_FROM_ALL`). Then what it finds and never fetches,
# OpenSceneGraph, Bullet, MyGUI and the rest; `extern/` holds what is vendored.

include(FetchContent)

# Each project's own settings stay inside the block; Crashpad's folder leaves it, for
# `components/crashcatcher/CMakeLists.txt`, which builds one of its files into every executable.
block(SCOPE_FOR VARIABLES PROPAGATE CRASHPAD_SOURCE_DIR)
set(CMAKE_CXX_CLANG_TIDY "")

# SDL3: the system's where it has one, and the pinned release built static otherwise, as upstream's
# prebuilt set has none. Static, so a package carries it inside the binaries. Ahead of the tree's own
# definitions, which are not SDL's: Windows's `NOGDI` and `NOMB` take out what SDL's video is built on.
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA512=74a5ee1e5bba138a8daa710e200ac3585c60f696311c46252d86bc685c55e8271cd0d68439f8e13ca515b702d46ef58914d0f7b2352019870a5763ed79739e9a
    SOURCE_DIR fetched/sdl3
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS 3.4 CONFIG
)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
FetchContent_MakeAvailable(SDL3)

# Recast at the commit pinned here on every system, built in, and never a system's: the navigator's
# tiles are its output, and `NavMeshTileStore::sTileVersion` keys them by it, which a distribution's
# own build moves without a version to say so (`DetourNavigatorTileVersionTest`).
FetchContent_Declare(recastnavigation
    URL https://github.com/OpenMW/recastnavigation/archive/03259f3287ff8330f0d66fcd98d022edddffaa97.zip
    URL_HASH SHA512=63db2ac90904764d7f1466d3c95994ec51d777cddd870b70f8ef674c09ed166b3d16ffc4ac1d18d0eab865fda4bf51b6ea17049025b66a338d3cadd0f48dc712
    SOURCE_DIR fetched/recastnavigation
    SYSTEM
    EXCLUDE_FROM_ALL
)
set(BUILD_SHARED_LIBS OFF)
set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")
FetchContent_MakeAvailable(recastnavigation)

# yaml-cpp, built static where no system's is found. One commit after the 0.8.0 tag, for CMake 4.
FetchContent_Declare(yaml-cpp
    URL https://github.com/jbeder/yaml-cpp/archive/c2680200486572baf8221ba052ef50b58ecd816e.zip
    URL_HASH SHA512=9eb328e37ff547e7f681d64e3a8f268a8a5645cb1c258106d1d9f959eec498dfc7c466690c56f210ad7d5fb95b806a30c87ccbb019cccf5c3c6135cfc3c13fe1
    SOURCE_DIR fetched/yaml-cpp
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS 0.8 CONFIG
)
set(YAML_BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(yaml-cpp)

if (OPENMW_BUILD_COMPONENTS_TESTS OR OPENMW_BUILD_CS_TESTS OR OPENMW_BUILD_GAME_TESTS)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        URL_HASH SHA512=2712d6f6e7f38eb0c8d5ebba957588c70aff68ea433c7aa0b31bde8cacfbe47de908a6e5a99a021969da568f5075daa2c45fca602bbf56711d146d333b76562b
        SOURCE_DIR fetched/googletest
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS 1.10 NAMES GTest CONFIG
    )
    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON)
    FetchContent_MakeAvailable(googletest)
endif ()

if (OPENMW_BUILD_BENCHMARKS)
    # 1.10, whose header no longer reads `__COUNTER__` in an `#if`, which Clang 23 counts a C2y extension
    # and 1.8's own `-pedantic-errors` made an error.
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.10.0.zip
        URL_HASH SHA512=9aaa9f7bbaa0dfa3a34932f31c0dad5cef5b5c15a7da2ac4e6567c29428b43e791f1d9a7c2541127cbde199d3e7c4a61516ae4c06db2962503082bb851bae8d0
        SOURCE_DIR fetched/benchmark
        SYSTEM
        EXCLUDE_FROM_ALL
        FIND_PACKAGE_ARGS CONFIG
    )
    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)
    FetchContent_MakeAvailable(benchmark)
endif ()

# volk, which loads the Vulkan loader when the first instance is made rather than when the program
# starts: no program imports it, so the game starts on a machine with no loader or an old one and says
# why it cannot draw there, and the ray tracer refuses the loader by name (`Instance`). The system's
# package only at the SDK version the tree is built against or a later one, which volk's own version is: an older one lacks the Vulkan 1.4 entry points the backend calls
# (`vkCmdPushDescriptorSet`), and would be taken and then fail to compile.
set(RTX_VOLK_VERSION 357)
FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.${RTX_VOLK_VERSION}.0.tar.gz
    URL_HASH SHA512=44feaf57236d8f52d4bde05cf4ba59de5df4730b6c8dc38c278a3aa13d02d4bb48e11660832232f7122c2d55b65d60fe9b5f8fe6725ee63d5d5213e5275b7110
    SOURCE_DIR fetched/volk
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS ${RTX_VOLK_VERSION} CONFIG QUIET
)
set(VOLK_HEADERS_ONLY ON)
set(VOLK_PULL_IN_VULKAN OFF)
FetchContent_MakeAvailable(volk)

# Vulkan Memory Allocator, behind `device/memory/memory.hpp`: a system's only at the pinned version or a
# later one of its major.
set(RTX_VMA_VERSION 3.4.0)
FetchContent_Declare(VulkanMemoryAllocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v${RTX_VMA_VERSION}.tar.gz
    URL_HASH SHA512=563acbcd8912d10d92c23715eba7acf0e7c1683af36021f415b36f359c2cce065f3906e395c32282a8410ec5c8179fbcb6412935c6629a49357475d4b4410e2a
    SOURCE_DIR fetched/vma
    SYSTEM
    EXCLUDE_FROM_ALL
    FIND_PACKAGE_ARGS ${RTX_VMA_VERSION} CONFIG QUIET
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

include("${CMAKE_CURRENT_LIST_DIR}/../extern/crashpad.cmake")
endblock()

# What the tree finds and never fetches. A package whose find module defines no target gets one here, an
# imported interface target carrying its headers, its libraries and the definitions its headers need, so
# what links it compiles against it and nothing else does; imported, so its headers are a system's.

set(OpenGL_GL_PREFERENCE LEGACY) # GL2; GLVND's is for GL3 and up.
find_package(OpenGL REQUIRED)
find_package(LZ4 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenAL REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n data)
find_package(Boost 1.83.0 CONFIG REQUIRED COMPONENTS iostreams program_options)
find_package(MyGUI 3.4.3 REQUIRED)
# 1.4.329 is the first SDK whose SPIR-V headers name `SPV_KHR_fma`, which the shaders' pinning
# refuses by name (`Rtx::pinFloatArithmetic`); an older one fails here rather than inside
# `spirvpin.cpp`.
find_package(VulkanHeaders 1.4.329 REQUIRED)

if (USE_QT)
    find_package(QT REQUIRED COMPONENTS Core NAMES Qt6)
    find_package(Qt6 COMPONENTS Core Widgets Network OpenGL OpenGLWidgets LinguistTools Svg REQUIRED)
    message(STATUS "Using Qt${QT_VERSION}")
endif ()

if (OPENMW_USE_UNSHIELD)
    find_package(LIBUNSHIELD REQUIRED)
endif ()

# FFmpeg 4.0 at least.
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)
foreach (floor AVFORMAT:58.12.100 AVCODEC:58.18.100 AVUTIL:56.14.100 SWSCALE:5.1.100 SWRESAMPLE:3.1.100)
    string(REPLACE ":" ";" floor "${floor}")
    list(GET floor 0 library)
    list(GET floor 1 version)
    if (FFmpeg_${library}_VERSION VERSION_LESS version)
        message(FATAL_ERROR "FFmpeg's ${library} is ${FFmpeg_${library}_VERSION}, older than ${version}: FFmpeg 4.0 is required")
    endif ()
endforeach ()
add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
target_include_directories(FFmpeg::FFmpeg INTERFACE ${FFmpeg_INCLUDE_DIRS})
target_link_libraries(FFmpeg::FFmpeg INTERFACE ${FFmpeg_LIBRARIES})
# Required for building the FFmpeg headers
target_compile_definitions(FFmpeg::FFmpeg INTERFACE __STDC_CONSTANT_MACROS)

if (OPENMW_SYSTEM_TINYXML)
    find_package(TinyXML REQUIRED)
    add_library(TinyXML::TinyXML INTERFACE IMPORTED)
    target_include_directories(TinyXML::TinyXML INTERFACE ${TinyXML_INCLUDE_DIRS})
    target_link_libraries(TinyXML::TinyXML INTERFACE ${TinyXML_LIBRARIES})
    target_compile_definitions(TinyXML::TinyXML INTERFACE TIXML_USE_STL)
endif ()

# Bullet 2.87 at least, for `btIDebugDraw::clearLines` (2.86 already needed for `btCapsuleShape` fixes),
# and in double precision. First the float64 config Debian's derivatives ship, which states its version
# in no form CMake compares.
set(REQUIRED_BULLET_VERSION 287)
find_package(Bullet CONFIGS BulletConfig-float64.cmake QUIET COMPONENTS BulletCollision LinearMath)
if (BULLET_FOUND)
    string(REPLACE "." "" _bullet_version_num ${BULLET_VERSION_STRING})
    if (_bullet_version_num VERSION_LESS REQUIRED_BULLET_VERSION)
        message(FATAL_ERROR "System bullet version too old, OpenMW requires at least ${REQUIRED_BULLET_VERSION}, got ${_bullet_version_num}")
    endif ()
    # Fix the relative include:
    set(BULLET_INCLUDE_DIRS "${BULLET_ROOT_DIR}/${BULLET_INCLUDE_DIRS}")
    include(FindPackageMessage)
    find_package_message(Bullet "Found Bullet: ${BULLET_LIBRARIES} ${BULLET_VERSION_STRING}" "${BULLET_VERSION_STRING}-float64")
else ()
    find_package(Bullet ${REQUIRED_BULLET_VERSION} REQUIRED COMPONENTS BulletCollision LinearMath)
endif ()
# Only link the Bullet libraries that we need:
string(REGEX MATCHALL "((optimized|debug);)?[^;]*(BulletCollision|LinearMath)[^;]*" BULLET_LIBRARIES "${BULLET_LIBRARIES}")
include(CheckBulletPrecision)
if (NOT HAS_DOUBLE_PRECISION_BULLET)
    message(FATAL_ERROR "Bullet does not uses double precision")
endif ()
add_library(Bullet::Bullet INTERFACE IMPORTED)
target_include_directories(Bullet::Bullet INTERFACE ${BULLET_INCLUDE_DIRS})
target_link_libraries(Bullet::Bullet INTERFACE ${BULLET_LIBRARIES})
target_compile_definitions(Bullet::Bullet INTERFACE BT_USE_DOUBLE_PRECISION)

add_library(MyGUI::MyGUI INTERFACE IMPORTED)
target_include_directories(MyGUI::MyGUI INTERFACE ${MyGUI_INCLUDE_DIRS})
target_link_libraries(MyGUI::MyGUI INTERFACE ${MyGUI_LIBRARIES})
target_compile_definitions(MyGUI::MyGUI INTERFACE MYGUI_DONT_USE_OBSOLETE=ON)

set(USED_OSG_PLUGINS
    osgdb_bmp
    osgdb_dae
    osgdb_dds
    osgdb_freetype
    osgdb_jpeg
    osgdb_osg
    osgdb_png
    osgdb_serializers_osg
    osgdb_tga)
find_package(OpenSceneGraph 3.6.5 REQUIRED
    osgAnimation osgDB osgGA osgFX osgParticle osgText osgUtil osgShadow osgSim osgViewer)
if (OPENMW_DEPLOY_RUNTIME_DEPENDENCIES)
    if (WIN32)
        set(OSGPlugins_DONT_FIND_DEPENDENCIES 1)
    endif ()
    find_package(OSGPlugins REQUIRED COMPONENTS ${USED_OSG_PLUGINS})
    foreach (osgplugin ${USED_OSG_PLUGINS})
        if (NOT TARGET ${osgplugin})
            add_library(${osgplugin} MODULE IMPORTED)
            string(TOUPPER ${osgplugin} pluginnameupper)
            foreach (configuration RELEASE DEBUG)
                if (${pluginnameupper}_LIBRARY_${configuration})
                    set_property(TARGET ${osgplugin} APPEND PROPERTY IMPORTED_CONFIGURATIONS ${configuration})
                    set_target_properties(${osgplugin} PROPERTIES
                        IMPORTED_LOCATION_${configuration} "${${pluginnameupper}_LIBRARY_${configuration}}")
                endif ()
            endforeach ()
        endif ()
    endforeach ()
    if (WIN32)
        set(osg_plugin_destination "${CMAKE_INSTALL_BINDIR}")
    else ()
        set(osg_plugin_destination "${CMAKE_INSTALL_LIBDIR}")
    endif ()
    install(IMPORTED_RUNTIME_ARTIFACTS ${USED_OSG_PLUGINS}
        DESTINATION "${osg_plugin_destination}/osgPlugins-${OPENSCENEGRAPH_VERSION}"
        RUNTIME_DEPENDENCY_SET openmw-dependencies)
endif ()
# One target a library of OpenSceneGraph's, each with the headers; `osg` with the multiview definition.
include(CheckOsgMultiview)
foreach (library OpenThreads osg osgAnimation osgDB osgGA osgFX osgParticle osgText osgUtil osgShadow osgSim osgViewer)
    string(TOUPPER ${library} upper)
    add_library(OpenSceneGraph::${library} INTERFACE IMPORTED)
    target_include_directories(OpenSceneGraph::${library} INTERFACE ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_link_libraries(OpenSceneGraph::${library} INTERFACE ${${upper}_LIBRARIES})
endforeach ()
target_link_libraries(OpenSceneGraph::osg INTERFACE OpenSceneGraph::OpenThreads)
if (HAVE_MULTIVIEW)
    target_compile_definitions(OpenSceneGraph::osg INTERFACE OSG_HAS_MULTIVIEW)
endif ()

if (OPENMW_LUAJIT)
    find_package(LuaJit REQUIRED)
    set(LUA_INCLUDE_DIR ${LuaJit_INCLUDE_DIR})
    set(LUA_LIBRARIES ${LuaJit_LIBRARIES})
else ()
    find_package(Lua REQUIRED)
endif ()
if (NOT WIN32)
    include(CheckLuaCustomAllocator)
endif ()
add_library(Lua::Lua INTERFACE IMPORTED)
target_include_directories(Lua::Lua INTERFACE ${LUA_INCLUDE_DIR})
target_link_libraries(Lua::Lua INTERFACE ${LUA_LIBRARIES})
if (NOT OPENMW_LUAJIT)
    target_compile_definitions(Lua::Lua INTERFACE NO_LUAJIT)
endif ()
