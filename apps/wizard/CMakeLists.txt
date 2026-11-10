qt_add_executable(openmw-wizard WIN32)
target_sources(openmw-wizard
    PRIVATE
        componentselectionpage.cpp
        conclusionpage.cpp
        existinginstallationpage.cpp
        importpage.cpp
        installationtargetpage.cpp
        intropage.cpp
        languageselectionpage.cpp
        main.cpp
        mainwizard.cpp
        methodselectionpage.cpp
        "${PROJECT_SOURCE_DIR}/files/wizard/wizard.qrc"
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        componentselectionpage.hpp
        conclusionpage.hpp
        existinginstallationpage.hpp
        importpage.hpp
        installationtargetpage.hpp
        intropage.hpp
        languageselectionpage.hpp
        mainwizard.hpp
        methodselectionpage.hpp
)
if (WIN32)
    target_sources(openmw-wizard PRIVATE
        "${PROJECT_SOURCE_DIR}/files/windows/openmw-wizard.rc"
        "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest"
        "${PROJECT_SOURCE_DIR}/files/windows/QWindowsVistaDark/dark.qrc")
endif ()

# Upstream's wizard unpacks the installer's archives with libunshield on every system but Windows, whose
# players run the installer itself.
if (OPENMW_USE_UNSHIELD)
    target_sources(openmw-wizard
        PRIVATE
            installationpage.cpp
            unshield/inisettings.cpp
            unshield/unshieldworker.cpp
        PRIVATE FILE_SET HEADERS FILES
            installationpage.hpp
            unshield/inisettings.hpp
            unshield/unshieldworker.hpp
    )
    target_compile_definitions(openmw-wizard PRIVATE OPENMW_USE_UNSHIELD)
    target_include_directories(openmw-wizard SYSTEM PRIVATE ${LIBUNSHIELD_INCLUDE_DIRS})
    target_link_libraries(openmw-wizard PRIVATE ${LIBUNSHIELD_LIBRARIES})
endif ()

set_target_properties(openmw-wizard PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS ui)
add_dependencies(openmw-wizard qm-files)
target_link_libraries(openmw-wizard PRIVATE openmw-components-qt Qt::Widgets Qt::Core Qt::Svg)

install(TARGETS openmw-wizard ${runtime_dependency_arguments})
generate_deploy_script_for_qt_app(openmw-wizard)
