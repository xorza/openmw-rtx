#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/Matrix>
#include <osg/Node>
#include <osg/StateSet>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>

#include "extractionstats.hpp"
#include "materialresolver.hpp"
#include "meshresolver.hpp"
#include "mirroridentity.hpp"
#include "released.hpp"

namespace Rtx
{
    /// The reference roots whose walk met nothing that changes between frames on its own —
    /// `SceneExtractor::Traversal::enter` says what does — and which the world walk passes rather
    /// than descends while their face stands. A frozen root's entries are held, so every sweep it
    /// is not walked in keeps them, and nothing in it is read again until it thaws.
    ///
    /// **A root whose face changed stays thawed until a walk after that finds it standing still**,
    /// its run kept with no keys and the face it was last met at. A door the game turns a step a
    /// frame, or a reference a script moves, would otherwise thaw, freeze again where it stood and
    /// thaw on the next frame: a hold taken and given back on every entry, and a run allocated and
    /// freed, on every frame of the motion.
    class FrozenRoots
    {
    public:
        /// One placement of a frozen subtree, by what the walk resolved it under: the placement's
        /// identity, the drawable its mesh is keyed on and its material's key, each held until the
        /// subtree thaws. A drawable the mesh resolver refused stands nothing, and only its refusal
        /// is held.
        struct Key
        {
            std::size_t mPlacement = 0;
            const osg::Drawable* mDrawable = nullptr;
            const osg::StateSet* mMaterial = nullptr;
            bool mPlaced = false;
        };

        /// What holds a frozen root's keys: the extractor's maps, handed at each call that takes or
        /// gives a hold, which this holds no reference to.
        struct Holders
        {
            Kept<boost::unordered_flat_map<std::size_t, Known>>& mPlacements;
            MeshResolver& mMeshes;
            MaterialResolver& mMaterials;
        };

        /// Room for `budget` roots, made once.
        void reserve(std::size_t budget) { mFrozen.reserve(budget); }

        /// Whether `root`, standing at `world`, is frozen and still what it froze as: counted into
        /// `stats` as walked and passed where it is, and thawed where it is not, for the walk to
        /// descend into it as any other. `walk` is the world walk's number, which the run is
        /// stamped as met by. A thawed root is walked, and its face kept.
        bool pass(const osg::Node& root, const osg::Matrix& world, unsigned int walk, Holders holders,
            ExtractionStats& stats);

        /// Starts recording what the world walk resolves under the root it is about to descend into,
        /// from `instances`, the walk's count of them so far.
        void record(std::uint32_t instances);

        /// Notes what the walk resolved, where a root is being recorded.
        void note(const Key& key)
        {
            if (mRecording)
                mRecorded.push_back(key);
        }

        /// Freezes the root being recorded with what it resolved, where `changeable` is false and
        /// its face did not change on this walk: held from here on, through `holders`. `instances`
        /// is the walk's count so far, which the root's own count is taken from.
        void end(const osg::Node& root, const osg::Matrix& world, bool changeable, unsigned int walk, Holders holders,
            std::uint32_t instances);

        /// Ends a recording a throw left open, so the next walk's root is not recorded inside it.
        void abandon() { mRecording = false; }

        /// Thaws every root the world walk `walk` did not meet — gone from the graph, or masked out —
        /// ahead of the sweep, which would otherwise keep its rows for ever; the root goes to
        /// `released`. Nothing where the walk met every run, which a world standing still does.
        void thawUnmet(unsigned int walk, Holders holders, Released& released);

        /// Thaws every root, into `released`.
        void thawAll(Holders holders, Released& released);

    private:
        /// What can change a frozen subtree from outside it, as its root stands: where the root is
        /// in the world, its state set, and its children, by their count and the first of them.
        struct Face
        {
            osg::Matrix mWorld;
            const osg::StateSet* mStateSet = nullptr;
            const osg::Node* mFirstChild = nullptr;
            unsigned int mChildren = 0;

            static Face of(const osg::Node& root, const osg::Matrix& world);

            bool operator==(const Face& other) const = default;
        };

        /// One root's run of keys and what it was frozen as.
        struct Frozen
        {
            Face mFace;
            Run mKeys;

            /// What its walk counted, which every walk that passes it counts again.
            std::uint32_t mInstances = 0;

            /// The world walk that last met it, and the one that last found its face changed.
            unsigned int mMet = 0;
            unsigned int mMoved = 0;

            /// Whether its keys are held and the walk passes it: false while it stands thawed.
            bool mHolding = false;
        };

        /// Stamps `frozen` as met by `walk`, and counts it among the walk's met.
        void meet(Frozen& frozen, unsigned int walk);

        /// Gives a frozen subtree's holds back, as the walk that thaws it or a sweep that missed it.
        void thaw(Frozen& frozen, Holders holders);

        /// By their roots, held so a root the game freed cannot be mistaken for the one built where
        /// it stood; their keys in one buffer, a run a subtree; and what the root being walked has
        /// resolved so far. Reserved and kept, never freed.
        boost::unordered_flat_map<osg::ref_ptr<const osg::Node>, Frozen, ByAddress<const osg::Node>,
            ByAddress<const osg::Node>>
            mFrozen;
        RunBuffer<Key> mKeys;
        std::vector<Key> mRecorded;

        /// Whether a root is being recorded, and the instance count its walk began at.
        bool mRecording = false;
        std::uint32_t mRecordedFrom = 0;

        /// How many runs the world walk `mMetBy` met, which `thawUnmet` skips its scan by.
        std::size_t mMet = 0;
        unsigned int mMetBy = 0;
    };
}
