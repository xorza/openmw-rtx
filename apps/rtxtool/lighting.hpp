#pragma once

#include <limits>

#include <osg/Vec3f>

#include <components/rtxbridge/fogbuilder.hpp>
#include <components/rtxbridge/lightbuilder.hpp>

namespace Rtx
{
    namespace Shaders
    {
        struct VisibilityConstants;
    }
}

namespace RtxTool
{
    /// How a cell is lit, which is the part of that a scene cannot carry.
    ///
    /// Its lamps are in the scene, being things standing in it. Its ambient belongs to the cell
    /// itself and its sun belongs to the hour, and neither is anywhere a ray can find them.
    struct CellLighting
    {
        osg::Vec3f mAmbient;

        /// How long the water has been moving, in seconds. Zero is a still sea and a deterministic
        /// frame, which is what a screenshot wants; a window passes its own clock.
        float mSeconds = 0.0f;

        /// Where the water's surface is. Minus infinity where the cell holds none, so that "how deep
        /// is this point" is never positive and nothing downstream needs a second question.
        float mWaterLevel = -std::numeric_limits<float>::infinity();

        /// The sun and the sky over an exterior. An interior leaves this dark.
        RtxBridge::Daylight mDaylight;

        /// The air in the cell, whichever of the two places it came from: an interior's `AMBI` or
        /// the weather over an exterior. A zero extinction is a cell with no fog, and costs nothing.
        RtxBridge::Fog mFog;
    };

    /// Writes how the cell is lit into the constants a frame is traced with.
    ///
    /// Shared because a screenshot and a window are the same frame: the two paths differ in where
    /// the camera comes from and in nothing else, and a light that reached one but not the other
    /// would be a difference nobody was looking for.
    ///
    /// The lamps themselves are not here. They are in the scene, and the pass finds them through the
    /// grid `SceneBuffers` binned them into rather than through a count anyone has to remember.
    void applyLighting(const CellLighting& lighting, Rtx::Shaders::VisibilityConstants& constants);
}
