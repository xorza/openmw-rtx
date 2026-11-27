#pragma once

#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/scene/light.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    /// Which lamps moved, came or went between the lights the bounce's history was written under and those a
    /// frame is traced under, which the accumulator drops its history by where their light changed
    /// (`Shaders::lampChangeShare`): A-SVGF's temporal gradient (Schied et al. 2018), computed from the lamps
    /// the host knows moved and not re-shaded from the samples, because a lamp's light is the one change of
    /// the bounce the host knows of exactly.
    ///
    /// **A lamp is its place, its reach and its source, and not its index**: the scene's walk lists the lamps
    /// anew every frame, and one lamp that comes or goes renumbers every lamp after it. So the two frames'
    /// lamps are sorted and merged, and each left over on one side is a lamp that went or came; one that
    /// went is paired with the nearest that came whose reach overlaps its own, and the two are one lamp that
    /// moved. Its colour is not part of it: a flame flickers every frame, and the antilag follows a change of
    /// brightness where nothing moved.
    class LampChanges
    {
    public:
        /// Keeps `lights` as the lights the next frame is traced under: the world's scene's, at each place it is
        /// built or placed. A copy, because the scene's list is cleared by its next walk.
        void see(std::span<const Light> lights);

        /// The lamps that changed between the lights the last `spend` was made under and the lights last seen,
        /// each relative to `eye`, nearest it first, and no more than `Shaders::LAMP_CHANGES_MOST`; from then on
        /// the lights seen are what the next is held against. Valid until the next call.
        ///
        /// **A frame's changes and not a placement's**: two placements between two frames leave what the
        /// history saw and what the frame sees, and a lamp that moved and moved back changed nothing.
        std::span<const Shaders::GpuLampChange> spend(const osg::Vec3f& eye);

    private:
        /// Every list refilled and never freed: the lights last seen, sorted by `spend`, and those the last
        /// `spend` was made under; those that went and came, and which of the latter are paired.
        std::vector<Shaders::GpuLampSphere> mSeen;
        std::vector<Shaders::GpuLampSphere> mSpent;
        std::vector<Shaders::GpuLampSphere> mWent;
        std::vector<Shaders::GpuLampSphere> mCame;
        std::vector<bool> mPaired;
        std::vector<Shaders::GpuLampChange> mChanges;
    };
}
