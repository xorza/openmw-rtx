#ifndef GAME_RENDER_GL_GLOFFSCREENVIEW_H
#define GAME_RENDER_GL_GLOFFSCREENVIEW_H

#include <memory>

#include <osg/ref_ptr>

#include "../offscreenview.hpp"

namespace MyGUIPlatform
{
    class OSGTexture;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class OffscreenDrawOnceCallback;
    class OffscreenRTTNode;

    /// An offscreen view as OpenSceneGraph draws one: a pre-render camera hung off the top of the
    /// graph, culled and drawn by the same traversal as everything else.
    ///
    /// **Every rasterizer decision the caller used to make for itself lives here.** The light rig,
    /// the sample count, the colour format, the depth convention, the cull mode, the loose uniforms
    /// the object shaders want and the premultiplied-alpha fixup are all worked out from
    /// `OffscreenViewSpec`, which names none of them.
    class GlOffscreenView final : public OffscreenView
    {
    public:
        /// @param parent whatever the viewer culls, which is where a pre-render camera has to sit
        ///        to be reached before the frame it feeds.
        GlOffscreenView(const OffscreenViewSpec& spec, osg::Group& parent, Resource::ResourceSystem& resources);
        ~GlOffscreenView() override;

        void setView(const osg::Matrixf& view) override;
        void setExtent(int width, int height) override;
        void sceneChanged() override;
        void redraw() override;
        bool pick(float x, float y, osg::NodePath& hit) const override;
        MyGUI::ITexture& getTexture() const override;

    private:
        osg::ref_ptr<osg::Group> mParent;
        osg::ref_ptr<OffscreenRTTNode> mNode;
        osg::ref_ptr<OffscreenDrawOnceCallback> mDrawOnce;
        osg::ref_ptr<osg::Group> mScene;
        std::unique_ptr<MyGUIPlatform::OSGTexture> mTexture;
    };
}

#endif
