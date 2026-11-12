#pragma once

#include "categories.hpp"

namespace Settings
{
    /// Rewrites what a `settings.cfg` written by upstream means to this fork, on the user's own
    /// values, before anything reads them.
    ///
    /// **The window's size**: upstream kept it in `[Video] resolution x/y`, which this fork reads as the
    /// frame the picture is drawn at, taking the window from `window width/height`. A file is the fork's
    /// where it carries `[Video] resolution sets the frame`, which this writes into every file it reads.
    /// One without it that names a resolution and no window is upstream's: its resolution becomes the
    /// window, and the frame the window's own, nought by nought, which is what upstream drew.
    void migrateUserSettings(CategorySettingValueMap& user);
}
