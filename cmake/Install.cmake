# What an install carries beside the programs, which each app's own `install(TARGETS)` names: the
# runtime libraries where a configure deploys them, the desktop files, the documents and licences,
# and the game's defaults and resources. `omw archive` and CI install through it; no installer is
# built from it.

# The licences of what the fork compiles into the binaries, beside the GPL's own text: each asks for
# its notice in every copy, binaries included, but SDL's zlib, which goes in so that no licence of
# what is compiled in is missing. `files/licenses` holds each text as its source ships it, and
# FidelityFX's stays with its headers.
set(licenses "${CMAKE_INSTALL_DOCDIR}/licenses")
install(
    FILES
        "${PROJECT_SOURCE_DIR}/extern/fidelityfx/LICENSE.txt"
    DESTINATION "${licenses}"
    RENAME "FidelityFX-SDK.txt"
)
install(
    FILES
        "${PROJECT_SOURCE_DIR}/files/licenses/VulkanMemoryAllocator.txt"
        "${PROJECT_SOURCE_DIR}/files/licenses/volk.txt"
        "${PROJECT_SOURCE_DIR}/files/licenses/SDL3.txt"
        "${PROJECT_SOURCE_DIR}/files/licenses/rgbcx.txt"
        "${PROJECT_SOURCE_DIR}/files/licenses/bc7enc.txt"
    DESTINATION "${licenses}"
)
if(OPENMW_CRASHPAD)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/files/licenses/crashpad/" DESTINATION "${licenses}/crashpad")
endif()

install(FILES "${PROJECT_SOURCE_DIR}/CHANGELOG.md" TYPE DOC)
install(FILES "${PROJECT_SOURCE_DIR}/README.md" TYPE DOC)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" TYPE DOC RENAME "LICENSE.txt")
install(FILES "${PROJECT_SOURCE_DIR}/AUTHORS.md" TYPE DOC)

install(
    FILES
        "${RUNTIME_OUTPUT_DIRECTORY}/defaults.bin"
        "${RUNTIME_OUTPUT_DIRECTORY}/gamecontrollerdb.txt"
    TYPE SYSCONF
)
if(OPENMW_LOCAL_INSTALL)
    install(FILES "${PROJECT_SOURCE_DIR}/files/openmw.cfg.local" TYPE SYSCONF RENAME "openmw.cfg")
else()
    configure_file("${PROJECT_SOURCE_DIR}/files/openmw.cfg" "openmw.cfg.install")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/openmw.cfg.install" TYPE SYSCONF RENAME "openmw.cfg")
endif()
install(DIRECTORY "${RUNTIME_OUTPUT_DIRECTORY}/resources" TYPE DATA)

if(OPENMW_DEPLOY_RUNTIME_DEPENDENCIES)
    if(WIN32)
        # CMake takes some of Windows's API set names for real DLLs, so every system DLL is excluded
        # by name: https://discourse.cmake.org/t/get-runtime-dependencies-has-difficulties-with-windows-api-sets/1768/23
        set(pre_exclude_regexes
            ^msvcp.*\\.dll$
            ^vcruntime.*\\.dll$
            ^api-ms-win-.*
            ^ext-ms-.*
            ^hvsifiletrust\\.dll$
            ^pdmutilities\\.dll$
        )
        set(systemroot "$ENV{SystemRoot}")
        cmake_path(NORMAL_PATH systemroot)
        escape_regex_control_characters(systemroot "${systemroot}")
        set(post_exclude_regexes ^${systemroot}/)
        # CMP0207 will avoid the need for this
        string(
            REPLACE "/"
            "(/|\\\\)"
            post_exclude_regexes
            "${post_exclude_regexes}"
        )
        set(directories "${RUNTIME_OUTPUT_DIRECTORY}")

        # **The C++ runtime the binaries were built against, beside them**, from the toolset that
        # built them: the fork ships a portable folder and not upstream's installer, which ran the
        # redistributable. The application's folder is searched before the system's, so an older
        # runtime installed system-wide, inside whose `std::mutex::lock` binaries of MSVC 17.10 and
        # later fault, is never the one loaded. The universal CRT is the system's from Windows 10 on.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
        set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
        include(InstallRequiredSystemLibraries)
    else()
        set(pre_exclude_regexes ^libopenal.so.*)
        set(post_exclude_regexes
            ^/lib64/
            ^/usr/lib64/
        )
        set(directories)
    endif()
    install(
        RUNTIME_DEPENDENCY_SET
            openmw-dependencies
        PRE_EXCLUDE_REGEXES
            ${pre_exclude_regexes}
        POST_EXCLUDE_REGEXES
            ${post_exclude_regexes}
        DIRECTORIES
            ${directories}
    )
endif()

if(WIN32)
    # Qt's plugins, copied here rather than by `windeployqt.exe`, which adds what the programs do not
    # use, ANGLE among it; and the debug symbols of the configurations that write them.
    install(
        DIRECTORY
            "${RUNTIME_OUTPUT_DIRECTORY}/iconengines"
            "${RUNTIME_OUTPUT_DIRECTORY}/imageformats"
            "${RUNTIME_OUTPUT_DIRECTORY}/platforms"
            "${RUNTIME_OUTPUT_DIRECTORY}/styles"
            "${RUNTIME_OUTPUT_DIRECTORY}/tls"
        DESTINATION "."
        FILES_MATCHING
        PATTERN "*.dll"
    )
    install(
        DIRECTORY
            "${RUNTIME_OUTPUT_DIRECTORY}/"
        DESTINATION "."
        CONFIGURATIONS
            Debug
            RelWithDebInfo
        FILES_MATCHING
        PATTERN "*.pdb"
        PATTERN ".cmake"
            EXCLUDE
        PATTERN ".qt"
            EXCLUDE
        PATTERN "_deps"
            EXCLUDE
        PATTERN "apps"
            EXCLUDE
        PATTERN "bin"
            EXCLUDE
        PATTERN "CMakeFiles"
            EXCLUDE
        PATTERN "components"
            EXCLUDE
        PATTERN "deps"
            EXCLUDE
        PATTERN "docs"
            EXCLUDE
        PATTERN "extern"
            EXCLUDE
        PATTERN "files"
            EXCLUDE
        PATTERN "lib"
            EXCLUDE
        PATTERN "Testing"
            EXCLUDE
        PATTERN "try-compile"
            EXCLUDE
    )
else()
    install(
        FILES
            "${PROJECT_BINARY_DIR}/org.openmw.launcher.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
        COMPONENT "openmw"
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/files/launcher/images/openmw.png"
        DESTINATION "${OPENMW_ICONDIR}"
        COMPONENT "openmw"
    )
    install(
        FILES
            "${PROJECT_BINARY_DIR}/openmw.appdata.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
        COMPONENT "openmw"
    )
    if(OPENMW_BUILD_CS)
        install(
            FILES
                "${PROJECT_BINARY_DIR}/org.openmw.cs.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
            COMPONENT "opencs"
        )
        install(
            FILES
                "${PROJECT_SOURCE_DIR}/files/opencs/openmw-cs.png"
            DESTINATION "${OPENMW_ICONDIR}"
            COMPONENT "opencs"
        )
    endif()
endif()
