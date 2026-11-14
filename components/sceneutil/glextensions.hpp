#pragma once

#include <osg/GLExtensions>
#include <osg/GraphicsThread>

namespace SceneUtil
{
    bool glExtensionsReady();
    osg::GLExtensions& getGLExtensions();

    class GetGLExtensionsOperation : public osg::GraphicsOperation
    {
    public:
        GetGLExtensionsOperation();

        void operator()(osg::GraphicsContext* graphicsContext) override;
    };
}
