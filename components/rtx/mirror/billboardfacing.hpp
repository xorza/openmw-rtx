#pragma once

#include <optional>

#include <osg/Matrixd>

namespace NifOsg
{
    class AutoTransform;
}

namespace Rtx
{
    struct ViewBasis;

    /// Where `billboard` stands in its parent's frame, turned toward `eye` as the rasterizer's cull turns it: the eye,
    /// the look and the up carried into the frame of a parent standing at `parentToWorld`, as a cull stack hands them,
    /// and handed to `NifOsg::AutoTransform::computeMatrixForFrame`. **One turn for every walk**: the frame's own and
    /// the ring's (`CellPlacer`), so a billboard the game loads faces where it faced before its cell loaded. At the
    /// rotation the file was authored with where there is no eye, or the parent's frame has no inverse, a scale of
    /// nought above it, as a visitor that is no cull's finds it.
    osg::Matrixd facingEye(
        const NifOsg::AutoTransform& billboard, const osg::Matrixd& parentToWorld, const std::optional<ViewBasis>& eye);
}
