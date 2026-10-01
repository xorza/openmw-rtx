#include "renderer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <osg/Camera>
#include <osg/FrameStamp>
#include <osg/Group>
#include <osg/Stats>

#include <components/crashcatcher/crash.hpp>
#include <components/misc/frameclock.hpp>
#include <components/misc/frameratelimiter.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sdlutil/sdldisplay.hpp>
#include <components/sdlutil/sdlvideowrapper.hpp>
#include <components/settings/values.hpp>
#include <components/shader/automaps.hpp>

#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>

#include "glrenderer.hpp"

namespace MWRender
{
    Renderer::Renderer() = default;
    Renderer::~Renderer() = default;

    void Renderer::prepareResources(Resource::ResourceSystem& resources)
    {
        mResources = &resources;

        // What the content's companion maps are called and whether to look for them, which both
        // renderers read the same way: the files are the content's, whoever draws them.
        const Settings::ShadersCategory& shaders = Settings::shaders();
        resources.getSceneManager()->setAutoMaps(Shader::AutoMapRules{
            .mNormalMaps = shaders.mAutoUseObjectNormalMaps,
            .mNormalMapPattern = shaders.mNormalMapPattern,
            .mNormalHeightMapPattern = shaders.mNormalHeightMapPattern,
            .mSpecularMaps = shaders.mAutoUseObjectSpecularMaps,
            .mSpecularMapPattern = shaders.mSpecularMapPattern,
        });

        configureResources(resources);
    }

    Resource::ResourceSystem& Renderer::getResources() const
    {
        assert(mResources != nullptr && "the resource system is Engine's to hand over, and it has not yet");
        return *mResources;
    }

    const Misc::FrameClock& Renderer::getFrameClock() const
    {
        assert(mClock != nullptr && "a frame before the host's clock was handed over");
        return *mClock;
    }

    void Renderer::setFrameRateLimit(const float limit)
    {
        mFrameRateLimit = limit;
        mLimiter = Misc::makeFrameRateLimiter(limit);
    }

    std::chrono::steady_clock::duration Renderer::awaitFrame()
    {
        // Here, because every frame the game draws opens through here: the world's, the loading
        // screen's, and the nested ones a video and a blocking message box draw. A frame counted
        // anywhere else leaves one of them to read as a hang.
        Crash::heartbeat();

        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        mLimiter.limit();
        mLastHold = std::chrono::steady_clock::now() - began;
        return mLimiter.getLastFrameDuration();
    }

    void Renderer::setScreenshotWriter(SceneUtil::AsyncScreenCaptureOperation& writer)
    {
        mScreenshotWriter = &writer;
    }

    SceneUtil::AsyncScreenCaptureOperation& Renderer::getScreenshotWriter() const
    {
        assert(mScreenshotWriter != nullptr && "the screenshot writer is Engine's to hand over, and it has not yet");
        return *mScreenshotWriter;
    }

    void Renderer::adopt(osg::Camera& camera, osg::FrameStamp& frameStamp, osg::Stats& stats)
    {
        mCamera = &camera;
        mFrameStamp = &frameStamp;
        mStats = &stats;
    }

    osg::Camera& Renderer::getCamera() const
    {
        assert(mCamera != nullptr && "the camera is the renderer's to adopt, and nothing has yet");
        return *mCamera;
    }

    osg::FrameStamp& Renderer::getFrameStamp() const
    {
        assert(mFrameStamp != nullptr && "the frame stamp is the renderer's to adopt, and nothing has yet");
        return *mFrameStamp;
    }

    osg::Stats& Renderer::getStats() const
    {
        assert(mStats != nullptr && "the stats are the renderer's to adopt, and nothing has yet");
        return *mStats;
    }

    osg::Group& Renderer::getTraversalRoot() const
    {
        assert(mTraversalRoot != nullptr && "nothing is topmost until a renderer says so");
        return *mTraversalRoot;
    }

    void Renderer::renderGuiFrame()
    {
        eventTraversal();
        updateTraversal();
        renderGui();
        advance(getFrameStamp().getSimulationTime());
    }

