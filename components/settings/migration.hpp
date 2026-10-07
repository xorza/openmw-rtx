#pragma once

#include "categories.hpp"

namespace Settings
{
    /// Rewrites what a `settings.cfg` written by upstream means to this fork, on the user's own
    /// values, before anything reads them.
    ///
    /// **The window's size**: upstream kept it in `[Video] resolution x/y`, which this fork reads as
    /// the frame the picture is drawn at, taking the window from `window width/height`. A file is the
    /// fork's where it carries `[Video] resolution sets the frame`, which this writes into every file
    /// it reads, so the next save keeps it. One without it states the resolution and no window
    /// width only where upstream wrote it, or the fork before the marker: its resolution becomes the
    /// window, and the frame becomes the window's own, nought by nought, which is what upstream drew.
    /// A fork file the marker is on keeps its frame, though only a windowed game writes a window.
    void migrateUserSettings(CategorySettingValueMap& user);
}
