#include "nodekind.hpp"

#include <osg/LOD>
#include <osg/Sequence>
#include <osgParticle/ParticleProcessor>
#include <osgParticle/ParticleSystem>
#include <osgParticle/ParticleSystemUpdater>
#include <typeinfo>

#include <components/nifosg/autotransform.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/morphgeometry.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/skeleton.hpp>

namespace Rtx
{
    namespace
    {
        NodeKind classify(const osg::Object& object)
        {
            if (dynamic_cast<const SceneUtil::Skeleton*>(&object) != nullptr)
                return NodeKind::Skeleton;
            if (dynamic_cast<const SceneUtil::LightSource*>(&object) != nullptr)
                return NodeKind::LightSource;
            if (dynamic_cast<const osgParticle::ParticleSystem*>(&object) != nullptr)
                return NodeKind::ParticleSystem;
            if (dynamic_cast<const osgParticle::ParticleProcessor*>(&object) != nullptr)
                return NodeKind::ParticleProcessor;
            if (dynamic_cast<const osgParticle::ParticleSystemUpdater*>(&object) != nullptr)
                return NodeKind::ParticleUpdater;
            if (dynamic_cast<const osg::Sequence*>(&object) != nullptr)
                return NodeKind::Sequence;
            if (dynamic_cast<const SceneUtil::RigGeometry*>(&object) != nullptr)
                return NodeKind::RigGeometry;
            if (dynamic_cast<const SceneUtil::MorphGeometry*>(&object) != nullptr)
                return NodeKind::MorphGeometry;
            if (dynamic_cast<const NifOsg::AutoTransform*>(&object) != nullptr)
                return NodeKind::Billboard;
            if (dynamic_cast<const osg::LOD*>(&object) != nullptr)
                return NodeKind::Lod;

            return NodeKind::Other;
        }
    }

    NodeKind NodeKinds::of(const osg::Object& object) const
    {
        const std::type_info* const type = &typeid(object);

        for (std::size_t at = 0; at < mHeld; ++at)
            if (mLearned[at].mClass == type)
                return mLearned[at].mKind;

        return learn(object);
    }

    NodeKind NodeKinds::learn(const osg::Object& object) const
    {
        const NodeKind kind = classify(object);

        if (mHeld < mLearned.size())
        {
            mLearned[mHeld] = Learned{ &typeid(object), kind };
            ++mHeld;
        }
        else
            ++mOverflow;

        return kind;
    }
}
