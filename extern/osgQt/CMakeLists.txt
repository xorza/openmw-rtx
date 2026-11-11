add_library(osgQt STATIC)
target_sources(
    osgQt
    PRIVATE
        osgQOpenGLWidget.cpp
        CompositeOsgRenderer.cpp
        include/osgQt/osgQOpenGLWidget.hpp
        include/osgQt/CompositeOsgRenderer.hpp
)
target_include_directories(osgQt SYSTEM PUBLIC include)
target_include_directories(osgQt PRIVATE include/osgQt)
target_link_libraries(
    osgQt
    PUBLIC
        Qt::Core
        Qt::OpenGL
        Qt::OpenGLWidgets
        OpenSceneGraph::osgViewer
)
set_target_properties(
    osgQt
    PROPERTIES
        AUTOMOC
            ON
)
