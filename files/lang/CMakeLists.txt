# The Qt programs' translations: `qm-files` compiles them into the resources the launcher and the
# wizard read, and `translations` and `purge-obsolete-translations` refresh the sources from the code.

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

get_target_property(lupdate Qt::lupdate IMPORTED_LOCATION)
set(components_scanned
    "${PROJECT_SOURCE_DIR}/components/contentselector"
    "${PROJECT_SOURCE_DIR}/components/process"
)
set(launcher_scanned "${PROJECT_SOURCE_DIR}/apps/launcher")
set(wizard_scanned "${PROJECT_SOURCE_DIR}/apps/wizard")
foreach(target translations purge-obsolete-translations)
    set(purge)
    if(target STREQUAL "purge-obsolete-translations")
        set(purge -no-obsolete)
    endif()
    set(commands)
    foreach(group components launcher wizard)
        list(
            APPEND commands
            COMMAND
            ${lupdate}
            -locations
            none
            ${purge}
            ${${group}_scanned}
            -ts
            ${${group}_ts}
        )
    endforeach()
    add_custom_target(${target} ${commands} VERBATIM)
endforeach()

set(ts
    ${components_ts}
    ${launcher_ts}
    ${wizard_ts}
)
qt_add_translation(qm ${ts} OPTIONS -silent)

# Qt's own translations of the languages the programs have, beside them, from the vcpkg set's Qt, which the
# programs link in.
get_target_property(qmake Qt::qmake IMPORTED_LOCATION)
execute_process(
    COMMAND
        "${qmake}" -query QT_INSTALL_TRANSLATIONS
    OUTPUT_VARIABLE qt_translations
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
foreach(language IN LISTS languages)
    if(EXISTS "${qt_translations}/qtbase_${language}.qm")
        list(APPEND qm "${qt_translations}/qtbase_${language}.qm")
    elseif(EXISTS "${qt_translations}/qt_${language}.qm")
        list(APPEND qm "${qt_translations}/qt_${language}.qm")
    else()
        message(
            FATAL_ERROR
            "Qt translations for '${language}' locale are not found in the '${qt_translations}' folder."
        )
    endif()
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
