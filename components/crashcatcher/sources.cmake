add_component_dir (crashcatcher
    crash
    crashanswering
    crashinstall
    crashmonitorarguments
    crashnote
    crashpackage
    crashpage
    crashsummary
    crashuncaught
)

# The image is kept for a monitor, and a build with no Crashpad starts none.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND OPENMW_CRASHPAD)
    add_component_dir (crashcatcher crashimagelinux)
else()
    add_component_dir (crashcatcher crashimagenone)
endif()

if (OPENMW_CRASHPAD)
    add_component_dir (crashcatcher
        crashpadclient
        crashpadclientsystem
        crashpadmonitor
        crashpadmonitorsystem
    )
    if (WIN32)
        add_component_dir (crashcatcher crashpadclientwin32 crashpadmonitorwin32)
    else()
        add_component_dir (crashcatcher crashpadclientposix crashpadmonitorposix)
    endif()
else()
    add_component_dir (crashcatcher
        crashunsupported
    )
endif()
