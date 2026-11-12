#pragma once

#include <components/toutf8/toutf8.hpp>
#include <components/widgets/fontusers.hpp>

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
    /// **The interface's fonts and everything drawn with them, changed as one.** Rasterizing a font in place
    /// replaces its textures, and every text and book page has to take its font's texture again before the
    /// next frame draws (`docs/rtx/research/2026-10-31-mygui-font-lifetimes.md`); `fit` is the one call that
    /// changes a font, so no caller can do the first without the second.
    ///
    /// The texts, lists and pages are three registries, refitted in that order: a list's refit may make
    /// texts, a page's may make texts and lists, and a registry makes no user of itself while it refits.
    ///
    /// Made once MyGUI's factory manager exists and before a skin is loaded; destroyed after MyGUI shuts
    /// down, because every text and list leaves its registry as it is destroyed.
    class InterfaceFonts
    {
    public:
        /// @param exportFonts export the converted fonts (Images and XML with glyph metrics) to files?
        InterfaceFonts(ToUTF8::FromType encoding, const VFS::Manager* vfs, MyGUIPlatform::DataManager& data,
            const FontFit& fit, bool exportFonts);

        InterfaceFonts(const InterfaceFonts&) = delete;
        InterfaceFonts& operator=(const InterfaceFonts&) = delete;

        /// Rasterizes each font `fit` moves again, and has every text take its font and the text
        /// height again and every widget sized to its text size itself again, then every list
        /// stand its lines at the new line height, then every book page typeset again. Nothing
        /// where neither a font nor the font size moves.
        void fit(const FontFit& fit);

        const FontFit& getFit() const { return mLoader.getFit(); }

        /// **The interface's font size**: what every line of its text is laid out at, in layout
        /// units, and what a row or a list sized to a line of text sizes by. `[GUI] font size` as
        /// the fonts were last fitted to: the fit is the setting's one reader, and each registry
        /// holds the size its users were last refitted at, which `fit` alone sets.
        int getFontSize() const { return getFit().mFontSize; }

        const FontUsers& getTexts() const { return mTexts; }

        /// What a book page enters as it is made (`MWGui::BookPage::registerMyGUIComponents`).
        FontUsers& getPages() { return mPages; }

    private:
        FontUsers mTexts;
        FontUsers mLists;
        FontUsers mPages;
        FontLoader mLoader;
    };
}
