#include "imagetosurface.hpp"

#include <cstddef>
#include <cstdint>

#include <SDL3/SDL_surface.h>
#include <osg/Image>

namespace SDLUtil
{

    SurfaceUniquePtr imageToSurface(osg::Image* image, bool flip)
    {
        std::int32_t width = image->s();
        std::int32_t height = image->t();
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);

        for (std::int32_t x = 0; x < width; ++x)
            for (std::int32_t y = 0; y < height; ++y)
            {
                osg::Vec4f clr = image->getColor(static_cast<std::uint32_t>(x),
                    flip ? (static_cast<std::uint32_t>((height - 1) - y)) : static_cast<std::uint32_t>(y));
                std::int32_t bpp = SDL_BYTESPERPIXEL(surface->format);
                Uint8* p = static_cast<Uint8*>(surface->pixels) + static_cast<std::ptrdiff_t>(y) * surface->pitch
                    + static_cast<std::ptrdiff_t>(x) * bpp;
                *reinterpret_cast<Uint32*>(p)
                    = SDL_MapSurfaceRGBA(surface, static_cast<Uint8>(clr.r() * 255), static_cast<Uint8>(clr.g() * 255),
                        static_cast<Uint8>(clr.b() * 255), static_cast<Uint8>(clr.a() * 255));
            }

        return SurfaceUniquePtr(surface, SDL_DestroySurface);
    }

}
