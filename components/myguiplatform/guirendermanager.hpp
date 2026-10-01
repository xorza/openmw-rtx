#pragma once

#include <cassert>
#include <memory>

#include <MyGUI_RenderManager.h>

namespace osg
{
    class Texture2D;
}

namespace SceneUtil
{
    class PaintedTexture;
}

namespace MyGUIPlatform
{

    /// MyGUI's render manager, plus the calls MyGUI does not declare and every backend needs.
    ///
    /// **Neutral, despite where it lives**: this is MyGUI's own interface with two lifetime hooks
    /// on it, the scale its view is laid out at, and one route for a picture, and it has no idea
    /// what draws. It is here and not beside the ray tracer because upstream's `Platform` owns it
    /// and calls the two hooks, and that is the whole reason: the log and the data manager beside it
    /// are the same either way, and it is only the render manager that is anybody's. A MyGUI type that names a backend
    /// — the layer that draws additively — is the backend's own, registered through `registerFactories`.
    class GuiRenderManager : public MyGUI::RenderManager
    {
    public:
        /// @param scalingFactor how many device pixels a GUI pixel is worth.
        explicit GuiRenderManager(float scalingFactor)
            : mScalingFactor(scalingFactor)
        {
            assert(scalingFactor > 0.f);
        }

        /// Called once, after MyGUI's log manager exists, because this logs.
        virtual void initialise() = 0;

        /// The view laid out anew at `width` × `height` device pixels, `scalingFactor` of them to a
        /// GUI pixel: one call, so no view is ever sized at one scale and drawn at another.
        void resizeView(int width, int height, float scalingFactor)
        {
            assert(scalingFactor > 0.f);
            mScalingFactor = scalingFactor;
            setViewSize(width, height);
        }

        /// Called while whatever the backend attached itself to is still alive, which is why it is
        /// not the destructor.
        virtual void shutdown() = 0;

        /// Registers the MyGUI types that name this backend, under the type names the layouts use:
        /// the layer that draws additively, which casts the render manager to its own backend's.
        /// Called once the factory manager exists, which is after `MyGUI::Gui::initialise` and so
        /// after both hooks above.
        virtual void registerFactories() = 0;

        /// A picture the game holds as an `osg::Texture2D` over an `osg::Image`, drawn from there.
        ///
        /// **The one route from a picture in main memory to the interface.** The fog of war, the
        /// world map, a save's thumbnail and a video frame are all `osg::Image`s the game writes and
        /// then marks dirty, which is how upstream hands them to the rasterizer: it draws the texture
        /// as it stands. A backend that never opens a GL context mirrors the image instead, and reads
        /// it again whenever `osg::Image::getModifiedCount` says it changed — so a caller writes the
        /// image the way it always did and never asks which backend it got.
        ///
        /// The texture and its image belong to the caller and outlive what comes back.
        virtual std::unique_ptr<MyGUI::ITexture> shareTexture(osg::Texture2D& texture) = 0;

        /// The same, for a picture whose painter says where it painted: a backend that mirrors the
        /// image sends that rectangle instead of comparing rows for it.
        virtual std::unique_ptr<MyGUI::ITexture> shareTexture(SceneUtil::PaintedTexture& texture) = 0;

    protected:
        /// `width` × `height` device pixels in GUI pixels, which `setViewSize` makes the view.
        MyGUI::IntSize scaledView(int width, int height) const
        {
            return MyGUI::IntSize(static_cast<int>(static_cast<float>(width) / mScalingFactor),
                static_cast<int>(static_cast<float>(height) / mScalingFactor));
        }

    private:
        float mScalingFactor;
    };

    /// `shareTexture` on whichever render manager is up, for the callers that have no handle to it.
    inline std::unique_ptr<MyGUI::ITexture> shareTexture(osg::Texture2D& texture)
    {
        return static_cast<GuiRenderManager&>(MyGUI::RenderManager::getInstance()).shareTexture(texture);
    }

    inline std::unique_ptr<MyGUI::ITexture> shareTexture(SceneUtil::PaintedTexture& texture)
    {
        return static_cast<GuiRenderManager&>(MyGUI::RenderManager::getInstance()).shareTexture(texture);
    }
}
