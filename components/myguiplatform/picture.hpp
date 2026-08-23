#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_PICTURE_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_PICTURE_H

#include <string>
#include <string_view>

#include <MyGUI_RenderFormat.h>

namespace MyGUI
{
    class ITexture;
}

namespace osg
{
    class Image;
}

namespace MyGUIPlatform
{

    /// Pixels the game holds in main memory, in front of the GUI.
    ///
    /// **Renderer-neutral, despite where it lives.** It names MyGUI's own factory and its
    /// `lock`/`unlock`, which every backend implements and none of which says what draws. It is here
    /// because this is where OSG meets MyGUI, and an `osg::Image` is what the game's video decoder,
    /// savegames, fog of war and map both produce and keep.
    class Picture
    {
    public:
        /// @param label what the texture is called for anything that lists them. Made unique here,
        ///        because MyGUI keys its textures by name and two pictures are two textures.
        explicit Picture(std::string_view label);
        ~Picture();

        Picture(const Picture&) = delete;
        Picture& operator=(const Picture&) = delete;

        /// Movable, because a picture is often a member of something a container holds.
        Picture(Picture&& other) noexcept;
        Picture& operator=(Picture&& other) noexcept;

        /// Copies the whole image in, making the texture the first time and again whenever the image
        /// changes shape. All or nothing: MyGUI hands out a fresh buffer on every lock and there is
        /// no asking it for part of one.
        void set(const osg::Image& image);

        /// Null until the first `set`.
        MyGUI::ITexture* getTexture() const { return mTexture; }

    private:
        std::string mName;
        MyGUI::ITexture* mTexture = nullptr;
        int mWidth = 0;
        int mHeight = 0;
        MyGUI::PixelFormat mFormat = MyGUI::PixelFormat::Unknow;
    };

}

#endif
