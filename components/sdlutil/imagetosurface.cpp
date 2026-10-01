#include "imagetosurface.hpp"

#include <SDL3/SDL_surface.h>
#include <osg/Image>

namespace SDLUtil
{

    SurfaceUniquePtr imageToSurface(osg::Image* image, bool flip)
    {
        int width = image->s();
        int height = image->t();
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA8888);

        for (int x = 0; x < width; ++x)
            for (int y = 0; y < height; ++y)
            {
                osg::Vec4f clr = image->getColor(x, flip ? ((height - 1) - y) : y);
                int bpp = SDL_BYTESPERPIXEL(surface->format);
                Uint8* p = (Uint8*)surface->pixels + y * surface->pitch + x * bpp;
                *(Uint32*)(p)
                    = SDL_MapSurfaceRGBA(surface, static_cast<Uint8>(clr.r() * 255), static_cast<Uint8>(clr.g() * 255),
                        static_cast<Uint8>(clr.b() * 255), static_cast<Uint8>(clr.a() * 255));
            }

        return SurfaceUniquePtr(surface, SDL_DestroySurface);
    }

}
