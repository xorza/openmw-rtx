#ifndef GAME_RENDER_RTX_COMPOSITE_H
#define GAME_RENDER_RTX_COMPOSITE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <osg/Drawable>

namespace RtxGl
{
    class ImportedFrame;
}

namespace MWRender::Rtx
{
    /// The traced frame, drawn into the game's own framebuffer.
    ///
    /// **Where the two APIs meet.** Vulkan renders the world offscreen and exports the allocation;
    /// this imports it as a texture and blits it in. It goes on the post-processor's HUD camera with
    /// a render bin past every scene bin, which puts it over the world and under MyGUI — the same
    /// slot the screenshot code uses, for the same reason.
    ///
    /// **Two threads touch this and only one of them may speak to OpenGL.** The game traces on the
    /// main thread and OSG draws on its own, so every GL object here is created and destroyed inside
    /// `drawImplementation` and nowhere else. The first version of this dropped the texture from
    /// `take` — on the main thread, with no context current — and the window went black and then
    /// stopped answering.
    class Composite : public osg::Drawable
    {
    public:
        Composite();
        ~Composite() override;

        Composite(const Composite& other, const osg::CopyOp& copy);
        META_Object(MWRender, Composite)

        /// Hands over a frame to import, replacing whatever was there. Any thread.
        ///
        /// @param memory a descriptor from the renderer's export. **Ownership passes here**, and on
        ///        to OpenGL, which closes it.
        ///
        /// Called once per resize rather than per frame: the allocation does not move while the
        /// frame stays the same size, and re-importing one that has not is a texture rebuilt for
        /// nothing. Nothing is imported here — the draw thread does that when it next runs.
        void take(int memory, std::uint64_t bytes, std::uint32_t width, std::uint32_t height);

        /// Why nothing is being drawn, or empty where something is.
        std::string getObstacle() const;

        void drawImplementation(osg::RenderInfo& renderInfo) const override;

        /// Drops the texture while a context is still current, which is what OSG calls this for.
        void releaseGLObjects(osg::State* state) const override;

    private:
        /// What `take` left and the draw thread has not picked up yet.
        struct Pending
        {
            int mMemory = -1;
            std::uint64_t mBytes = 0;
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
        };

        mutable std::mutex mMutex;
        mutable Pending mPending;
        mutable std::string mObstacle;

        /// **Only ever touched by the draw thread.** Mutable because `drawImplementation` is const
        /// and the import can only happen there.
        mutable std::unique_ptr<RtxGl::ImportedFrame> mFrame;
    };
}

#endif
