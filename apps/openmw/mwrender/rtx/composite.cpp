#include "composite.hpp"

#include <unistd.h>

#include <osg/RenderInfo>
#include <osg/Viewport>

#include <components/debug/debuglog.hpp>
#include <components/rtxgl/importedframe.hpp>

namespace MWRender::Rtx
{
    Composite::Composite()
    {
        // Every frame, whatever the camera is looking at: this is a full-screen blit and there is no
        // bounding volume that would ever cull it correctly.
        setCullingActive(false);
        setUseDisplayList(false);

        // **Past every scene bin and before the POST_RENDER GUI camera.** The world underneath is
        // about to be covered, and the GUI on top is drawn by MyGUI in a camera of its own.
        getOrCreateStateSet()->setRenderBinDetails(100, "RenderBin", osg::StateSet::USE_RENDERBIN_DETAILS);
    }

    Composite::Composite(const Composite& other, const osg::CopyOp& copy)
        : osg::Drawable(other, copy)
    {
    }

    Composite::~Composite()
    {
        // Only the descriptor, and only where nothing imported it. Once OpenGL has, the descriptor
        // is OpenGL's and closing it here would be closing something else's.
        //
        // **The texture is deliberately not touched.** There is no context current on whatever
        // thread destroys this, and `releaseGLObjects` is where OSG offers one.
        if (mPending.mMemory >= 0)
            ::close(mPending.mMemory);

        // Leaked rather than deleted where OSG never called `releaseGLObjects` — which only happens
        // when the context has already gone, and a texture in a dead context is nobody's.
        static_cast<void>(mFrame.release());
    }

    void Composite::take(int memory, std::uint64_t bytes, std::uint32_t width, std::uint32_t height)
    {
        const std::lock_guard<std::mutex> lock(mMutex);

        // One that was handed over and never picked up. The draw thread may not have run since.
        if (mPending.mMemory >= 0)
            ::close(mPending.mMemory);

        mPending = Pending{ .mMemory = memory, .mBytes = bytes, .mWidth = width, .mHeight = height };
    }

    std::string Composite::getObstacle() const
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        return mObstacle;
    }

    void Composite::releaseGLObjects(osg::State* state) const
    {
        mFrame.reset();
        osg::Drawable::releaseGLObjects(state);
    }

    void Composite::drawImplementation(osg::RenderInfo& renderInfo) const
    {
        Pending taken;
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            taken = mPending;
            mPending = Pending{};
        }

        if (taken.mMemory >= 0)
        {
            // Here and not in `take`, because here there is a context to destroy it in.
            mFrame.reset();

            try
            {
                mFrame
                    = std::make_unique<RtxGl::ImportedFrame>(taken.mMemory, taken.mBytes, taken.mWidth, taken.mHeight);

                const std::lock_guard<std::mutex> lock(mMutex);
                mObstacle.clear();
            }
            catch (const std::exception& failed)
            {
                const std::lock_guard<std::mutex> lock(mMutex);
                mObstacle = failed.what();

                // Reported once per import and not per frame: a message every frame would be the
                // log and nothing else.
                Log(Debug::Error) << "Ray tracing cannot reach the screen: " << mObstacle;
            }
        }

        if (mFrame == nullptr)
            return;

        const osg::Viewport* viewport = renderInfo.getCurrentCamera()->getViewport();
        if (viewport == nullptr)
            return;

        mFrame->blit(static_cast<std::uint32_t>(viewport->width()), static_cast<std::uint32_t>(viewport->height()));
    }
}
