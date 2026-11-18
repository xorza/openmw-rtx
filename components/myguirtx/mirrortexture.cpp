#include "mirrortexture.hpp"

#include <cstdint>

#include <components/crashcatcher/crash.hpp>

namespace MyGUIRtx
{
    void MirrorTexture::createManual(std::int32_t, std::int32_t, MyGUI::TextureUsage, MyGUI::PixelFormat)
    {
        Crash::fatal("a mirrored picture is the game's, and is not made through MyGUI");
    }

    void MirrorTexture::loadFromFile(const std::string&)
    {
        Crash::fatal("a mirrored picture is the game's, and is not loaded through MyGUI");
    }

    void* MirrorTexture::lock(MyGUI::TextureUsage)
    {
        Crash::fatal("a mirrored picture is the game's, and is not written through MyGUI");
    }

    void MirrorTexture::unlock()
    {
        Crash::fatal("a mirrored picture is the game's, and is not written through MyGUI");
    }
}
