#include "atmosphere.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Matrixf>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osg/Vec3d>

#include <components/misc/result.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sky/vertexrules.hpp>
#include <components/vfs/manager.hpp>

namespace Rtx
{
    namespace
    {
        /// The radius and height of every vertex of one ring, summed, as the graph places them.
        struct RingSum
        {
            double mRadius = 0.0;
            double mHeight = 0.0;
            std::size_t mCount = 0;

            void add(const osg::Vec3f& placed)
            {
                mRadius += std::hypot(double{ placed.x() }, double{ placed.y() });
                mHeight += double{ placed.z() };
                ++mCount;
            }
        };

        /// Every vertex of the mesh into the ring its alpha puts it in. Placed, because Morrowind's
        /// atmosphere hangs upside down under a root rotation of `diag(1, -1, -1)`.
        class RingReader : public osg::NodeVisitor
        {
        public:
            RingReader()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Geometry& geometry) override
            {
                const auto* vertices = dynamic_cast<const osg::Vec3Array*>(geometry.getVertexArray());
                if (vertices == nullptr)
                    return;

                const osg::Matrixf placed = osg::computeLocalToWorld(getNodePath());
                for (std::size_t i = 0; i < vertices->size(); ++i)
                    (Sky::atmosphereAlphaOf(i) > 0.0f ? mUpper : mLower).add(placed.preMult((*vertices)[i]));
            }

            RingSum mUpper;
            RingSum mLower;
        };

        /// The antiderivative of `sin e cos e · Q'(e) / Q(e)` for `Q = C cos e - D sin e`, written
        /// through `Q = R cos(e + φ)`: `-cos 2φ · e / 2 + sin 2e / 4 + sin 2φ · ln(Q / R) / 2`.
        double tiltedLog(double e, double c, double d)
        {
            const double r2 = c * c + d * d;
            const double q = c * std::cos(e) - d * std::sin(e);
            return -0.5 * (c * c - d * d) / r2 * e + 0.25 * std::sin(2.0 * e)
                + (c * d) / r2 * std::log(q / std::sqrt(r2));
        }
    }

    float zenithShareOf(const Shaders::SkyRamp& ramp)
    {
        // The share is `t = (A s - B c) / (C c - D s)` between the rings, `s = sin e`, `c = cos e`,
        // and the cosine-weighted mean is `2 ∫ z t dz = 2 ∫ s c t de`. Writing the numerator as
        // `α Q + β Q'` over the denominator `Q` makes `t = α + β Q' / Q`, whose two terms integrate
        // to `α s² / 2` and `β · tiltedLog`.
        const double a = ramp.mLow.x();
        const double b = ramp.mLow.y();
        const double d = ramp.mStep.x();
        const double c = ramp.mStep.y();
        const double r2 = c * c + d * d;
        const double alpha = (-a * d - b * c) / r2;
        const double beta = (b * d - a * c) / r2;

        // Below the horizon is no part of the sky a surface facing it receives.
        const double bottom = std::clamp(double{ ramp.mBottom }, 0.0, 1.0);
        const double top = std::clamp(double{ ramp.mTop }, bottom, 1.0);
        const double from = std::asin(bottom);
        const double to = std::asin(top);

        const double wall
            = alpha * (top * top - bottom * bottom) + 2.0 * beta * (tiltedLog(to, c, d) - tiltedLog(from, c, d));
        return float(1.0 - top * top + wall);
    }

    Atmosphere readAtmosphere(osg::Node& mesh)
    {
        RingReader read;
        mesh.accept(read);

        if (read.mUpper.mCount == 0 || read.mLower.mCount == 0)
            return Atmosphere{};

        const double lowRadius = read.mLower.mRadius / double(read.mLower.mCount);
        const double lowHeight = read.mLower.mHeight / double(read.mLower.mCount);
        const double highRadius = read.mUpper.mRadius / double(read.mUpper.mCount);
        const double highHeight = read.mUpper.mHeight / double(read.mUpper.mCount);

        // A fade runs up the wall, so the upper ring stands at a higher elevation than the lower.
        // Anything else is not the cylinder the engine's rule is written for.
        const double bottom = lowHeight / std::hypot(lowRadius, lowHeight);
        const double top = highHeight / std::hypot(highRadius, highHeight);
        if (!(lowRadius > 0.0) || !(highRadius > 0.0) || !(top > bottom))
            return Atmosphere{};

        const Shaders::SkyRamp ramp{
            .mBottom = float(bottom),
            .mTop = float(top),
            .mLow = osg::Vec2f(float(lowRadius), float(lowHeight)),
            .mStep = osg::Vec2f(float(highRadius - lowRadius), float(highHeight - lowHeight)),
        };
        return Atmosphere{ .mRamp = ramp, .mZenithShare = zenithShareOf(ramp) };
    }

    Misc::Result<Atmosphere, std::string> readAtmosphere(Resource::SceneManager& scenes, VFS::Path::NormalizedView mesh)
    {
        if (!scenes.getVFS()->exists(mesh))
            return Misc::Err{ "the archives hold no such file" };

        return readAtmosphere(const_cast<osg::Node&>(*scenes.getTemplate(mesh, false)));
    }
}