    void Renderer::skipGuiFrame()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        advance(getFrameStamp().getSimulationTime());
    }

    void Renderer::renderLoadingFrame(const double targetFrameRate)
    {
        openNestedFrame();
        applyLoadingBudget(targetFrameRate);
        renderGuiFrame();
    }

    namespace
    {
        /// The longest a frame is allowed to stand for, whatever the wall says: a frame after a
        /// stall steps the world by this much and no more.
        constexpr std::chrono::steady_clock::duration sLongestFrame = std::chrono::milliseconds(200);

        double secondsStood(const std::chrono::steady_clock::duration stood)
        {
            return std::chrono::duration_cast<std::chrono::duration<double>>(std::min(stood, sLongestFrame)).count();
        }
    }

    double Renderer::openFrame()
    {
        assert(mClock != nullptr && "a frame before the host's clock was handed over");

        mClock->advance(secondsStood(awaitFrame()));
        return mClock->getStep();
    }

    float Renderer::openNestedFrame()
    {
        assert(mClock != nullptr && "a frame before the host's clock was handed over");

        const std::chrono::steady_clock::duration stood = awaitFrame();

        // **A clock that states its step counts the loop's frames and no others.** How many
        // loading frames a run draws is the wall's answer, and a clock stepped by them would age
        // the resource caches differently in two runs of one build — the reference time `repeat`
        // once found doing exactly that. Such a frame stands for nothing of the world's.
        if (mClock->getStatedStep().has_value())
        {
            mClock->hold();
            return 0.0f;
        }

        mClock->advance(secondsStood(stood));
        return static_cast<float>(mClock->getStep());
    }

    void Renderer::resolutionChanged()
    {
        presentIn(mPresentation.mDrawable);
    }

    void Renderer::presentIn(const osg::Vec2i& drawable)
    {
        const osg::Vec2i asked
            = mNative ? osg::Vec2i() : osg::Vec2i(Settings::video().mResolutionX, Settings::video().mResolutionY);
        const Misc::Presentation presentation = Misc::present(asked, drawable);
        if (presentation == mPresentation)
            return;

        mPresentation = presentation;
        applyPresentation();
    }

    void Renderer::setViewMask(const unsigned int mask)
    {
        mViewMask = mask;
        applyViewMask();
    }

    void Renderer::showWorld(const bool shown)
    {
        // Asked every frame by the window manager, and answered on the change alone.
        if (mWorldShown == shown)
            return;

        mWorldShown = shown;
        applyWorldShown();
    }

    bool Renderer::toggleRenderMode(const RenderMode mode)
    {
        if (mode != Render_Scene)
            return toggleOwnRenderMode(mode);

        mWorldToggled = !mWorldToggled;
        applyWorldShown();
        return mWorldToggled;
    }

    void Renderer::setTraversalRoot(osg::Group& root)
    {
        mTraversalRoot = &root;
        adoptTraversalRoot(root);
    }

    std::unique_ptr<Renderer> createRenderer(std::string_view name, const RendererSpec& spec)
    {
        if (name == "opengl")
            return std::make_unique<GlRenderer>(spec);

        if (name == "raytrace")
            return std::make_unique<RtxRenderer>(spec);

        // **Named rather than fallen back from.** A renderer that quietly became a different one
        // answers "why does it look like that" with silence, and a name no renderer has is a
        // configuration mistake rather than a runtime condition.
        throw std::runtime_error("there is no renderer named \"" + std::string(name) + '"');
    }

    SDL_Window* openWindow(const SDL_WindowFlags surfaceFlag)
    {
        // Read inside `SDL_CreateWindow`.
        SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, Settings::video().mMinimizeOnFocusLoss ? "1" : "0");

        const SDL_DisplayID display = SDLUtil::displayAt(Settings::video().mScreen);
        const int width = Settings::video().mWindowWidth;
        const int height = Settings::video().mWindowHeight;
        const bool border = Settings::video().mWindowBorder;
        SDL_WindowFlags flags = surfaceFlag | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (!border)
            flags |= SDL_WINDOW_BORDERLESS;

        const SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "OpenMW");
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
        SDL_Window* window = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);

        if (window != nullptr)
        {
            SDLUtil::setVideoMode(window, width, height, Settings::video().mWindowMode, border);
            // So the size a renderer reads next is the one just asked for: a window system applies a
            // size or a mode when it gets round to it.
            SDL_SyncWindow(window);
        }
        return window;
    }
}
