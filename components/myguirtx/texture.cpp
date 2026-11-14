#include "texture.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <MyGUI_RenderFormat.h>
#include <osg/Image>
#include <osg/ref_ptr>

#include <components/crashcatcher/crash.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/common/letgo.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/vfs/pathutil.hpp>

namespace MyGUIRtx
{
    namespace
    {
        std::size_t elementsOf(MyGUI::PixelFormat format)
        {
            switch (format.getValue())
            {
                case MyGUI::PixelFormat::L8:
                    return 1;
                case MyGUI::PixelFormat::L8A8:
                    return 2;
                case MyGUI::PixelFormat::R8G8B8:
                    return 3;
                case MyGUI::PixelFormat::R8G8B8A8:
                    return 4;
                default:
                    return 0;
            }
        }
    }

    Texture::Texture(std::string name, Rtx::GuiRenderer& renderer, Resource::ImageManager* imageManager)
        : SlotTexture(std::move(name), renderer)
        , mImageManager(imageManager)
    {
    }

    void Texture::release()
    {
        drop();
        mFormat = MyGUI::PixelFormat::Unknow;
        mUsage = MyGUI::TextureUsage::Default;
        mNumElemBytes = 0;
        mPixels.clear();
        mLocked = false;
    }

    void Texture::createManual(int width, int height, MyGUI::TextureUsage usage, MyGUI::PixelFormat format)
    {
        // MyGUI asks its formats of the game's interface code, which asks none this backend does not draw.
        const std::size_t elements = elementsOf(format);
        Crash::contract(elements != 0, "an interface texture in a format the backend does not draw");

        release();

        mFormat = format;
        mUsage = usage;
        mNumElemBytes = elements;
        take(width, height);
    }

    void Texture::loadFromFile(const std::string& fname)
    {
        Crash::contract(mImageManager != nullptr, "an interface texture read from a file with no image manager");

        // **Decoded by the engine's own image manager**, which is where every other picture in this
        // fork comes from: OpenSceneGraph stays as the content loader whatever draws
        // (`docs/architecture.md`, "Layers and libraries"), and a second decoder here would be a second set
        // of formats to be wrong about.
        const osg::ref_ptr<osg::Image> image = mImageManager->getImage(VFS::Path::Normalized(fname));

        createManual(image->s(), image->t(), MyGUI::TextureUsage::Static, MyGUI::PixelFormat::R8G8B8A8);
        sendImage(*image);
    }

    void Texture::destroy()
    {
        release();
    }

    void* Texture::lock(MyGUI::TextureUsage /*access*/)
    {
        assert(mNumElemBytes != 0 && "a texture locked before it was created");
        assert(!mLocked && "a texture locked twice");

        mLocked = true;

        // The renderer's own bytes at four channels. **A buffer of its own for one fill otherwise, dropped at the
        // unlock**: a narrower format is widened out of it there, and a slot the renderer refused draws as nothing
        // (`SlotTexture::take`) though MyGUI still fills the whole surface it locked, a font's atlas past the
        // device's side among them. Held between a lock and its unlock alone, where a 1024 square atlas held two
        // megabytes for as long as the texture stood.
        if (mNumElemBytes == 4 && !getSlot().isNone())
            return mRenderer.lendGuiTexture(getSlot(), whole()).data();

        mPixels.assign(static_cast<std::size_t>(getWidth()) * getHeight() * mNumElemBytes, 0);
        return mPixels.data();
    }

    void Texture::unlock()
    {
        assert(mLocked && "a texture unlocked that was not locked");

        mLocked = false;

        if (mNumElemBytes == 4 && !getSlot().isNone())
        {
            mRenderer.sendGuiTexture(getSlot());
            return;
        }

        if (!getSlot().isNone())
            widen();
        Rtx::letGoOf(mPixels);
    }

    void Texture::widen()
    {
        // MyGUI asked for fewer channels than the table holds, so they are widened here rather than
        // by giving the table a second format to know about: a font atlas is written once and this
        // is the only place that knows what its bytes meant.
        const std::size_t count = static_cast<std::size_t>(getWidth()) * getHeight();
        std::uint8_t* const into = mRenderer.lendGuiTexture(getSlot(), whole()).data();

        // One loop a format, the format chosen once and not at every pixel.
        const std::uint8_t* const from = mPixels.data();
        switch (mNumElemBytes)
        {
            case 1:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint8_t* const out = into + i * 4;
                    out[0] = out[1] = out[2] = from[i];
                    out[3] = 0xFF;
                }
                break;
            case 2:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint8_t* const out = into + i * 4;
                    out[0] = out[1] = out[2] = from[i * 2];
                    out[3] = from[i * 2 + 1];
                }
                break;
            default:
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::uint8_t* const out = into + i * 4;
                    out[0] = from[i * 3];
                    out[1] = from[i * 3 + 1];
                    out[2] = from[i * 3 + 2];
                    out[3] = 0xFF;
                }
                break;
        }

        mRenderer.sendGuiTexture(getSlot());
    }
}
