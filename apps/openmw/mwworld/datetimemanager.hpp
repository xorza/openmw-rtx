#ifndef GAME_MWWORLD_DATETIMEMANAGER_H
#define GAME_MWWORLD_DATETIMEMANAGER_H

#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "globalvariablename.hpp"

namespace ESM
{
    struct EpochTimeStamp;
}

namespace MWWorld
{
    class Globals;
    class TimeStamp;
    class World;

    class DateTimeManager
    {
    public:
        // Game time.
        // Note that game time generally goes faster than the simulation time.
        std::string_view getMonthName(int month = -1) const; // -1: current month
        // The time of the day as the Calendar localization shows it.
        std::string formatTimeOfDay(float hour) const;
        TimeStamp getTimeStamp() const;
        ESM::EpochTimeStamp getEpochTimeStamp() const;
        double getGameTime() const
        {
            return (static_cast<double>(mDaysPassed) * 24 + static_cast<double>(mGameHour)) * 3600.0;
        }
        float getGameTimeScale() const { return mGameTimeScale; }
        void setGameTimeScale(float scale); // game time to simulation time ratio

        // Rendering simulation time (summary simulation time of rendering frames since application start).
        double getRenderingSimulationTime() const { return mRenderingSimulationTime; }
        void setRenderingSimulationTime(double t) { mRenderingSimulationTime = t; }

        // World simulation time (the number of seconds passed from the beginning of the game).
        double getSimulationTime() const { return mSimulationTime; }
        void setSimulationTime(double t) { mSimulationTime = t; }
        float getSimulationTimeScale() const { return mSimulationTimeScale; }
        void setSimulationTimeScale(float scale); // simulation time to real time ratio

        // Whether the game is paused in the current frame.
        bool isPaused() const { return mPaused; }

        // Pauses the game starting from the next frame until `unpause` is called with the same tag.
        void pause(std::string_view tag) { mPausedTags.emplace(tag); }
        void unpause(std::string_view tag);
        const std::set<std::string, std::less<>>& getPausedTags() const { return mPausedTags; }

        // Updates mPaused; should be called once a frame.
        void updateIsPaused();

        /// Whether the clock jumped from `before` to `after`: moved either way round the day by
        /// more than `step`, the hours the frame advanced it by. A script that holds the hour moves
        /// it back by one step and no more, and `set gamehour to 21` moves it by hours.
        static bool jumps(float before, float after, double step);

    private:
        friend class World;
        void setup(Globals& globalVariables);
        void updateGlobalInt(GlobalVariableName name, int value);
        void updateGlobalFloat(GlobalVariableName name, float value);
        void advanceTime(double hours, Globals& globalVariables);

        void setHour(double hour);
        void setDay(int day);
        void setMonth(int month);

        int mDaysPassed = 0;
        int mDay = 0;
        int mMonth = 0;
        int mYear = 0;
        float mGameHour = 0.f;
        float mGameTimeScale = 0.f;
        float mSimulationTimeScale = 1.0;
        double mRenderingSimulationTime = 0.0;
        double mSimulationTime = 0.0;
        bool mPaused = false;
        std::set<std::string, std::less<>> mPausedTags;
    };

    /// The hour the last frame left, and whether the clock jumped since (`DateTimeManager::jumps`):
    /// what the ray tracer's histories hear as a cut. **Read once a frame on the main thread and
    /// not where the hour is written**, because a Lua global script writes it from the Lua worker
    /// while the frame draws, and because every writer — mwscript, Lua, the console, a host — then
    /// meets one rule.
    class HourWatch
    {
    public:
        /// Whether the clock jumped from the hour the last call saw to `hour`, the frame having
        /// advanced it by `step`; false on the first call after a `reset`, which has nothing to hold
        /// `hour` against.
        bool jumped(float hour, double step);

        /// Forgets the hour: a new game or a load, which is a cut of its own.
        void reset() { mSeen.reset(); }

    private:
        std::optional<float> mSeen;
    };
}

#endif
