qt_add_executable(openmw-launcher WIN32)
target_sources(openmw-launcher
    PRIVATE
        datafilespage.cpp
        graphicspage.cpp
        importpage.cpp
        main.cpp
        maindialog.cpp
        sdlinit.cpp
        settingspage.cpp
        textslotmsgbox.cpp
        utils/cellnameloader.cpp
        utils/lineedit.cpp
        utils/openalutil.cpp
        utils/profilescombobox.cpp
        utils/textinputdialog.cpp
        "${PROJECT_SOURCE_DIR}/files/launcher/launcher.qrc"
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        datafilespage.hpp
        graphicspage.hpp
        importpage.hpp
        maindialog.hpp
        sdlinit.hpp
        settingspage.hpp
        textslotmsgbox.hpp
        utils/cellnameloader.hpp
        utils/lineedit.hpp
        utils/openalutil.hpp
        utils/profilescombobox.hpp
        utils/textinputdialog.hpp
)
if (WIN32)
    target_sources(openmw-launcher PRIVATE
        "${PROJECT_SOURCE_DIR}/files/windows/launcher.rc"
        "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest"
        "${PROJECT_SOURCE_DIR}/files/windows/QWindowsVistaDark/dark.qrc")
endif ()

set_target_properties(openmw-launcher PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    AUTOUIC_SEARCH_PATHS ui)
add_dependencies(openmw-launcher qm-files)
target_link_libraries(openmw-launcher
    PRIVATE
        SDL3::SDL3
        OpenAL::OpenAL
        openmw-components-qt
        Qt::Widgets
        Qt::Core
        Qt::Svg
        Qt::Network
)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-launcher PRIVATE
        <boost/program_options/options_description.hpp>

        <algorithm>
        <string>
        <vector>
    )
endif ()

install(TARGETS openmw-launcher ${runtime_dependency_arguments})
generate_deploy_script_for_qt_app(openmw-launcher)
