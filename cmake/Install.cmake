# What an install carries beside the programs, which each app's own `install(TARGETS)` names: the
# desktop files, the documents and licences, and the game's defaults and resources. `omw archive` and CI install through it; no installer is
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
install(DIRECTORY "${PROJECT_SOURCE_DIR}/files/licenses/crashpad/" DESTINATION "${licenses}/crashpad")

# The vcpkg set's, each port's own `copyright` as vcpkg installs it, under the port's name: every library the
# programs link statically (`vcpkg.json`), whose licences ask for their notices in every copy, and the LGPL
# ones (Qt, FFmpeg, OpenAL Soft) for the source too, which the tree's pins and overlays name. Every port's,
# the host tools' among them, rather than a list that drifts from the set.
file(GLOB vcpkg_copyrights "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
if(NOT vcpkg_copyrights)
    message(FATAL_ERROR "The vcpkg set at ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET} holds no licence")
endif()
foreach(copyright IN LISTS vcpkg_copyrights)
    cmake_path(GET copyright PARENT_PATH port)
    cmake_path(GET port FILENAME port)
    install(FILES "${copyright}" DESTINATION "${licenses}/vcpkg" RENAME "${port}.txt")
endforeach()

install(FILES "${PROJECT_SOURCE_DIR}/CHANGELOG.md" TYPE DOC)
install(FILES "${PROJECT_SOURCE_DIR}/README.md" TYPE DOC)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" TYPE DOC RENAME "LICENSE.txt")
install(FILES "${PROJECT_SOURCE_DIR}/AUTHORS.md" TYPE DOC)

install(
    FILES
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/defaults.bin"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/gamecontrollerdb.txt"
    TYPE SYSCONF
)
install(FILES "${PROJECT_SOURCE_DIR}/files/openmw.cfg.local" TYPE SYSCONF RENAME "openmw.cfg")
install(DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources" TYPE DATA)

if(MSVC)
    # Each program's debug symbols, in the configurations that write them: its own PDB, and not every binary's
    # in the folder the programs share with the tests and the harness. The programs are the ones the apps'
    # `install(TARGETS)` name.
    foreach(
        program
        IN
        ITEMS
            openmw
            openmw-launcher
            openmw-wizard
            openmw-cs
            openmw-bulletobjecttool
            openmw-essimporter
            openmw-iniimporter
            openmw-navmeshtool
            bsatool
            esmtool
            niftest
    )
        install(
            FILES
                "$<TARGET_PDB_FILE:${program}>"
            TYPE BIN
            CONFIGURATIONS
                Debug
                RelWithDebInfo
        )
    endforeach()
endif()
if(NOT WIN32)
    install(
        FILES
            "${PROJECT_BINARY_DIR}/org.openmw.launcher.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/files/launcher/images/openmw.png"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps"
    )
    install(FILES "${PROJECT_BINARY_DIR}/openmw.appdata.xml" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo")
    install(FILES "${PROJECT_BINARY_DIR}/org.openmw.cs.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES "${PROJECT_SOURCE_DIR}/files/opencs/openmw-cs.png" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps")
endif()
