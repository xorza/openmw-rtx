#ifndef OPENMW_COMPONENTS_MYGUIPLATFORM_GUIRENDERMANAGER_H
#define OPENMW_COMPONENTS_MYGUIPLATFORM_GUIRENDERMANAGER_H

#include <MyGUI_RenderManager.h>

namespace MyGUIPlatform
{

    /// MyGUI's render manager, plus the two calls MyGUI does not declare and every backend needs.
    ///
    /// **Neutral, despite where it lives**, for the reason `Picture` is: this is MyGUI's own
    /// interface with two lifetime hooks on it, and it has no idea what draws. It exists so that one
    /// `Platform` serves every backend — the log and the data manager beside it are the same either
    /// way, and it is only the render manager that is anybody's.
    class GuiRenderManager : public MyGUI::RenderManager
    {
    public:
        /// Called once, after MyGUI's log manager exists, because this logs.
        virtual void initialise() = 0;

        /// Called while whatever the backend attached itself to is still alive, which is why it is
        /// not the destructor.
        virtual void shutdown() = 0;
    };

}

#endif
