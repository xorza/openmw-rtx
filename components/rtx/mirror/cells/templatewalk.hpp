#pragma once

#include <string>
#include <vector>

#include <osg/Matrix>
#include <osg/Node>
#include <osg/NodeVisitor>

#include <components/rtx/common/result.hpp>
#include <components/rtx/mirror/meshreader.hpp>
#include <components/rtx/mirror/nodekind.hpp>
#include <components/rtx/mirror/shading.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/meantexels.hpp>

#include "nightday.hpp"

namespace osg
{
    class Drawable;
    class StateSet;
    class Transform;
}

namespace Rtx
{
    struct PreparedModel;

    /// A read-only walk over a model as the loader built it, into a `PreparedModel`, for a thread
    /// that is not the frame's. Not `SceneExtractor::Traversal`, because that one steps sequences
    /// and runs controllers, and a template is shared with every clone and the preloader, so a walk
    /// from another thread may write nothing into it. `SceneUtil::CopyOp` shares the drawables,
    /// state sets and transforms, so a mesh read here is the mesh the frame's walk finds under the
    /// clone. A sequence is walked at the frame it stands on, an LOD at its nearest level:
    /// `descendInWorld` is the rule both walks share. **Save a `NightDaySwitch`, whose every branch
    /// is read**, each part marked with the modes it is shown in: the game sets a clone's switch by
    /// the hour, and nothing sets the template's, which stays at the branch its file opens on. Not
    /// thread-safe: one a thread.
    class TemplateWalk final : public osg::NodeVisitor
    {
    public:
        TemplateWalk();

        /// Walks `root` and appends one part to `into` for every drawable under it that holds a
        /// triangle, with its arrays appended to the model's buffers. An error for the first
        /// drawable this cannot take, saying why, which refuses the model whole; what `into` holds
        /// then is its caller's to drop.
        ///
        /// @param mask which nodes the walk may descend into — the same `osg` traversal mask the
        ///        frame's walk carries, so the two reach the same drawables.
        Result<void, std::string> read(const osg::Node& root, osg::Node::NodeMask mask, PreparedModel& into);

        /// What the reads computed from the content since the last take — `ContentPreprocessor`.
        ContentStats takeStats() { return mContent.takeStats(); }

        void apply(osg::Node& node) override;
        void apply(osg::Transform& node) override;
        void apply(osg::Drawable& drawable) override;

    private:
        /// Descends into the children of `node` that are in the world, running no clock on the way,
        /// and into every branch of a day-night switch.
        void descend(osg::Node& node);

        /// Puts `stateSet` at the near end of the chain, with the fade resolved through it.
        void pushShading(const osg::StateSet& stateSet);

        /// Reads the drawable the walk is standing at into a part of `mInto`, under the state sets
        /// in force at it.
        void take(const osg::Drawable& drawable);

        PreparedModel* mInto = nullptr;

        ContentPreprocessor mContent;
        MeshReader mMeshes{ mContent };
        MeanTexels mMeans{ mContent };

        /// This thread's own classifier: `NodeKinds` is written on a miss.
        NodeKinds mKinds;

        /// The local-to-template of the node being visited. Accumulated in the width
        /// `computeLocalToWorldMatrix` works in, and narrowed where a drawable is handed over, as
        /// the frame's walk narrows it.
        osg::Matrix mHere;

        /// The modes the day-night switches above where the walk is standing show it in.
        NightDayModes mModes;

        /// The state sets in force where the walk is standing, nearest last. Kept across walks and
        /// refilled, because a model is hundreds of drawables and the thread reads thousands.
        std::vector<Shading> mShading;

        /// Why the first drawable this walk could not take was refused, or empty.
        std::string mRefused;
    };
}
