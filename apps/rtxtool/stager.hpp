#pragma once

#include <string>

#include <components/misc/result.hpp>

namespace RtxTool
{
    struct SessionRequest;
    struct Stop;

    /// Puts the world where a stop stands, once, at its start: the player in the stop's cell, the
    /// clock and the sky, the seed, the rate the clock runs at, god mode, the interface, the weapon
    /// an armed stop holds, and the walls where the camera is flown or followed rather than walked. What moves while
    /// the stop runs is `CameraDriver`'s. The rest are what the session asks of the world between frames, because
    /// the game undoes a stop there: the history the first uncounted frames leave, a menu a script
    /// opens, and what holds the world paused where a frame says it stood so.
    namespace Staging
    {
        /// Stages `stop` of `request`, and says why not where it cannot be: the content holds no cell or weapon of the
        /// name the stop gives, which the session passes over (`WhenAbsent`). Ends with
        /// the navmesh built whole and the renderer's history let go of, so the first frame drawn
        /// after this is drawn at the stop and from nothing before it.
        Result<void, std::string> stage(const Stop& stop, const SessionRequest& request);

        /// The days passed a new game starts on: the content's own `DaysPassed` global, from which a
        /// stop's day is counted, 16 Last Seed in Morrowind.
        int newGameDaysPassed();

        /// Tells the renderer that nothing before this frame describes where it now stands.
        void forgetHistory();

        /// Leaves every menu open, as a player closing each would. For a run nobody plays, every
        /// frame: a menu pauses the world, and nobody is at the keys to close one a script opens —
        /// M[FR]'s hotkey notice opens one on every new game, and every run under it measured a
        /// world with its clock, its particles and its air standing still. The console, the
        /// post-processing window and a message box are no menus and stay (`describePause`).
        void closeMenus();

        /// What holds the world paused, as a report names it: the pause tags a script set — the
        /// interface's `ui` among them, which a menu sets — and the game's own reasons beside them.
        std::string describePause();
    }
}
