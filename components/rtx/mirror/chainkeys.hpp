#pragma once

#include <cstddef>
#include <span>

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
    ///
    /// **And folded again where the frame adopts a reading** (`keyOf`), by this same table: the cell
    /// ring's reader reads a model on its own thread and keeps the links that state anything, so a
    /// chain the ring read and the walk met is one key, and one material.
    class ChainKeys
    {
    public:
        /// The key of a chain keyed `above` — null where nothing on it stated anything yet — once
        /// `link` joins it: `link` itself where it is a controller's, which
        /// `MaterialResolver::animate` keeps one of per placement, or the first link to state
        /// anything; the pair of `above` and `link` where one stated before it; and `above` where
        /// `link` states nothing.
        const osg::StateSet* join(const osg::StateSet* above, const osg::StateSet& link, bool animated, bool states);

        /// The key of a chain whose links that state anything are `stating`, root first, each
        /// joined in turn: what `MaterialResolver::chainOf` keeps of a reading. Null for none.
        const osg::StateSet* keyOf(std::span<const osg::StateSet* const> stating);

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
        /// The key for `stateSet` under the chain keyed `above`, both stating something: the one
        /// object this table holds for the pair.
        const osg::StateSet* under(const osg::StateSet& above, const osg::StateSet& stateSet);

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
