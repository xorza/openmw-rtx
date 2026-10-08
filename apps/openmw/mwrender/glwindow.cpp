#include "glwindow.hpp"

#include <sstream>
#include <stdexcept>

#include <SDL3/SDL_error.h>
#include <osg/GraphicsContext>

#include <components/debug/debuglog.hpp>
#include <components/debug/gldebug.hpp>
#include <components/settings/values.hpp>

#include "renderer.hpp"

namespace MWRender
{
    namespace
    {
        void checkSDLError(bool succeeded)
        {
            if (!succeeded)
                Log(Debug::Error) << "SDL error: " << SDL_GetError();
        }
    }

    GlWindow::GlWindow(const SDLUtil::VSyncMode vsync)
    {
        unsigned antialiasing = static_cast<unsigned>(Settings::video().mAntialiasing);

        // Read when `openWindow` makes the window, so set before it.
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

        osg::ref_ptr<SDLUtil::GraphicsWindowSDL>& graphicsWindow = mGraphics;
        while (!graphicsWindow || !graphicsWindow->valid())
        {
            while (!mWindow)
            {
                mWindow.reset(openWindow(SDL_WINDOW_OPENGL));
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

            osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
            SDL_GetWindowPosition(mWindow.get(), &traits->x, &traits->y);
            SDL_GetWindowSizeInPixels(mWindow.get(), &traits->width, &traits->height);
            traits->windowName = SDL_GetWindowTitle(mWindow.get());
            traits->windowDecoration = !(SDL_GetWindowFlags(mWindow.get()) & SDL_WINDOW_BORDERLESS);
            traits->screenNum = SDL_GetDisplayForWindow(mWindow.get());
            traits->vsync = 0;
            traits->inheritedWindowData = new SDLUtil::GraphicsWindowSDL::WindowData(mWindow.get());

            graphicsWindow = new SDLUtil::GraphicsWindowSDL(traits, vsync);
            if (!graphicsWindow->valid())
                throw std::runtime_error("Failed to create GraphicsContext");

            if (traits->samples < antialiasing)
            {
                Log(Debug::Warning) << "Warning: Framebuffer MSAA level is only " << traits->samples << "x instead of "
                                    << antialiasing << "x. Trying " << antialiasing / 2 << "x instead.";
                graphicsWindow->closeImplementation();
                mWindow.reset();
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
    }

    GlWindow::~GlWindow()
    {
        // `SDL_GL_DestroyContext` on a window that has already gone is undefined, and the graphics
        // window would otherwise be torn down whenever the renderer's base lets the camera go.
        if (mGraphics != nullptr)
            mGraphics->close();
    }
}
