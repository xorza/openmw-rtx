#include "glrenderer.hpp"

#include <fstream>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include <SDL.h>

#include <osg/DisplaySettings>
#include <osg/GraphicsContext>
#include <osg/Image>
#include <osg/Stats>

#include <osgDB/ReaderWriter>
#include <osgDB/Registry>

#include <osgGA/EventQueue>

#include <osgUtil/IncrementalCompileOperation>

#include <osgViewer/Renderer>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include <components/debug/debuglog.hpp>
#include <components/debug/gldebug.hpp>
#include <components/l10n/manager.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/resource/stats.hpp>
#include <components/sceneutil/color.hpp>
#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/glextensions.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sceneutil/util.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/sdlutil/imagetosurface.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>
#include <components/settings/values.hpp>
#include <components/shader/shadermanager.hpp>
#include <components/stereo/stereomanager.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/windowmanager.hpp"

#include "../../profile.hpp"

#include "../stage.hpp"
#include "screenshotmanager.hpp"

namespace
{
    void checkSDLError(int ret)
    {
        if (ret != 0)
            Log(Debug::Error) << "SDL error: " << SDL_GetError();
    }

    void initStatsHandler(Resource::Profiler& profiler)
    {
        const osg::Vec4f textColor(1.f, 1.f, 1.f, 1.f);
        const osg::Vec4f barColor(1.f, 1.f, 1.f, 1.f);
        const float multiplier = 1000;
        const bool average = true;
        const bool averageInInverseSpace = false;
        const float maxValue = 10000;

        OMW::forEachUserStatsValue([&](const OMW::UserStats& v) {
            profiler.addUserStatsLine(v.mLabel, textColor, barColor, v.mTaken, multiplier, average,
                averageInInverseSpace, v.mBegin, v.mEnd, maxValue);
        });
        // the forEachUserStatsValue loop is "run" at compile time, hence the settings manager is not available.
        // Unconditionnally add the async physics stats, and then remove it at runtime if necessary
        if (Settings::physics().mAsyncNumThreads == 0)
            profiler.removeUserStatsLine(" -Async");
    }

    struct ScreenCaptureMessageBox
    {
        void operator()(std::string filePath) const
        {
            if (filePath.empty())
            {
                MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                    "#{OMWEngine:ScreenshotFailed}", MWGui::ShowInDialogueMode_Never);

                return;
            }

            auto l10n = MWBase::Environment::get().getL10nManager()->getContext("OMWEngine");
            std::string message = l10n->formatMessage("ScreenshotMade", { "file" }, { L10n::toUnicode(filePath) });

            MWBase::Environment::get().getWindowManager()->scheduleMessageBox(
                std::move(message), MWGui::ShowInDialogueMode_Never);
        }
    };

    struct IgnoreString
    {
        void operator()(std::string) const {}
    };

    class IdentifyOpenGLOperation : public osg::GraphicsOperation
    {
    public:
        IdentifyOpenGLOperation()
            : GraphicsOperation("IdentifyOpenGLOperation", false)
        {
        }

        void operator()(osg::GraphicsContext* graphicsContext) override
        {
            Log(Debug::Info) << "OpenGL Vendor: " << glGetString(GL_VENDOR);
            Log(Debug::Info) << "OpenGL Renderer: " << glGetString(GL_RENDERER);
            Log(Debug::Info) << "OpenGL Version: " << glGetString(GL_VERSION);
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &mMaxTextureImageUnits);
        }

        int getMaxTextureImageUnits() const
        {
            if (mMaxTextureImageUnits == 0)
                throw std::logic_error("mMaxTextureImageUnits is not initialized");
            return mMaxTextureImageUnits;
        }

    private:
        int mMaxTextureImageUnits = 0;
    };
}

