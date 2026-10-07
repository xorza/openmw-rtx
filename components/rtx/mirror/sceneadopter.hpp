#pragma once

#include <span>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/specularlayout.hpp>

#include "materialresolver.hpp"

namespace osg
{
    class Drawable;
    class StateSet;
}

namespace Rtx
{
    struct ExtractionStats;
    struct MeshReading;
    class SceneDesc;

    /// What a residency may do inside the walk that asks it: adopt rows through the mirror's own
    /// resolvers, under the identity the walk would find a clone's mesh under, so that a mesh both
    /// stand is one mesh. An adoption is a hold and a release gives it back, so the sweep keeps a
    /// held entry whatever its stamp.
    ///
    /// **The one route a residency has to the scene**: the rows it adopts, the placements it stands
    /// and the counts it adds all go where this says, so a residency cannot stand rows in one scene
    /// and adopt them into another.
    class SceneAdopter
    {
    public:
        virtual ~SceneAdopter() = default;

        SceneAdopter(const SceneAdopter&) = delete;
        SceneAdopter& operator=(const SceneAdopter&) = delete;

        /// The material of a reading somebody else made, with one hold taken on it, and the key it
        /// is held under: `chain` keyed by the walk's own table (`ChainKeys::keyOf`), so a chain the
        /// walk meets as well is one material. `sNoIndex`, a null key and no hold for an empty
        /// chain.
        virtual MaterialResolver::Resolved adoptMaterial(
            const MaterialReading& reading, std::span<const osg::StateSet* const> chain)
            = 0;

        /// The same for a mesh, held under the identity the walk will find a clone's mesh under.
        virtual Index adoptMesh(const osg::Drawable& drawable, const MeshReading& reading) = 0;

        /// Gives one hold back on what `adoptMesh` held under `drawable`.
        virtual void releaseMesh(const osg::Drawable& drawable) = 0;

        /// The same for `adoptMaterial`, by the key it handed back. Nothing for null.
        virtual void releaseMaterial(const osg::StateSet* key) = 0;

        /// The scene every adoption lands in.
        virtual SceneDesc& getScene() = 0;

        /// What the content's `_spec` maps mean on the adopter's thread — `WalkContext::mSpecular`.
        virtual SpecularLayout getSpecularLayout() const = 0;

        /// The counts of the walk in progress. Only inside a walk, which is the only time a
        /// residency stands anything to count.
        virtual ExtractionStats& getStats() = 0;

    protected:
        SceneAdopter() = default;
    };
}
