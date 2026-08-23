#ifndef GAME_RENDER_RTX_TRACEDVIEW_H
#define GAME_RENDER_RTX_TRACEDVIEW_H

#include <cstdint>
#include <memory>
#include <vector>

#include <osg/Matrixf>
#include <osg/Node>
#include <osg/ref_ptr>

#include <components/rtx/renderer.hpp>

#include "../offscreenview.hpp"

namespace MyGUI
{
    class ITexture;
}

namespace osg
{
    class Image;
}

namespace Rtx
{
    class SceneDesc;
}

namespace RtxBridge
{
    class SceneExtractor;
}

namespace MWRender::Rtx
{

    class RtxRenderer;

    /// An offscreen view as a ray tracer makes one: a second camera into a texture the GUI already
    /// draws from.
    ///
    /// **Nothing is rendered to a texture and nothing is read back.** The rasterizer answers this
    /// with a pre-render camera hung off the graph and an `osg::Texture2D` under it; here the trace
    /// writes straight into the renderer's own GUI table, so the picture is never a frame, never a
    /// framebuffer and never in main memory — unless `keepCopy` asks, which the global map does and
    /// nothing else.
    ///
    /// **Two kinds, and `OffscreenViewSpec::mFromWorld` is which.** A map tile is a piece of the
    /// world and traces against the scene the renderer already holds. A doll is a group the game
    /// assembled for one picture — nothing in it stands in a cell — so it is mirrored into a scene
    /// of its own and rebuilt whenever the picture is asked for again, which is when the character
    /// put something on.
    class TracedView final : public OffscreenView
    {
    public:
        TracedView(const OffscreenViewSpec& spec, RtxRenderer& owner, ::Rtx::Renderer& renderer);
        ~TracedView() override;

        void setView(const osg::Matrixf& view) override;
        void setExtent(int width, int height) override;
        void sceneChanged() override;
        void redraw() override;
        void keepCopy() override;
        const osg::Image* getCopy() const override;
        bool pick(float x, float y, osg::NodePath& hit) const override;
        MyGUI::ITexture& getTexture() const override { return *mTexture; }

    private:
        /// The camera this picture is taken with, built from the spec and wherever `setView` last
        /// put it.
        ::Rtx::Shaders::VisibilityConstants describeCamera() const;

        /// Poses the subtree, mirrors it and hands it to the renderer. The doll's half of a redraw.
        void rebuildSubject();

        RtxRenderer& mOwner;
        ::Rtx::Renderer& mRenderer;

        /// Made through MyGUI's own factory, so which backend is behind it is not this class's
        /// business — but its slot in the renderer's table is, because that is what is traced into.
        MyGUI::ITexture* mTexture = nullptr;
        std::uint32_t mSlot = 0;

        /// What this is a picture of, where it is not the world. Held because it is walked again
        /// every time the picture is asked for.
        osg::ref_ptr<const osg::Node> mSubject;
        osg::Node::NodeMask mSubjectMask = ~0u;

        /// The mirror of it, and the slot the renderer keeps its acceleration structures in. Null
        /// and unused for a picture of the world.
        std::unique_ptr<::Rtx::SceneDesc> mScene;
        std::unique_ptr<RtxBridge::SceneExtractor> mExtractor;
        std::uint32_t mViewScene = 0;

        /// The traversal number the subject was last posed at. What an intersection test has to be
        /// told, because a skinned mesh keeps two poses and picks between them by frame.
        unsigned int mPosedFrame = 0;

        osg::ref_ptr<osg::Image> mCopy;

        /// Whether `mCopy` holds the picture the most recent `redraw()` asked for.
        ///
        /// **Because `OffscreenView::getCopy` promises null until it does**, and a black image is
        /// not a picture that has not arrived — it is a picture of nothing. The global map paints
        /// the tile it is handed and marks the cell done, so answering early paints that cell black
        /// for the rest of the session.
        bool mCopyIsCurrent = false;

        /// What a read lands in before it is handed to the copy. Kept rather than made per redraw:
        /// the local map draws a tile a cell, and a cell arriving is already the busiest frame there
        /// is.
        std::vector<std::uint8_t> mPixels;

        osg::Matrixf mView;

        /// Everything the caller described that outlives one redraw. The subtree itself is not among
        /// them: what is traced is the scene the renderer already holds.
        int mWidth = 0;
        int mHeight = 0;
        int mExtentX = 0;
        int mExtentY = 0;

        ::Rtx::GuiTraceOptions mOptions;

        bool mPerspective = true;
        float mFieldOfView = 0.f;
        float mBoxWidth = 0.f;
        float mBoxHeight = 0.f;
        float mNear = 1.f;
        float mFar = 10000.f;

        osg::Vec3f mSunDirection;
        osg::Vec3f mSunIrradiance;
        osg::Vec3f mAmbient;

        /// Whether a ray that hits nothing leaves the picture transparent. Taken from the clear
        /// colour's alpha, because that is the same statement: a picture the GUI composites over
        /// what is behind it has to say where it stops, and one that fills its widget does not.
        bool mTransparent = false;

        /// Whether the renderer holds the geometry this picture is of. False draws the clear colour.
        bool mFromWorld = false;

        bool mKeepCopy = false;
    };

}

#endif
