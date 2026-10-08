#pragma once

#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/scene/specularlayout.hpp>

namespace MWRender
{
    /// What the mirror is handed of the settings, and never reads for itself: the two knobs the
    /// paging read for the distance's statics, which this renderer stands itself, the three the
    /// groundcover read, and how far out the world is built. Handed once, because a frame reads
    /// what it was handed: the reach is one number for the ground, the air, the distant lights and
    /// the checks, and a host that asked the registry per frame could answer it differently in
    /// each. A run's, in `RunSetup`, so the harness and the played game fill it the one way; the
    /// statics need a restart, and the reach follows the menu through `WorldMirror::setReach`.
    struct MirrorKnobs
    {
        /// How far out the world is built, in cells — `Rtx::CellGrid::reachOf` puts it in units.
        Rtx::LandReach mReach;

        /// `object paging`: whether the distance's statics stand at all.
        bool mDistantStatics = true;

        /// `object paging min size`: the size rule's constant.
        float mMinSize = 0.0f;

        /// `[Groundcover] rendering distance`, in units, where `[Groundcover] enabled` is on, and
        /// nought where it is off: `Rtx::WorldAround::mGroundcoverReach`.
        float mGroundcoverReach = 0.0f;

        /// `[Groundcover] density`: the share of each cell's plants that stand.
        float mGroundcoverDensity = 0.0f;

        /// `[Groundcover] point lighting`: whether the lamps light a plant.
        bool mGroundcoverLampLit = true;

        /// `[RTX] specular map layout`: what the content's `_spec` maps mean, for every scene the
        /// mirror and the pictures inside the interface read materials into.
        Rtx::SpecularLayout mSpecularLayout = Rtx::SpecularLayout::Classic;
    };
}
