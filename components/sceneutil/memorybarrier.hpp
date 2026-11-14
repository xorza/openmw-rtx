#pragma once

#include <osg/Drawable>

namespace SceneUtil
{
    struct MemoryBarrier : public osg::Drawable::DrawCallback
    {
        explicit MemoryBarrier(GLbitfield barriers)
            : mBarriers(barriers)
        {
        }

        void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* drawable) const override
        {
            drawable->drawImplementation(renderInfo);
            renderInfo.getState()->get<osg::GLExtensions>()->glMemoryBarrier(mBarriers);
        }
        GLbitfield mBarriers;
    };
}
