#ifndef OPENMW_APPS_RTXTOOL_LIGHTING_H
#define OPENMW_APPS_RTXTOOL_LIGHTING_H

#include <limits>

#include <osg/Vec3f>

#include <components/rtxbridge/fogbuilder.hpp>
#include <components/rtxbridge/lightbuilder.hpp>

namespace Rtx
{
    class SceneDesc;

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

    /// Writes the lighting and the scene's lamp count into the constants a frame is traced with.
    ///
    /// Shared because a screenshot and a window are the same frame: the two paths differ in where
    /// the camera comes from and in nothing else, and a light that reached one but not the other
    /// would be a difference nobody was looking for.
    void applyLighting(
        const CellLighting& lighting, const Rtx::SceneDesc& scene, Rtx::Shaders::VisibilityConstants& constants);
}

#endif
