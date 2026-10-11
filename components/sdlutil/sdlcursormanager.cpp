#include "sdlcursormanager.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <osg/Version>
#include <osgViewer/GraphicsWindow>

#include <components/debug/debuglog.hpp>

#include "imagetosurface.hpp"
#include "sdlvideowrapper.hpp"

#if defined(OSG_LIBRARY_STATIC) && (!defined(ANDROID) || OSG_VERSION_GREATER_THAN(3, 6, 5))
// Sets the default windowing system interface according to the OS.
// Necessary for OpenSceneGraph to do some things, like decompression.
USE_GRAPHICSWINDOW()
#endif

namespace SDLUtil
{
    namespace
    {
        /// The video driver SDL runs on, or nothing before SDL's video is up.
        std::string_view currentDriver()
        {
            const char* const driver = SDL_GetCurrentVideoDriver();
            return driver != nullptr ? std::string_view(driver) : std::string_view();
        }
    }

    CursorScaling SDLCursorManager::scalingOf(const std::string_view driver)
    {
        // `X11_CreateXCursorCursor` loads the base image as it is and never asks for an alternate,
        // where every other desktop driver scales it or picks the alternate the display wants.
        return driver == "x11" ? CursorScaling::AsPixels : CursorScaling::ByDisplay;
    }

    float SDLCursorManager::baseDivisor(const CursorScaling scaling, const float displayScale)
    {
        return scaling == CursorScaling::ByDisplay ? displayScale : 1.f;
    }

    SDLCursorManager::SDLCursorManager()
        : mEnabled(false)
        , mInitialized(false)
        , mScaling(scalingOf(currentDriver()))
    {
        // So SDL draws a cursor at the display's scale and picks the image `createCursor` made for
        // it, rather than showing its pixels one to one on every display.
        SDL_SetHint(SDL_HINT_MOUSE_DPI_SCALE_CURSORS, "1");
    }

    SDLCursorManager::~SDLCursorManager()
    {
        dropCursors();
    }

    void SDLCursorManager::removeCursor(std::string_view name)
    {
        const auto found = mCursorMap.find(name);
        if (found == mCursorMap.end())
            return;

        SDL_DestroyCursor(found->second);
        mCursorMap.erase(found);
    }

    void SDLCursorManager::dropCursors()
    {
        for (const auto& [name, cursor] : mCursorMap)
            SDL_DestroyCursor(cursor);
        mCursorMap.clear();
    }

    void SDLCursorManager::setEnabled(bool enabled)
    {
        if (mInitialized && enabled == mEnabled)
            return;

        mInitialized = true;
        mEnabled = enabled;

        // turn on hardware cursors
        if (enabled)
        {
            _setGUICursor(mCurrentCursor);
        }
        // turn off hardware cursors
        else
        {
            SDL_HideCursor();
        }
    }

    void SDLCursorManager::cursorChanged(std::string_view name)
    {
        mCurrentCursor = name;
        _setGUICursor(mCurrentCursor);
    }

    void SDLCursorManager::_setGUICursor(std::string_view name)
    {
        auto it = mCursorMap.find(name);
        if (it == mCursorMap.end())
            it = mCursorMap.find("arrow");
        if (it != mCursorMap.end())
            SDL_SetCursor(it->second);
    }

    namespace
    {
        /// What SDL said of the call that failed, after `what`, as `createCursor` reports it.
        [[noreturn]] void fail(const char* what)
        {
            throw std::runtime_error(std::string(what) + ": " + SDL_GetError());
        }

        /// `source` drawn into `width` × `height` pixels and turned by `rotDegrees`.
        SurfaceUniquePtr draw(SDL_Surface& source, double rotDegrees, int width, int height)
        {
            SurfaceUniquePtr target(SDL_CreateSurface(width, height, source.format), SDL_DestroySurface);
            if (target == nullptr)
                fail("Failed to create cursor target surface");

            const std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
                SDL_CreateSoftwareRenderer(target.get()), SDL_DestroyRenderer);
            if (renderer == nullptr)
                fail("Failed to create cursor renderer");
            if (!SDL_RenderClear(renderer.get()))
                fail("Failed to clear cursor renderer");

            const std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
                SDL_CreateTextureFromSurface(renderer.get(), &source), SDL_DestroyTexture);
            if (texture == nullptr)
                fail("Failed to create cursor texture");
            SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);

            if (!SDL_RenderTextureRotated(
                    renderer.get(), texture.get(), nullptr, nullptr, -rotDegrees, nullptr, SDL_FLIP_NONE))
                fail("Failed to render cursor texture");

            return target;
        }
    }

    void SDLCursorManager::createCursor(std::string_view name, double rotDegrees, osg::Image* image, int hotspotX,
        int hotspotY, int width, int height, float displayScale)
    {
#ifndef ANDROID
        if (mCursorMap.find(name) != mCursorMap.end())
            return;

        const float divisor = baseDivisor(mScaling, displayScale);
        const int baseWidth = std::max(1, windowPoints(width, divisor));
        const int baseHeight = std::max(1, windowPoints(height, divisor));

        try
        {
            const SurfaceUniquePtr decoded = imageToSurface(image);
            SurfaceUniquePtr surface = draw(*decoded, rotDegrees, baseWidth, baseHeight);
            if (divisor > 1.f)
            {
                const SurfaceUniquePtr whole = draw(*decoded, rotDegrees, width, height);
                SDL_AddSurfaceAlternateImage(surface.get(), whole.get());
            }

            SDL_Cursor* cursor
                = SDL_CreateColorCursor(surface.get(), std::clamp(windowPoints(hotspotX, divisor), 0, baseWidth - 1),
                    std::clamp(windowPoints(hotspotY, divisor), 0, baseHeight - 1));
            if (cursor == nullptr)
                fail("Failed to create cursor");
            mCursorMap.emplace(name, cursor);

            if (mEnabled && name == mCurrentCursor)
                SDL_SetCursor(cursor);
        }
        catch (std::exception& e)
        {
            Log(Debug::Warning) << e.what();
            Log(Debug::Warning) << "Using default cursor.";
        }
#endif
    }
}
