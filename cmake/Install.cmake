# What an install carries: the programs, their licences, the configuration and the settings' defaults, the
# resources the programs read at run time, and the menu entry. Nothing else: no document for developers, no
# resource only the rasterizer or OpenMW-CS reads. `omw archive` and CI install through it; no installer is built
# from it.
#
# **Two components, the two archives a release ships** (`cmake --install --component`): `game`, what a player runs,
# and `tools`, the console programs a modder runs, each archive whole by itself. Refused: one archive, whose AppImage
# reaches no program but the launcher.

# The game's programs: the game, and what the launcher and the wizard start, the importer and the navmesh tool.
set(game_programs
    openmw
    openmw-launcher
    openmw-wizard
    openmw-iniimporter
    openmw-navmeshtool
)
# The tools: console programs, none of them Qt's, which a plain archive holds. **OpenMW-CS is built and not shipped**:
# upstream's editor, which draws through the rasterizer, and whose Qt needs the libraries only the AppImage carries.
set(tools_programs
    openmw-bulletobjecttool
    openmw-essimporter
    bsatool
    esmtool
    niftest
)
# **What an install carries, built alone**: `omw archive` builds this and not `all`, so a release links none of the
# tests, the benchmarks, the harness or the editor, each another link under link-time optimisation. The programs
# bring the rest of the install, the shaders and the translations among it, as their own dependencies.
add_custom_target(openmw-installed)
add_dependencies(
    openmw-installed
    ${game_programs}
    ${tools_programs}
)

# The licences of what the fork compiles into the binaries, beside the GPL's own text: each asks for
# its notice in every copy, binaries included, but SDL's zlib, which goes in so that no licence of
# what is compiled in is missing. `files/licenses` holds each text as its source ships it, and
# FidelityFX's stays with its headers.
set(licenses "${CMAKE_INSTALL_DOCDIR}/licenses")

# The vcpkg set's, each port's own `copyright` as vcpkg installs it, under the port's name: every library the
# programs link statically (`vcpkg.json`), whose licences ask for their notices in every copy, and the LGPL
# ones (Qt, FFmpeg, OpenAL Soft) for the source too, which the tree's pins and overlays name. Every port's,
# the host tools' among them, rather than a list that drifts from the set.
file(GLOB vcpkg_copyrights "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
if(NOT vcpkg_copyrights)
    message(FATAL_ERROR "The vcpkg set at ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET} holds no licence")
endif()

# What each archive holds of its own and of what both do: its programs, their licences, and the configuration and
# the settings' defaults the tools read too, so the tools' archive works wherever it is unpacked. No symbols: a
# release publishes them apart (`omw archive`).
foreach(component game tools)
    install(TARGETS ${${component}_programs} COMPONENT ${component})

    install(
        FILES
            "${PROJECT_SOURCE_DIR}/extern/fidelityfx/LICENSE.txt"
        DESTINATION "${licenses}"
        RENAME "FidelityFX-SDK.txt"
        COMPONENT ${component}
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/files/licenses/VulkanMemoryAllocator.txt"
            "${PROJECT_SOURCE_DIR}/files/licenses/volk.txt"
            "${PROJECT_SOURCE_DIR}/files/licenses/SDL3.txt"
            "${PROJECT_SOURCE_DIR}/files/licenses/rgbcx.txt"
            "${PROJECT_SOURCE_DIR}/files/licenses/bc7enc.txt"
            "${PROJECT_SOURCE_DIR}/files/licenses/Lucide.txt"
        DESTINATION "${licenses}"
        COMPONENT ${component}
    )
    install(
        DIRECTORY
            "${PROJECT_SOURCE_DIR}/files/licenses/crashpad/"
        DESTINATION "${licenses}/crashpad"
        COMPONENT ${component}
    )
    foreach(copyright IN LISTS vcpkg_copyrights)
        cmake_path(GET copyright PARENT_PATH port)
        cmake_path(GET port FILENAME port)
        install(FILES "${copyright}" DESTINATION "${licenses}/vcpkg" RENAME "${port}.txt" COMPONENT ${component})
    endforeach()

    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" TYPE DOC RENAME "LICENSE.txt" COMPONENT ${component})

    install(FILES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/defaults.bin" TYPE SYSCONF COMPONENT ${component})
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/files/openmw.cfg.local"
        TYPE SYSCONF
        RENAME "openmw.cfg"
        COMPONENT ${component}
    )
endforeach()

# **The resources by name, each one a program reads at run time** (`files/` stages more, for the rasterizer, the
# editor and the tests). The virtual file system without what only the rasterizer reads: its post-processing
# shaders, which no frame of the ray tracer's runs, and its water's normal map. The game's run-time folders whole,
# and of them the tools' the virtual file system's, which `openmw-bulletobjecttool` loads the content through. The
# ray tracer's shaders are compiled into the game (`Rtx::shippedVulkanShaders`). Refused: the folder whole minus
# what is known unused, which would ship whatever folder upstream adds next.
set(resources "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources")
set(vfs_folders
    "${resources}/vfs"
    "${resources}/vfs-mw"
)
set(vfs_rasterized
    REGEX
    "/vfs/shaders$"
    EXCLUDE
    REGEX
    "/vfs/textures/omw$"
    EXCLUDE
)
install(FILES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/gamecontrollerdb.txt" TYPE SYSCONF COMPONENT game)
foreach(component game tools)
    install(
        DIRECTORY
            ${vfs_folders}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/resources"
        COMPONENT ${component}
        ${vfs_rasterized}
    )
endforeach()
install(
    DIRECTORY
        "${resources}/lua_libs"
        "${resources}/translations"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/resources"
    COMPONENT game
)
install(
    FILES
        "${resources}/openmw.png"
        "${resources}/version"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/resources"
    COMPONENT game
)

if(NOT WIN32)
    install(
        FILES
            "${PROJECT_BINARY_DIR}/org.openmw.launcher.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
        COMPONENT game
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/files/launcher/images/openmw.png"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps"
        COMPONENT game
    )
endif()
