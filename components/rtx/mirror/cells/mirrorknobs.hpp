#pragma once

#include <components/rtx/scene/specularlayout.hpp>

#include "cellgrid.hpp"

namespace Rtx
{
    /// What the mirror is handed of the settings, and never reads for itself: the two knobs the
    /// paging read for the distance's statics, which this renderer stands itself, and how far out
    /// the world is built. Handed once, because a frame reads what it was handed: the reach is one
    /// number for the ground, the air, the distant lights and the checks, and a host that asked
    /// the registry per frame could answer it differently in each. A run's, in `RunSetup`, so the
    /// harness and the played game fill it the one way; the statics need a restart, and the reach
    /// follows the menu through `WorldMirror::setReach`.
    struct MirrorKnobs
    {
        /// How far out the world is built, in cells — `CellGrid::reachOf` puts it in units.
        LandReach mReach;

        /// `object paging`: whether the distance's statics stand at all.
        bool mDistantStatics = true;

        /// `object paging min size`: the size rule's constant.
        float mMinSize = 0.0f;

        /// `[RTX] specular map layout`: what the content's `_spec` maps mean, for every scene the
        /// mirror and the pictures inside the interface read materials into.
        SpecularLayout mSpecularLayout = SpecularLayout::Ignore;
    };
}
