#pragma once

#include <osg/Drawable>
#include <osg/Node>
#include <osgUtil/CullVisitor>

#include "nodekind.hpp"

namespace Rtx
{
    /// A cull traversal that culls nothing and draws nothing, for the one thing left that answers
    /// only to an `osgUtil::CullVisitor`: `SceneUtil::RigGeometry` and `SceneUtil::MorphGeometry`
    /// skin inside `accept` by casting the visitor to one, and a CPU intersection reads that posed
    /// copy. Not for a whole graph: `Terrain::TerrainDrawable::cull` puts the chunk in a render bin
    /// and never applies it. A real `CullVisitor`, because those casts are unchecked. Whoever uses
    /// it owes it a frame stamp — `SceneUtil::FrameTimeSource` reads the simulation time off it
    /// unchecked — and a traversal number a skeleton has not seen.
    class PoseCull : public osgUtil::CullVisitor
    {
    public:
        PoseCull();

        /// The pose is read off the drawable afterwards, so there is nothing to do with it here.
        void apply(osg::Drawable&) override {}

        /// Everything but a particle simulation, which a real cull visitor would otherwise run:
        /// `osgParticle` keeps a once-per-frame guard and a `_t0` per processor, so two visitors
        /// on two clocks difference a `_t0` from one against a time from the other.
        /// Each `SceneExtractor` owns the emitter clock of what it walks.
        void apply(osg::Node& node) override;

    private:
        NodeKinds mKinds;
    };
}
