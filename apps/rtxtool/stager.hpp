#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace MWBase
{
    class World;
}

namespace RtxTool
{
    struct SessionRequest;
    struct Stop;

    /// Puts the world where a stop stands, once, at its start: the player in the stop's cell, the
    /// clock and the sky, the seed, the rate the clock runs at, god mode, the interface, and the walls
    /// where the camera is flown or followed rather than walked. What moves while the stop runs is
    /// `CameraDriver`'s. The statics are what the session asks of the world between frames, because
    /// the game undoes a stop there: the history the first uncounted frames leave, a menu a script
    /// opens, and what holds the world paused where a frame says it stood so.
    class Stager
    {
    public:
        /// Stages `stop` of `request`, or says why it cannot be: a cell nothing is called. Ends with
        /// the navmesh built whole and the renderer's history let go of, so the first frame drawn
        /// after this is drawn at the stop and from nothing before it.
        std::optional<std::string> stage(const Stop& stop, const SessionRequest& request) const;

        /// Tells the renderer that nothing before this frame describes where it now stands.
        static void forgetHistory();

        /// Leaves every menu open, as a player closing each would. For a run nobody plays, every
        /// frame: a menu pauses the world, and nobody is at the keys to close one a script opens —
        /// M[FR]'s hotkey notice opens one on every new game, and every run under it measured a
        /// world with its clock, its particles and its air standing still. The console, the
        /// post-processing window and a message box are no menus and stay (`describePause`).
        static void closeMenus();

        /// What holds the world paused, as a report names it: the pause tags a script set — the
        /// interface's `ui` among them, which a menu sets — and the game's own reasons beside them.
        static std::string describePause();

    private:
        /// Puts the sky under the weather called `name` over the player's region, as `changeweather`
        /// would, and warns for a name that is none of the ten.
        static void setWeather(MWBase::World& world, std::string_view name);

        /// Gives the player every attribute and skill at 255, a Speed of 2000, level 255 and a
        /// million gold, through the calls the console's `setspeed`, `setlevel` and `additem` make.
        /// A body walking at Morrowind's pace crosses a cell in a minute.
        static void boostPlayer();

        /// Turns the player's collision off, as `tcl` does.
        static void turnCollisionOff(MWBase::World& world);
    };
}
