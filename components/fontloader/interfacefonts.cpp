#include "interfacefonts.hpp"

namespace Gui
{
    InterfaceFonts::InterfaceFonts(ToUTF8::FromType encoding, const VFS::Manager* vfs, MyGUIPlatform::DataManager& data,
        const FontFit& fit, bool exportFonts)
        : mTexts(FontUsers::Users::Texts, fit.mFontSize)
        , mLists(FontUsers::Users::Lists, fit.mFontSize)
        , mPages(FontUsers::Users::Entered, fit.mFontSize)
        , mLoader(encoding, vfs, data, fit, exportFonts)
    {
    }

    void InterfaceFonts::fit(const FontFit& fit)
    {
        // A bitmap font is not rasterized at a size, and a text drawn with one still takes the
        // new height.
        const bool resized = fit.mFontSize != getFit().mFontSize;
        if (!mLoader.fit(fit) && !resized)
            return;

        mTexts.refit(fit.mFontSize);
        mLists.refit(fit.mFontSize);
        mPages.refit(fit.mFontSize);
    }
}
