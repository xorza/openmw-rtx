add_library(oics STATIC)
target_sources(
    oics
    PRIVATE
        ICSChannel.cpp
        ICSControl.cpp
        ICSInputControlSystem.cpp
        ICSInputControlSystem_keyboard.cpp
        ICSInputControlSystem_mouse.cpp
        ICSInputControlSystem_joystick.cpp
        include/oics/ICSChannel.h
        include/oics/ICSChannelListener.h
        include/oics/ICSControl.h
        include/oics/ICSControlListener.h
        include/oics/ICSInputControlSystem.h
        include/oics/ICSPrerequisites.h
)
target_include_directories(oics SYSTEM PUBLIC include)
target_include_directories(oics PRIVATE include/oics)

add_library(local_tinyxml STATIC)
target_sources(
    local_tinyxml
    PRIVATE
        tinyxml/tinyxml.cpp
        tinyxml/tinyxmlparser.cpp
        tinyxml/tinyxmlerror.cpp
        tinyxml/tinystr.cpp
)
target_include_directories(local_tinyxml SYSTEM PUBLIC tinyxml)

target_link_libraries(
    oics
    PUBLIC
        local_tinyxml
        SDL3::SDL3
)
