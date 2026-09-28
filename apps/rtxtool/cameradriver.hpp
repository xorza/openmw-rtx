#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <osg/Vec3f>

#include "model/skycrossing.hpp"

namespace ESM
{
    struct Region;
}

namespace RtxTool
{
    struct Route;
    struct Stop;

    /// Moves the camera through a stop, frame by frame: where a route has flown to, where a film's
    /// track stands the eye, the clock and the sky, the sky a turn or the keys cross, and the aim
    /// that holds the camera there. What the world is put in once, at the stop's start, is
    /// `Stager`'s; this starts where that leaves the player.
    ///
    /// **The sky is the world's until a turn or a key takes it**, and from then until the stop
    /// ends it is this one's (`SkyCrossing`), handed to the world whole on every frame: the world
    /// then runs no crossing of its own and rolls no weather of its own, which at a fast clock it
    /// did every few seconds over what was asked for. A take's track holds its own sky.
    class CameraDriver
    {
    public:
        /// Starts `stop`'s camera where the stager left the player: at the stop's eye, facing its look,
        /// or where the player stands where the stop names no eye or hands the camera to the player.
        /// A track's clock runs from the game's time now. Aims the camera, because a frame drawn
        /// before the first `aim` would be drawn from wherever the last stop left it.
        void begin(const Stop& stop);

        /// Moves one frame of `stop`, the `measured`th of its measured frames or one of its warm-up
        /// where that is nothing (`Measurer::getMeasuredIndex`): a track stands its eye, clock and
        /// sky at every frame, the warm-up at its first; a route and a turning sky move over the
        /// measured frames alone, `seconds` of world a frame.
        void step(const Stop& stop, std::optional<std::uint32_t> measured, float seconds);

        /// Steps the sky `steps` weathers on from the last one asked for, or back where negative,
        /// among the weathers the player's region rolls, and says where it went. From what the sky
        /// shows now (`SkyCrossing::ask`), at the weather's own `Transition_Delta` and the clock's
        /// speed, and at once where the clock is stopped. Nothing under a take's track, or where
        /// the player stands under no region's sky.
        void turnSkyBy(const Stop& stop, int steps);

        /// Puts the camera where the stop stands this frame, and points it where the stop asked.
        /// Every frame, because `omw/camera/camera.lua`'s `onActive` forces third person and a
        /// stop's teleport reactivates the player: aimed once, every view drew its frames from a
        /// camera 192 units behind the body. `Check::CameraStands` says it still holds. A standing
        /// stop and a heading route are one case, carrying the heading forward from where the route
        /// has flown; nothing where the stop named no camera or gave it to the player.
        void aim(const Stop& stop);

        /// Whether the route has reached the destination it named. What ends a routed stop: the
        /// frames past arrival stand where the route ended and measure nothing the route was flown
        /// for, so `--seconds` is the ceiling a route that never arrives runs to.
        bool hasArrived() const { return mArrived; }

        /// How much of `route`'s line has been flown, nought to one, and one for a line of no
        /// length: a run that ended short measured a shorter journey than its name says.
        float getTravelled(const Route& route) const;

    private:
        /// Puts the route where it starts, the one place all three are set.
        void standAt(const osg::Vec3f& eye, const osg::Vec3f& look);

        /// Puts the camera where the player stands, facing the way they face: what a stop that names
        /// no camera falls back to, and what a free-camera stop starts from.
        void standWhereThePlayerIs();

        /// Flies the player along `route` by one frame's worth of `step` seconds.
        void fly(const Route& route, float step);

        /// Asks for the next weather of `through` where one frame of `step` seconds brings a turn's
        /// time round.
        void turnWeather(const Stop& stop, float step);

        /// Takes the sky for a stop that turns it, from the stop's first frame, and asks for the
        /// turn's first weather.
        void beginTurn(const Stop& stop);

        /// Asks the taken sky for `weather`, which a turn names by its content-file spelling.
        void askTurn(const std::string& weather);

        /// Moves the taken sky on by one frame of `seconds` and hands it to the world, where it is
        /// taken.
        void crossSky(const Stop& stop, float seconds);

        /// Hands the world the sky whole for this frame (`MWBase::World::holdWeather`), as
        /// `SkyCrossing` numbers its weathers.
        static void holdSky(std::uint32_t weather, std::uint32_t next, float crossed);

        /// The sky this holds, taken from the world where it holds none yet.
        SkyCrossing& takeSky();

        /// What the world's sky is doing now.
        static SkyCrossing skyOfTheWorld();

        /// How much of a crossing into `weather` the world runs a second, at the game's own clock.
        static float transitionDeltaOf(std::uint32_t weather);

        /// The chance `region` rolls `weather` at, in per cent, and nought where it names none.
        static int chanceOf(const ESM::Region& region, std::uint32_t weather);

        /// Puts the player's body at `eye`, where a route or a track has the camera this frame.
        static void moveBodyTo(const osg::Vec3f& eye);

        /// Moves the body by however far its camera stands from `mSettling`, once a frame, until it
        /// stands there.
        void settleBody();

        /// Stands the game's camera at `eye` facing `rotation`, `Stand::getRotation`'s angles, for
        /// as long as nothing else moves it.
        static void aimCamera(const osg::Vec3f& eye, const osg::Vec3f& rotation);

        /// Where the eye stood when the stop began, which a route flies from.
        osg::Vec3f mFrom;
        osg::Vec3f mFromLook;

        /// Where the route has flown to: the route's own place and not the player's, because
        /// gravity steps the actor between frames and a step taken from where it landed compounds
        /// the fall.
        osg::Vec3f mFlown;

        /// Which way a track faces this frame, as `TrackPose::mRotation`.
        osg::Vec3f mFacing;

        /// The game's clock at a track's first frame, in hours since the game began: what the
        /// track's hours run on from.
        double mClockFrom = 0.0;

        /// Where a window's own camera has to come to stand, while the body is still being put
        /// under it, and how far off it stood at the last correction, squared: `settleBody`.
        std::optional<osg::Vec3f> mSettling;
        float mSettleLeft = std::numeric_limits<float>::infinity();

        bool mArrived = false;

        /// Which weather the turn is on, and how far into the time before it asks for the next.
        std::size_t mTurnedTo = 0;
        float mTurned = 0.0f;

        /// The sky once a turn or a key took it, and nothing while it is the world's.
        std::optional<SkyCrossing> mSky;

        /// How much of a crossing the keys' last weather takes a second at the game's own clock:
        /// its `Transition_Delta`, read once a press rather than once a frame.
        float mDelta = 0.0f;
    };
}
