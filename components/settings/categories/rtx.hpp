#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RTX_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RTX_H

#include <components/settings/settingvalue.hpp>

namespace Settings
{
    /// The experimental ray tracing renderer.
    ///
    /// These are read once, at startup: the renderer decides how the window is created and what
    /// draws into it, so there is nothing meaningful to change while a frame is in flight. The
    /// settings exist whether or not the renderer was compiled in, so that a configuration file
    /// survives moving between builds.
    struct RTXCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mEnabled{ mIndex, "RTX", "enabled" };
        SettingValue<bool> mValidation{ mIndex, "RTX", "validation" };
    };
}

#endif
