#pragma once

#include <components/toutf8/toutf8.hpp>
#include <components/widgets/textregistry.hpp>

#include "fontloader.hpp"

namespace MyGUIPlatform
{
    class DataManager;
}

namespace VFS
{
    class Manager;
}

namespace Gui
{
    /// **The interface's fonts and every text drawn with them, changed as one.** The fonts are
    /// rasterized in place, which replaces their textures, and every text has to take its font's
    /// texture again before the next frame draws or it draws one destroyed
    /// (`docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`); `fit` is the one call that changes
    /// a font, so no caller can do the first without the second.
    ///
    /// Made once MyGUI's factory manager exists and before a skin is loaded, because it stands its
    /// texts in MyGUI's factories; destroyed after MyGUI shuts down, because every text leaves its
    /// registry as it is destroyed.
    class InterfaceFonts
    {
    public:
        /// @param exportFonts export the converted fonts (Images and XML with glyph metrics) to files?
        InterfaceFonts(ToUTF8::FromType encoding, const VFS::Manager* vfs, MyGUIPlatform::DataManager& data,
            const FontFit& fit, bool exportFonts);

        InterfaceFonts(const InterfaceFonts&) = delete;
        InterfaceFonts& operator=(const InterfaceFonts&) = delete;

        /// Rasterizes each font `fit` moves again, and has every text take its font again and every
        /// widget sized to its text size itself again. Nothing where no font moves.
        void fit(const FontFit& fit);

        const FontFit& getFit() const { return mLoader.getFit(); }

        const TextRegistry& getTexts() const { return mTexts; }

    private:
        TextRegistry mTexts;
        FontLoader mLoader;
    };
}
