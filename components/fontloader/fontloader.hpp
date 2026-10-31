#ifndef OPENMW_COMPONENTS_FONTLOADER_H
#define OPENMW_COMPONENTS_FONTLOADER_H

#include <vector>

#include <MyGUI_Types.h>
#include <MyGUI_Version.h>
#include <MyGUI_XmlDocument.h>

#include <components/myguiplatform/myguidatamanager.hpp>
#include <components/toutf8/toutf8.hpp>

namespace VFS
{
    class Manager;
}

namespace MyGUI
{
    class ITexture;
    class ResourceManualFont;
    class ResourceTrueTypeFont;
}

namespace Gui
{
    /// What the TrueType fonts are rasterized for: how many frame pixels a unit of the interface
    /// takes, which a font is rasterized at rounded up to a whole multiple, and the interface's view
    /// in its own units, which the journal's book layer is scaled into and its book font follows.
    struct FontFit
    {
        float mRaster = 1.0f;
        MyGUI::IntSize mView;

        bool operator==(const FontFit& other) const = default;
    };

    /// @brief loads Morrowind's .fnt/.tex fonts for use with MyGUI and OSG
    /// @note The FontLoader needs to remain in scope as long as you want to use the loaded fonts.
    class FontLoader
    {
    public:
        /// @param data what a TrueType font's file is held in, for every rasterization after the
        /// load to read again (`MyGUIPlatform::DataManager::hold`).
        /// @param exportFonts export the converted fonts (Images and XML with glyph metrics) to files?
        FontLoader(ToUTF8::FromType encoding, const VFS::Manager* vfs, MyGUIPlatform::DataManager& data,
            const FontFit& fit, bool exportFonts);

        FontLoader(const FontLoader&) = delete;
        FontLoader& operator=(const FontLoader&) = delete;

        /// Rasterizes each TrueType font whose resolution `fit` changes again, in place: its
        /// `IFont*` stays and its texture is replaced, so every text drawn with it has to take the
        /// font's texture again before the next frame draws
        /// (`docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`). Reads nothing from the VFS.
        void fit(const FontFit& fit);

        const FontFit& getFit() const { return mFit; }

        void overrideLineHeight(MyGUI::xml::ElementPtr node, std::string_view file, MyGUI::Version version);

        static std::string_view getFontForFace(std::string_view face);

    private:
        struct FontId
        {
            std::string_view mValue;
        };

        /// A TrueType font this made, and the resolution its file states, which a fit multiplies.
        struct TrueTypeFace
        {
            MyGUI::ResourceTrueTypeFont* mFont = nullptr;
            int mResolution = 0;

            /// In points, as the load sizes it.
            int mSize = 0;

            bool mBook = false;

            /// Whether its file states a tab's width, which MyGUI otherwise derives from a space's
            /// at the first rasterization and keeps.
            bool mTabWidthStated = false;
        };

        unsigned int resolutionOf(const TrueTypeFace& face, const FontFit& fit) const;

        /// Rasterizes `face` at `resolution` the one way every rasterization goes, the load's included.
        static void rasterize(const TrueTypeFace& face, unsigned int resolution);

        ToUTF8::FromType mEncoding;
        const VFS::Manager* mVFS;
        MyGUIPlatform::DataManager& mData;
        bool mExportFonts;
        FontFit mFit;

        /// The journal's book layer, as `openmw_layers.xml` lays it out: read once, at the load.
        MyGUI::IntSize mBookSize;

        std::vector<TrueTypeFace> mFaces;

        void loadFonts();
        void loadFont(FontId fontId, std::string_view fontName);

        void loadBitmapFont(FontId fontId, const VFS::Path::Normalized& path, std::istream& stream);
        void loadTrueTypeFont(FontId fontId, const VFS::Path::Normalized& path, std::istream& stream);
    };

}

#endif
