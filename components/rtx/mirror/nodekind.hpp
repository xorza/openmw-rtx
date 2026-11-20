#pragma once

#include <cstdint>

#include <osg/Object>

#include <components/rtx/common/classanswers.hpp>

namespace Rtx
{
    /// The classes the mirror treats specially. Everything else is walked as a plain node.
    enum class NodeKind : std::uint8_t
    {
        Other,
        Skeleton,
        LightSource,
        ParticleSystem,
        ParticleProcessor,
        ParticleUpdater,
        Sequence,
        RigGeometry,
        MorphGeometry,

        /// A `NiBillboardNode` — `NifOsg::AutoTransform` — which turns to face whoever is looking.
        Billboard,

        /// A `NiLODNode` — `osg::LOD` — whose children are one thing at several budgets.
        Lod,
    };

    /// Answers what kind a node is by its class, learning each class once (`ClassAnswers`). Each class is its own
    /// key, where the literals `META_Object` answers are shared by a subclass that declares none. A class is learned as
    /// the kind it derives from, which is what makes the `static_cast` in `as` sound. One per walking thread.
    class NodeKinds
    {
    public:
        NodeKind of(const osg::Object& object) const;

    private:
        mutable ClassAnswers<NodeKind, 32> mLearned;
    };

    /// `object` as `T` where its kind is `wanted`, else null. A downcast the kind has already
    /// answered for, so it is static.
    template <class T, class Object>
    T* as(const NodeKind held, const NodeKind wanted, Object& object)
    {
        return held == wanted ? static_cast<T*>(&object) : nullptr;
    }
}
