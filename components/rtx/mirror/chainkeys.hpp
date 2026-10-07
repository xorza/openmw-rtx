#pragma once

#include <cstddef>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/StateSet>
#include <osg/ref_ptr>

namespace Rtx
{
    /// What a material is held under: the chain of state sets that say anything at a drawable, as
    /// one object whose address is the identity.
    ///
    /// **The chain and not its nearest link.** A NIF puts an `NiNode`'s own properties on that
    /// node's state set, and `SceneUtil::SharedStateManager` makes equal state sets one object
    /// across files, so one shape's state set stands under parents that name other textures and
    /// other sidedness. Keyed on the shape's alone, the first chain met decided the material for
    /// every other.
    ///
    /// **Folded as the chain is built** (`Shading::under`), a link at a time: a link that states
    /// nothing keeps the key above it; the first that states something is its own key, which is
    /// the common chain of one; and each one after it is keyed on the pair of the key above and
    /// itself, an object this table makes the first time it meets the pair and finds after that.
    class ChainKeys
    {
    public:
        /// The key for `stateSet` under the chain keyed `above`, both stating something: the one
        /// object this table holds for the pair.
        const osg::StateSet* under(const osg::StateSet& above, const osg::StateSet& stateSet);

        /// Drops every pair whose key nothing but this table holds, and the state sets it held with
        /// it. A material's entry holds its key, and a pair holds the key above it, so a chain goes
        /// once nothing is keyed on it, the nearest pair first.
        void retire();

        /// Room for `count` pairs before the table rehashes, which no frame of a walk should pay.
        void reserve(std::size_t count) { mKeys.reserve(count); }

        /// Drops every pair, for a walk whose keys its readings hold.
        void clear() { mKeys.clear(); }

        std::size_t size() const { return mKeys.size(); }

    private:
        struct Pair
        {
            const osg::StateSet* mAbove;
            const osg::StateSet* mOwn;

            bool operator==(const Pair& other) const = default;
        };

        struct PairHash
        {
            std::size_t operator()(const Pair& pair) const;
        };

        /// The pair's two state sets held, so neither address can be handed to another state set
        /// while the pair stands: `ByAddress` says why.
        struct Held
        {
            osg::ref_ptr<const osg::StateSet> mAbove;
            osg::ref_ptr<const osg::StateSet> mOwn;
            osg::ref_ptr<const osg::StateSet> mKey;
        };

        boost::unordered_flat_map<Pair, Held, PairHash> mKeys;
    };
}
