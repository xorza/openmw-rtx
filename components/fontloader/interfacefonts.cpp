#include "interfacefonts.hpp"

namespace Gui
{
    InterfaceFonts::InterfaceFonts(ToUTF8::FromType encoding, const VFS::Manager* vfs, MyGUIPlatform::DataManager& data,
        const FontFit& fit, bool exportFonts)
        : mLoader(encoding, vfs, data, fit, exportFonts)
    {
        mTexts.registerTextFactories();
    }

    void InterfaceFonts::fit(const FontFit& fit)
    {
        if (!mLoader.fit(fit))
            return;

        mTexts.refit();
        mPages.refit();
    }
}
