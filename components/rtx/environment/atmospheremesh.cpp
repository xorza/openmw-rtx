#include "atmospheremesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <osg/Geometry>
#include <osg/NodeVisitor>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <components/misc/result.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sky/vertexrules.hpp>
#include <components/vfs/manager.hpp>

#include "skymesh.hpp"

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

        /// Every vertex of the mesh into the ring its alpha puts it in, placed where the graph puts
        /// it (`placedVertices`).
        class RingReader : public osg::NodeVisitor
        {
        public:
            RingReader()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Geometry& geometry) override
            {
                mPlaced.clear();
                if (!placedVertices(geometry, getNodePath(), mPlaced))
                    return;

                for (std::size_t i = 0; i < mPlaced.size(); ++i)
                    (Sky::atmosphereAlphaOf(i) > 0.0f ? mUpper : mLower).add(mPlaced[i]);
            }

            RingSum mUpper;
            RingSum mLower;

        private:
            std::vector<osg::Vec3f> mPlaced;
        };
    }

    Atmosphere readAtmosphere(const osg::Node& mesh)
    {
        // OSG's visitor API is non-const throughout, and this walk writes nothing.
        RingReader read;
        const_cast<osg::Node&>(mesh).accept(read);

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

        return readAtmosphere(*scenes.getTemplate(mesh, false));
    }
}
