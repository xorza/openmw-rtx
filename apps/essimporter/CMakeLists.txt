add_executable(openmw-essimporter)
target_sources(openmw-essimporter
    PRIVATE
        convertacdt.cpp
        convertcntc.cpp
        convertcrec.cpp
        converter.cpp
        convertinventory.cpp
        convertnpcc.cpp
        convertplayer.cpp
        convertscpt.cpp
        convertscri.cpp
        importcellref.cpp
        importcntc.cpp
        importcrec.cpp
        importdial.cpp
        importer.cpp
        importgame.cpp
        importinfo.cpp
        importinventory.cpp
        importjour.cpp
        importklst.cpp
        importnpcc.cpp
        importplayer.cpp
        importproj.cpp
        importques.cpp
        importscpt.cpp
        importscri.cpp
        importsplm.cpp
        main.cpp
    PRIVATE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}" FILES
        convertacdt.hpp
        convertcntc.hpp
        convertcrec.hpp
        converter.hpp
        convertinventory.hpp
        convertnpcc.hpp
        convertplayer.hpp
        convertscpt.hpp
        convertscri.hpp
        importacdt.hpp
        importcellref.hpp
        importcntc.hpp
        importcrec.hpp
        importdial.hpp
        importer.hpp
        importercontext.hpp
        importgame.hpp
        importinfo.hpp
        importinventory.hpp
        importjour.hpp
        importklst.hpp
        importnpcc.hpp
        importplayer.hpp
        importproj.h
        importques.hpp
        importscpt.hpp
        importscri.hpp
        importsplm.h
)
if (WIN32)
    target_sources(openmw-essimporter PRIVATE "${PROJECT_SOURCE_DIR}/files/windows/other-apps.manifest")
endif ()
target_link_libraries(openmw-essimporter PRIVATE Boost::program_options openmw-components)

install(TARGETS openmw-essimporter ${runtime_dependency_arguments})

if (MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)
    target_precompile_headers(openmw-essimporter PRIVATE
        <algorithm>
        <filesystem>
        <fstream>
        <string>
        <vector>
    )
endif ()
