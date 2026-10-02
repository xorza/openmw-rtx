#pragma once

#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/scene/ripple.hpp>

#include "../../mwworld/ptr.hpp"
#include "../sceneframe.hpp"

namespace MWWorld
{
    class CellStore;
}

namespace MWRender
{
    /// What disturbs the water, decided as the rasterizer's `RippleSimulation` decides it: every
    /// actor the scene adds is an emitter, and one that stands in water without being submerged —
    /// or walks on it — presses a ring under its feet every frame; a strike on the water presses
    /// one where it landed. What comes out is the frame's list of `Rtx::RippleImpulse`, which the
    /// trace's ripple field takes.
    ///
    /// **`RippleSimulation::update`'s rule, and not a copy of it**: who presses a ring and how big
    /// is `ripplerules.hpp`, which the rasterizer's ripples ask as well, so the two renderers ring
    /// the same water. What is this class's is which actors it watches and the list it hands on.
    class RippleEmitters
    {
    public:
        void add(const MWWorld::Ptr& ptr);
        void remove(const MWWorld::Ptr& ptr);

        /// Drops every emitter that stands in `cell` but the player, who is refreshed by pointer.
        void removeCell(const MWWorld::CellStore& cell);

        /// Something struck the water at `at`, which is pressed on the next `update` if it landed
        /// near enough the surface — `strikesWater`, the test `RippleSimulation::emitRipple` asks.
        void splash(const osg::Vec3f& at);

        /// Decides this frame's impulses: the wading emitters and the strikes since the last. Every
        /// frame, so the list is always this frame's: a paused one decides none and keeps its
        /// strikes for the frame the simulation runs again.
        /// @param water where the surface stands, which a strike is tested against, and whether
        ///        there is one.
        /// @param simulated whether the simulation advanced this frame.
        void update(const WaterState& water, bool simulated);

        /// Forgets the strikes since the last update, for a frame that draws no world: the game goes
        /// on while the world is hidden, and its strikes are not this renderer's to press later.
        void dropStrikes() { mStrikes.clear(); }

        /// What `update` decided, until the next.
        std::span<const Rtx::RippleImpulse> getImpulses() const { return mImpulses; }

        /// Everything, for a world that is going.
        void clear();

    private:
        std::vector<MWWorld::ConstPtr> mEmitters;

        /// Strikes since the last update, and the impulses the last update decided. Both refilled
        /// per frame and never freed.
        std::vector<osg::Vec3f> mStrikes;
        std::vector<Rtx::RippleImpulse> mImpulses;
    };
}
