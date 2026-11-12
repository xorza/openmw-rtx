#pragma once

#include <osg/Vec2f>
#include <osg/Vec3f>

namespace Rtx
{
    /// The surface Morrowind's cloud deck is painted on, read off the mesh the rasterizer draws: a cap of
    /// radius 1000 falling from 292 above the eye to 85 at its rim. A plane and a cap disagree about where
    /// a direction lands (at fifteen degrees the cap's tiles are twice a plane's), which is why a Morrowind
    /// sky reads as a few great clouds. It is `z = h - k r²` with `k = 1 / 2R`, so the two numbers are a
    /// height and a curvature. Read as ratios, so the scale cancels, because the mesh is content.
    struct CloudShell
    {
        /// How high the layer hangs over the eye, in texture tiles along each of the sheet's axes. Signed:
        /// Morrowind's `v` is the negative one, or the clouds drift *into* the storm.
        osg::Vec2f mTiles;

        /// How far the layer falls away from the eye, as `k · h`, a pure number. Nought is a flat plane.
        float mCurvature = 0.0f;

        /// Where the engine's own fade sits, as three radii in texture tiles: the last carrying a whole
        /// deck, the one carrying a quarter, and the rim past which there is none (linear in radius between
        /// rings, `ModVertexAlphaVisitor::Clouds`). On Morrowind's own cap 1.17, 1.72 and 2.50 tiles.
        osg::Vec3f mRings;
    };
}
