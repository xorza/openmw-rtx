#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RTX_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_RTX_H

#include <string>

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

        /// Loads the Vulkan validation layers and refuses to start on anything they object to.
        ///
        /// **No control offers this, on purpose.** It is a developer's diagnostic: it costs a large
        /// part of the frame rate, it needs layers that are not on a player's machine, and what it
        /// does when it finds something is stop. It is reached by editing `settings.cfg`, which is
        /// the audience it has; `openmw-rtxtool --validation` is the same switch for the harness and
        /// is on there by default outside a Release build.
        SettingValue<bool> mValidation{ mIndex, "RTX", "validation" };

        /// How hard DLSS Ray Reconstruction works, or `off` for none of it.
        ///
        /// A name rather than a number, and unrecognised is refused rather than defaulted — see
        /// `Rtx::upscaleNamed`.
        SettingValue<std::string> mUpscale{ mIndex, "RTX", "upscale" };
    };
}

#endif