namespace MWRender
{
    GlRenderer::GlRenderer(const RendererSpec& spec)
        : mStage(spec.mStage)
        , mSelectDepthFormatOperation(new SceneUtil::SelectDepthFormatOperation())
        , mSelectColorFormatOperation(new SceneUtil::Color::SelectColorFormatOperation())
    {
        mViewer = new osgViewer::Viewer;
        SceneUtil::disableFFPLightModelForRenderer(
            static_cast<osgViewer::Renderer*>(mViewer->getCamera()->getRenderer()));
        mViewer->setReleaseContextAtEndOfFrameHint(false);
        mViewer->setLightingMode(osgViewer::View::NO_LIGHT);

        // Do not try to outsmart the OS thread scheduler (see bug #4785).
        mViewer->setUseConfigureAffinity(false);

        // Before the window, because `Stereo::getStereo()` is what decides whether the realize
        // operations get an initialiser and only the manager's constructor answers it.
        const bool stereoEnabled
            = Settings::stereo().mStereoEnabled || osg::DisplaySettings::instance().get()->getStereo();
        mStereoManager = std::make_unique<Stereo::Manager>(
            mViewer, stereoEnabled, Settings::camera().mNearClip, Settings::camera().mViewingDistance);

        // Taken from the viewer rather than made and handed to it: the viewer wires its update and
        // event visitors to the frame stamp at construction, and substituting objects underneath
        // without substituting those references is a bug that shows up frames later.
        mStage.adopt(*mViewer->getCamera(), *mViewer->getFrameStamp(), *mViewer->getEventQueue(),
            *mViewer->getUpdateVisitor(), *mViewer->getViewerStats());

        createWindow(spec.mResourceDir);

        mScreenCaptureOperation = new SceneUtil::AsyncScreenCaptureOperation(&spec.mWorkQueue,
            new SceneUtil::WriteScreenshotToFileOperation(spec.mScreenshotPath, Settings::general().mScreenshotFormat,
                Settings::general().mNotifyOnSavedScreenshot
                    ? std::function<void(std::string)>(ScreenCaptureMessageBox{})
                    : std::function<void(std::string)>(IgnoreString{})));

        mScreenCaptureHandler = new osgViewer::ScreenCaptureHandler(mScreenCaptureOperation);
        mViewer->addEventHandler(mScreenCaptureHandler);

        mScreenshotManager = std::make_unique<ScreenshotManager>(*this, mStage);
    }

    GlRenderer::~GlRenderer()
    {
        if (mScreenCaptureOperation != nullptr)
            mScreenCaptureOperation->stop();

        mScreenshotManager.reset();
        mStereoManager.reset();
        mViewer = nullptr;

        // `SDL_GL_DeleteContext` on a window that has already gone is undefined, and the graphics
        // window would otherwise be torn down whenever the stage lets the camera go.
        if (mGraphicsWindow != nullptr)
            mGraphicsWindow->close();
        mGraphicsWindow = nullptr;

        if (mWindow != nullptr)
            SDL_DestroyWindow(mWindow);
    }

    void GlRenderer::createWindow(const std::filesystem::path& resourceDir)
    {
        const int screen = Settings::video().mScreen;
        const int width = Settings::video().mResolutionX;
        const int height = Settings::video().mResolutionY;
        const Settings::WindowMode windowMode = Settings::video().mWindowMode;
        const bool windowBorder = Settings::video().mWindowBorder;
        const SDLUtil::VSyncMode vsync = Settings::video().mVsyncMode;
        unsigned antialiasing = static_cast<unsigned>(Settings::video().mAntialiasing);

        int posX = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);
        int posY = SDL_WINDOWPOS_CENTERED_DISPLAY(screen);

        if (windowMode == Settings::WindowMode::Fullscreen || windowMode == Settings::WindowMode::WindowedFullscreen)
        {
            posX = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
            posY = SDL_WINDOWPOS_UNDEFINED_DISPLAY(screen);
        }

        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
        if (windowMode == Settings::WindowMode::Fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;
        else if (windowMode == Settings::WindowMode::WindowedFullscreen)
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        // Allows for Windows snapping features to properly work in borderless window
        SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "1");
        SDL_SetHint("SDL_BORDERLESS_RESIZABLE_STYLE", "1");

        if (!windowBorder)
            flags |= SDL_WINDOW_BORDERLESS;

        SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, Settings::video().mMinimizeOnFocusLoss ? "1" : "0");

        checkSDLError(SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0));
        checkSDLError(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24));
        if (Debug::shouldDebugOpenGL())
            checkSDLError(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG));

        if (antialiasing > 0)
        {
            checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1));
            checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
        }

        osg::ref_ptr<SDLUtil::GraphicsWindowSDL2>& graphicsWindow = mGraphicsWindow;
        while (!graphicsWindow || !graphicsWindow->valid())
        {
            while (!mWindow)
            {
                mWindow = SDL_CreateWindow("OpenMW", posX, posY, width, height, flags);
                if (!mWindow)
                {
                    // Try with a lower AA
                    if (antialiasing > 0)
                    {
                        Log(Debug::Warning) << "Warning: " << antialiasing << "x antialiasing not supported, trying "
                                            << antialiasing / 2;
                        antialiasing /= 2;
                        Settings::video().mAntialiasing.set(antialiasing);
                        checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
                        continue;
                    }
                    else
                    {
                        std::stringstream error;
                        error << "Failed to create SDL window: " << SDL_GetError();
                        throw std::runtime_error(error.str());
                    }
                }
            }

            // Since we use physical resolution internally, we have to create the window with scaled resolution,
            // but we can't get the scale before the window exists, so instead we have to resize aftewards.
            int w, h;
            SDL_GetWindowSize(mWindow, &w, &h);
            int dw, dh;
            SDL_GL_GetDrawableSize(mWindow, &dw, &dh);
            if (dw != w || dh != h)
            {
                SDL_SetWindowSize(mWindow, width / (dw / w), height / (dh / h));
            }

            setWindowIcon(resourceDir);

            osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
            SDL_GetWindowPosition(mWindow, &traits->x, &traits->y);
            SDL_GL_GetDrawableSize(mWindow, &traits->width, &traits->height);
            traits->windowName = SDL_GetWindowTitle(mWindow);
            traits->windowDecoration = !(SDL_GetWindowFlags(mWindow) & SDL_WINDOW_BORDERLESS);
            traits->screenNum = SDL_GetWindowDisplayIndex(mWindow);
            traits->vsync = 0;
            traits->inheritedWindowData = new SDLUtil::GraphicsWindowSDL2::WindowData(mWindow);

            graphicsWindow = new SDLUtil::GraphicsWindowSDL2(traits, vsync);
            if (!graphicsWindow->valid())
                throw std::runtime_error("Failed to create GraphicsContext");

            if (traits->samples < antialiasing)
            {
                Log(Debug::Warning) << "Warning: Framebuffer MSAA level is only " << traits->samples << "x instead of "
                                    << antialiasing << "x. Trying " << antialiasing / 2 << "x instead.";
                graphicsWindow->closeImplementation();
                SDL_DestroyWindow(mWindow);
                mWindow = nullptr;
                antialiasing /= 2;
                Settings::video().mAntialiasing.set(antialiasing);
                checkSDLError(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, antialiasing));
                continue;
            }

            if (traits->red < 8)
                Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->red << " bit red channel.";
            if (traits->green < 8)
                Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->green << " bit green channel.";
            if (traits->blue < 8)
                Log(Debug::Warning) << "Warning: Framebuffer only has a " << traits->blue << " bit blue channel.";
            if (traits->depth < 24)
                Log(Debug::Warning) << "Warning: Framebuffer only has " << traits->depth << " bits of depth precision.";

            traits->alpha = 0; // set to 0 to stop ScreenCaptureHandler reading the alpha channel
        }

        osg::Camera& camera = mStage.getCamera();
        camera.setGraphicsContext(graphicsWindow);
        camera.setViewport(0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);

        osg::ref_ptr<SceneUtil::OperationSequence> realizeOperations = new SceneUtil::OperationSequence(false);
        mViewer->setRealizeOperation(realizeOperations);
        osg::ref_ptr<IdentifyOpenGLOperation> identifyOp = new IdentifyOpenGLOperation();
        realizeOperations->add(identifyOp);
        realizeOperations->add(new SceneUtil::GetGLExtensionsOperation());

        if (Debug::shouldDebugOpenGL())
            realizeOperations->add(new Debug::EnableGLDebugOperation());

        realizeOperations->add(mSelectDepthFormatOperation);
        realizeOperations->add(mSelectColorFormatOperation);

        if (Stereo::getStereo())
        {
            Stereo::Settings settings;

            settings.mMultiview = Settings::stereo().mMultiview;
            settings.mAllowDisplayListsForMultiview = Settings::stereo().mAllowDisplayListsForMultiview;
            settings.mSharedShadowMaps = Settings::stereo().mSharedShadowMaps;

            if (Settings::stereo().mUseCustomView)
            {
                const osg::Vec3 leftEyeOffset(Settings::stereoView().mLeftEyeOffsetX,
                    Settings::stereoView().mLeftEyeOffsetY, Settings::stereoView().mLeftEyeOffsetZ);

                const osg::Quat leftEyeOrientation(Settings::stereoView().mLeftEyeOrientationX,
                    Settings::stereoView().mLeftEyeOrientationY, Settings::stereoView().mLeftEyeOrientationZ,
                    Settings::stereoView().mLeftEyeOrientationW);

                const osg::Vec3 rightEyeOffset(Settings::stereoView().mRightEyeOffsetX,
                    Settings::stereoView().mRightEyeOffsetY, Settings::stereoView().mRightEyeOffsetZ);

                const osg::Quat rightEyeOrientation(Settings::stereoView().mRightEyeOrientationX,
                    Settings::stereoView().mRightEyeOrientationY, Settings::stereoView().mRightEyeOrientationZ,
                    Settings::stereoView().mRightEyeOrientationW);

                settings.mCustomView = Stereo::CustomView{
                    .mLeft = Stereo::View{
                        .pose = Stereo::Pose{
                            .position = leftEyeOffset,
                            .orientation = leftEyeOrientation,
                        },
                        .fov = Stereo::FieldOfView{
                            .angleLeft = Settings::stereoView().mLeftEyeFovLeft,
                            .angleRight = Settings::stereoView().mLeftEyeFovRight,
                            .angleUp = Settings::stereoView().mLeftEyeFovUp,
                            .angleDown = Settings::stereoView().mLeftEyeFovDown,
                        },
                    },
                    .mRight = Stereo::View{
                        .pose = Stereo::Pose{
                            .position = rightEyeOffset,
                            .orientation = rightEyeOrientation,
                        },
                        .fov = Stereo::FieldOfView{
                            .angleLeft = Settings::stereoView().mRightEyeFovLeft,
                            .angleRight = Settings::stereoView().mRightEyeFovRight,
                            .angleUp = Settings::stereoView().mRightEyeFovUp,
                            .angleDown = Settings::stereoView().mRightEyeFovDown,
                        },
                    },
                };
            }

            if (Settings::stereo().mUseCustomEyeResolution)
                settings.mEyeResolution
                    = osg::Vec2i(Settings::stereoView().mEyeResolutionX, Settings::stereoView().mEyeResolutionY);

            realizeOperations->add(new Stereo::InitializeStereoOperation(settings));
        }

        mViewer->realize();
        mCapabilities.mTextureUnits = identifyOp->getMaxTextureImageUnits();

        mStage.getEvents().getCurrentEventState()->setWindowRectangle(
            0, 0, graphicsWindow->getTraits()->width, graphicsWindow->getTraits()->height);
    }
    void GlRenderer::setWindowIcon(const std::filesystem::path& resourceDir)
    {
        std::ifstream windowIconStream;
        const auto windowIcon = resourceDir / "openmw.png";
        windowIconStream.open(windowIcon, std::ios_base::in | std::ios_base::binary);
        if (windowIconStream.fail())
            Log(Debug::Error) << "Error: Failed to open " << windowIcon;
        osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (!reader)
        {
            Log(Debug::Error) << "Error: Failed to read window icon, no png readerwriter found";
            return;
        }
        osgDB::ReaderWriter::ReadResult result = reader->readImage(windowIconStream);
        if (!result.success())
            Log(Debug::Error) << "Error: Failed to read " << windowIcon << ": " << result.message() << " code "
                              << result.status();
        else
        {
            osg::ref_ptr<osg::Image> image = result.getImage();
            auto surface = SDLUtil::imageToSurface(image, true);
            SDL_SetWindowIcon(mWindow, surface.get());
        }
    }
    void GlRenderer::setSceneRoot(osg::Group& root)
    {
        mStage.setSceneRoot(root);
        mViewer->setSceneData(&root);
    }

    void GlRenderer::advance(double simulationTime)
    {
        mViewer->advance(simulationTime);
    }

    void GlRenderer::eventTraversal()
    {
        mViewer->eventTraversal();
    }

    void GlRenderer::updateTraversal()
    {
        mViewer->updateTraversal();
    }

    void GlRenderer::renderFrame()
    {
        mViewer->renderingTraversals();
    }

    bool GlRenderer::done() const
    {
        return mViewer->done();
    }

    void GlRenderer::capture(osg::Image& image, int width, int height)
    {
        mScreenshotManager->screenshot(&image, width, height);
    }

    void GlRenderer::saveScreenshot()
    {
        mScreenCaptureHandler->setFramesToCapture(1);
        mScreenCaptureHandler->captureNextFrame(*mViewer);
    }

    void GlRenderer::suspendDraw()
    {
        mViewer->stopThreading();
    }

    void GlRenderer::resumeDraw()
    {
        mViewer->startThreading();
    }

    osgUtil::IncrementalCompileOperation* GlRenderer::getCompileOperation() const
    {
        return mViewer->getIncrementalCompileOperation();
    }

    void GlRenderer::setCompileOperation(osgUtil::IncrementalCompileOperation* operation)
    {
        mViewer->setIncrementalCompileOperation(operation);
    }

    void GlRenderer::setVSync(SDLUtil::VSyncMode mode)
    {
        osgViewer::Viewer::Windows windows;
        mViewer->getWindows(windows);
        mViewer->stopThreading();
        for (osgViewer::GraphicsWindow* window : windows)
        {
            if (auto* sdl2Window = dynamic_cast<SDLUtil::GraphicsWindowSDL2*>(window))
                sdl2Window->setSyncToVBlank(mode);
            else
                window->setSyncToVBlank(mode != SDLUtil::VSyncMode::Disabled);
        }
        mViewer->startThreading();
    }

    void GlRenderer::reloadChangedShaders(Shader::ShaderManager& shaders)
    {
        shaders.update(*mViewer);
    }

    std::unique_ptr<MyGUIPlatform::Platform> GlRenderer::createGuiPlatform(osg::Group& guiRoot,
        Resource::ImageManager& images, const VFS::Manager& vfs, float scalingFactor,
        VFS::Path::NormalizedView resourcePath, const std::filesystem::path& logPath)
    {
        return std::make_unique<MyGUIPlatform::Platform>(
            mViewer, &guiRoot, &images, &vfs, scalingFactor, resourcePath, logPath);
    }

    osg::Timer_t GlRenderer::getStartTick() const
    {
        return mViewer->getStartTick();
    }

    void GlRenderer::installStatsOverlay(const VFS::Manager& vfs, bool toFile)
    {
        osg::ref_ptr<Resource::Profiler> profiler = new Resource::Profiler(toFile, vfs);
        initStatsHandler(*profiler);
        mViewer->addEventHandler(profiler);

        mViewer->addEventHandler(new Resource::StatsHandler(toFile, vfs));

        if (toFile)
            Resource::collectStatistics(*mViewer);
    }

    void GlRenderer::reportStats(unsigned frameNumber, std::ostream& stream) const
    {
        mViewer->getViewerStats()->report(stream, frameNumber);
        osgViewer::Viewer::Cameras cameras;
        mViewer->getCameras(cameras);
        for (osg::Camera* camera : cameras)
            camera->getStats()->report(stream, frameNumber);
    }

    std::unique_ptr<Renderer> createRenderer(std::string_view name, const RendererSpec& spec)
    {
        if (name == "opengl")
            return std::make_unique<GlRenderer>(spec);

        throw std::runtime_error("No renderer named \"" + std::string(name) + "\"");
    }
}
