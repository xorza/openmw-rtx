add_library(osg-ffmpeg-videoplayer STATIC)
target_sources(
    osg-ffmpeg-videoplayer
    PRIVATE
        videoplayer.cpp
        videostate.cpp
        audiodecoder.cpp
        include/osg-ffmpeg-videoplayer/audiodecoder.hpp
        include/osg-ffmpeg-videoplayer/audiofactory.hpp
        include/osg-ffmpeg-videoplayer/libavformatdefines.hpp
        include/osg-ffmpeg-videoplayer/libavutildefines.hpp
        include/osg-ffmpeg-videoplayer/videodefs.hpp
        include/osg-ffmpeg-videoplayer/videoplayer.hpp
        include/osg-ffmpeg-videoplayer/videostate.hpp
)
target_include_directories(osg-ffmpeg-videoplayer SYSTEM PUBLIC include)
target_include_directories(osg-ffmpeg-videoplayer PRIVATE include/osg-ffmpeg-videoplayer)
target_link_libraries(
    osg-ffmpeg-videoplayer
    PUBLIC
        FFmpeg::FFmpeg
        OpenSceneGraph::osg
)
