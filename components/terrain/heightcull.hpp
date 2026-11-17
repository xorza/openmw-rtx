#pragma once

#include <cstdint>
#include <limits>

#include <osg/Referenced>

#include <components/sceneutil/nodecallback.hpp>

namespace osg
{
    class Node;
    class NodeVisitor;
}

namespace Terrain
{
    class HeightCullCallback : public SceneUtil::NodeCallback<HeightCullCallback>
    {
    public:
        void setLowZ(float z) { mLowZ = z; }
        float getLowZ() const { return mLowZ; }

        void setHighZ(float highZ) { mHighZ = highZ; }
        float getHighZ() const { return mHighZ; }

        void setCullMask(std::uint32_t mask) { mMask = mask; }
        std::uint32_t getCullMask() const { return mMask; }

        void operator()(osg::Node* node, osg::NodeVisitor* nv)
        {
            if (mLowZ <= mHighZ)
                traverse(node, nv);
        }

    private:
        float mLowZ{ -std::numeric_limits<float>::max() };
        float mHighZ{ std::numeric_limits<float>::max() };
        std::uint32_t mMask{ ~0u };
    };

}
