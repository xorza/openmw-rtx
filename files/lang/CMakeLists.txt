# The Qt programs' translations: `qm-files` compiles them into the resources the launcher and the
# wizard read. `omw translations` refreshes the sources from the code, as `omw lint translations` checks them.

set(languages
    bg
    de
    en
    fr
    pl
    ru
    sv
)
foreach(group components launcher wizard)
    list(
        TRANSFORM languages
        REPLACE
            "^(.*)$"
            "${CMAKE_CURRENT_SOURCE_DIR}/${group}_\\1.ts"
        OUTPUT_VARIABLE ${group}_ts
    )
endforeach()

set(ts
    ${components_ts}
    ${launcher_ts}
    ${wizard_ts}
)
qt_add_lrelease(
    TS_FILES
        ${ts}
    QM_FILES_OUTPUT_VARIABLE qm
    OPTIONS
        -silent
    EXCLUDE_FROM_ALL
    NO_GLOBAL_TARGET
)

# Qt's own translations of the languages the programs have, beside them, from the vcpkg set's Qt, which the
# programs link in.
set(qt_translations "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
foreach(language IN LISTS languages)
    if(NOT EXISTS "${qt_translations}/qtbase_${language}.qm")
        message(FATAL_ERROR "The set's Qt has no translation of its own for '${language}' (${qt_translations})")
    endif()
    list(APPEND qm "${qt_translations}/qtbase_${language}.qm")
endforeach()

set(destination "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/resources/translations")
add_custom_target(
    qm-files
    COMMAND
        ${CMAKE_COMMAND} -E make_directory "${destination}"
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different ${qm} "${destination}"
    DEPENDS
        ${qm}
    COMMENT "Copy *.qm files to resources folder"
)
