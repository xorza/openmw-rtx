add_component_dir (crashcatcher
    crash
    crashmonitorarguments
    crashnote
    crashpackage
    crashpage
    crashsummary
    crashuncaught
)

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
