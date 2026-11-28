#include "billboardfacing.hpp"

#include <osg/Vec3d>

#include <components/nifosg/autotransform.hpp>
#include <components/rtx/frame/camera.hpp>

namespace Rtx
{
    osg::Matrixd facingEye(
        const NifOsg::AutoTransform& billboard, const osg::Matrixd& parentToWorld, const std::optional<ViewBasis>& eye)
    {
        osg::Matrixd toLocal;
        if (!eye.has_value() || !toLocal.invert(parentToWorld) || !toLocal.valid())
            return billboard.computeMatrix(nullptr);

        const osg::Vec3d eyeLocal = osg::Vec3d(eye->mOrigin) * toLocal;
        const osg::Vec3d lookLocal = osg::Matrixd::transform3x3(osg::Vec3d(eye->mForward), toLocal);
        const osg::Vec3d upLocal = osg::Matrixd::transform3x3(osg::Vec3d(eye->mUp), toLocal);
        return billboard.computeMatrixForFrame(eyeLocal, lookLocal, upLocal);
    }
}
