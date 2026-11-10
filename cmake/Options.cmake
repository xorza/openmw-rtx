# What a configure chooses: the programs and suites it builds, the features compiled in, and where an
# install puts things. The presets set these; a bare configure gets the defaults.

option(BUILD_OPENMW             "Build OpenMW" ON)
option(BUILD_LAUNCHER           "Build Launcher" ON)
option(BUILD_WIZARD             "Build Installation Wizard" ON)
option(BUILD_MWINIIMPORTER      "Build MWiniImporter" ON)
option(BUILD_OPENCS             "Build OpenMW Construction Set" ON)
option(BUILD_ESSIMPORTER        "Build ESS (Morrowind save game) importer" ON)
option(BUILD_BSATOOL            "Build BSA extractor" ON)
option(BUILD_ESMTOOL            "Build ESM inspector" ON)
option(BUILD_NIFTEST            "Build nif file tester" ON)
option(BUILD_NAVMESHTOOL        "Build navmesh tool" ON)
option(BUILD_BULLETOBJECTTOOL   "Build Bullet object tool" ON)
option(BUILD_DOCS               "Build documentation." OFF)
option(BUILD_COMPONENTS_TESTS   "Build components library tests" OFF)
option(BUILD_OPENCS_TESTS       "Build OpenMW Construction Set tests" OFF)
option(BUILD_OPENMW_TESTS       "Build OpenMW tests" OFF)
option(BUILD_BENCHMARKS         "Build benchmarks with Google Benchmark" OFF)
option(BUILD_WITH_CODE_COVERAGE "Enable code coverage with gconv" OFF)

option(OPENMW_RTX_DLSS "Build the ray tracer with NVIDIA DLSS support" ON)
option(OPENMW_CRASHPAD "Capture crashes with Crashpad" ON)
option(USE_LUAJIT "Switch Lua/LuaJit (TRUE is highly recommended)" ON)
option(USE_SYSTEM_TINYXML "Use system TinyXML library instead of internal." OFF)
option(OPENMW_LTO_BUILD "Build OpenMW with Link-Time Optimization (Needs ~2GB of RAM)" OFF)
option(PRECOMPILE_HEADERS_WITH_MSVC "Precompile most common used headers with MSVC (alternative to ccache)" ON)
if (WIN32)
    option(USE_DEBUG_CONSOLE "Whether a console should be displayed if OpenMW isn't launched from the command line. Does not affect the Release configuration." ON)
endif ()

# Qt for every program that draws a window of its own.
if (BUILD_LAUNCHER OR BUILD_OPENCS OR BUILD_WIZARD OR BUILD_OPENCS_TESTS)
    set(USE_QT TRUE)
else ()
    set(USE_QT FALSE)
endif ()

# libunshield for the wizard, which unpacks the installer's archives everywhere but on Windows, whose
# players run the installer itself.
if (NOT WIN32 AND BUILD_WIZARD)
    set(OPENMW_USE_UNSHIELD TRUE)
endif ()

# A bare configure with no build type builds with line tables and the optimizer.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif ()

# **Where an install puts things.** Windows installs into one folder by default, every file beside the
# executables; Linux installs into the system's folders, with the data and configuration paths baked
# into the binaries.
if (WIN32)
    set(deploy_by_default ON)
else ()
    set(deploy_by_default OFF)
endif ()
option(DEPLOY_QT_TRANSLATIONS "Deploy standard Qt translations to resources folder. Needed when OpenMW applications are deployed with Qt libraries" ${deploy_by_default})
option(OPENMW_DEPLOY_RUNTIME_DEPENDENCIES "Deploy runtime dependency libraries. Needed when not relying on a system-wide insallation of these libraries existing" ${deploy_by_default})
option(OPENMW_LOCAL_INSTALL "Local install of OpenMW instead of Global. Other files will be installed to the same location as the binaries, and accessed via relative paths." ${deploy_by_default})

set(runtime_dependency_arguments)
if (OPENMW_DEPLOY_RUNTIME_DEPENDENCIES)
    set(runtime_dependency_arguments RUNTIME_DEPENDENCY_SET openmw-dependencies)
endif ()

include(GNUInstallDirs)
set(SYSCONFDIR_SUFFIX "openmw" CACHE PATH "Suffix for CMAKE_INSTALL_SYSCONFDIR")
set(DATADIR_SUFFIX "openmw" CACHE PATH "Suffix for CMAKE_INSTALL_DATADIR")
set(CMAKE_INSTALL_SYSCONFDIR "${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFDIR_SUFFIX}")
set(CMAKE_INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/${DATADIR_SUFFIX}")
if (OPENMW_LOCAL_INSTALL)
    set(CMAKE_INSTALL_BINDIR ".")
    set(CMAKE_INSTALL_LIBDIR "lib")
    set(CMAKE_INSTALL_DATADIR ".")
    set(CMAKE_INSTALL_SYSCONFDIR ".")
endif ()
foreach (dir BINDIR SYSCONFDIR LIBDIR DATADIR)
    GNUInstallDirs_get_absolute_install_dir(CMAKE_INSTALL_FULL_${dir} CMAKE_INSTALL_${dir} ${dir})
endforeach ()

if (UNIX)
    set(ICONDIR "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps" CACHE PATH "Set icon dir")
    if (OPENMW_LOCAL_INSTALL)
        # The libraries beside the executables, found relative to them. `cmake_path`, because
        # `file(RELATIVE_PATH)` cannot relate two relative paths.
        cmake_path(RELATIVE_PATH CMAKE_INSTALL_LIBDIR BASE_DIRECTORY "${CMAKE_INSTALL_BINDIR}"
            OUTPUT_VARIABLE origin_relative_libdir)
        set(CMAKE_INSTALL_RPATH "$ORIGIN/${origin_relative_libdir}")
    else ()
        set(GLOBAL_DATA_PATH "${CMAKE_INSTALL_FULL_DATADIR}" CACHE PATH "Set absolute data path prefix for Global installs. This is baked into the binaries, so MUST be set to the final install path at build time.")
        set(GLOBAL_CONFIG_PATH "${CMAKE_INSTALL_FULL_SYSCONFDIR}" CACHE PATH "Set absolute config dir path for Global installs. This is baked into the binaries, so MUST be set to the final install path at build time.")
        set(OPENMW_RESOURCE_FILES "${CMAKE_INSTALL_FULL_DATADIR}/resources" CACHE PATH "Set absolute resources dir path for Global installs. This is baked into openmw.cfg, so MUST be set to the final install path at build time.")
    endif ()
endif ()
