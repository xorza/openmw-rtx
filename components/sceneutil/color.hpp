#pragma once

#include <cstdint>

#include <osg/GraphicsThread>

// After OpenSceneGraph's headers, which include the system's <GL/gl.h> that Khronos's extension header builds on:
// Windows' names stop at GL 1.1.
#include <GL/glext.h>

namespace SceneUtil
{
    bool isColorFormat(GLenum format);
    bool isFloatingPointColorFormat(GLenum format);
    std::int32_t getColorFormatChannelCount(GLenum format);
    void getColorFormatSourceFormatAndType(GLenum internalFormat, GLenum& sourceFormat, GLenum& sourceType);

    namespace Color
    {
        GLenum colorSourceFormat();
        GLenum colorSourceType();
        GLenum colorInternalFormat();

        class SelectColorFormatOperation final : public osg::GraphicsOperation
        {
        public:
            SelectColorFormatOperation()
                : GraphicsOperation("SelectColorFormatOperation", false)
            {
            }

            void operator()(osg::GraphicsContext* graphicsContext) override;

            void setSupportedFormats(const std::vector<GLenum>& supportedFormats)
            {
                mSupportedFormats = supportedFormats;
            }

        private:
            std::vector<GLenum> mSupportedFormats;
        };
    }
}
