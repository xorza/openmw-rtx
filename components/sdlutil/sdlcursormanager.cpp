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

    SDLCursorManager::SDLCursorManager()
        : mEnabled(false)
        , mInitialized(false)
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
        SurfaceUniquePtr draw(SDL_Surface& source, float rotDegrees, int width, int height)
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

            if (!SDL_RenderTextureRotated(renderer.get(), texture.get(), nullptr, nullptr,
                    static_cast<double>(-rotDegrees), nullptr, SDL_FLIP_NONE))
                fail("Failed to render cursor texture");

            return target;
        }
    }

    void SDLCursorManager::createCursor(std::string_view name, int rotDegrees, osg::Image* image, int hotspotX,
        int hotspotY, int width, int height, float displayScale)
    {
#ifndef ANDROID
        if (mCursorMap.find(name) != mCursorMap.end())
            return;

        const int baseWidth = std::max(1, windowPoints(width, displayScale));
        const int baseHeight = std::max(1, windowPoints(height, displayScale));

        try
        {
            const SurfaceUniquePtr decoded = imageToSurface(image);
            SurfaceUniquePtr surface = draw(*decoded, static_cast<float>(rotDegrees), baseWidth, baseHeight);
            if (displayScale > 1.f)
            {
                const SurfaceUniquePtr whole = draw(*decoded, static_cast<float>(rotDegrees), width, height);
                SDL_AddSurfaceAlternateImage(surface.get(), whole.get());
            }

            SDL_Cursor* cursor = SDL_CreateColorCursor(surface.get(),
                std::clamp(windowPoints(hotspotX, displayScale), 0, baseWidth - 1),
                std::clamp(windowPoints(hotspotY, displayScale), 0, baseHeight - 1));
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
