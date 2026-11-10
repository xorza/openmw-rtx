#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/scene/foldedshape.hpp>

#include "pockettree.hpp"
#include "probetable.hpp"

namespace Rtx
{
    /// Folds the reversed twin every sheet in the game is doubled with back into one triangle, and
    /// says what the shape was. Morrowind has no two-sided flag, so the content draws a card's
    /// back by modelling it: a second triangle over the same three positions, wound the other way,
    /// with vertices of its own — 3670 shapes in the game are nothing but such pairs. Every ray
    /// that carries light meets both faces of everything, so it meets both copies at the same depth
    /// and light passing through the card would be taken off twice. A shape that was nothing but
    /// pairs is a sheet, which is what lets a leaf carry the light that falls on its far side.
    ///
    /// **And a pocket's second wall goes too**, which is the same card modelled less exactly. The
    /// content also builds cloth and plaster as a thin shell whose two sheets cross, so that in
    /// places the back sheet stands a few units in front of the front one and faces it: a gap that
    /// is inside out, whose two walls each face into it. The rasterizer culls each wall from the
    /// side the other is seen from, so it only ever shows one; a ray that leaves either wall meets
    /// the other face on — the ship's sail went black wherever a bounce left it. Such a pair is
    /// the card's two faces, and the fold keeps the wall the file wrote first, as it does for a
    /// twin. What tells a pocket from a slot, whose walls face each other the same way, is the
    /// generalized winding number at the gap's middle (Jacobson, Kavan and Sorkine-Hornung,
    /// "Robust Inside-Outside Segmentation using Generalized Winding Numbers", 2013): minus one
    /// inside out, nought in a slot, which is outside the solid.
    class ShapeFold
    {
    public:
        /// Drops the second of every reversed pair and then the later wall of every pocket,
        /// compacting `indices` in place, and says what the shape came to. A twin is matched by
        /// exact equality of positions, because it is a copy and not a remodel; a copy wound the
        /// same way is not a twin and is left. No allocation per triangle, because a cell crossing
        /// folds tens of thousands of triangles a second.
        FoldedShape fold(std::span<const osg::Vec3f> positions, std::vector<std::uint32_t>& indices);

    private:
        /// **Made by a `ShapePass` and by nothing else**, so the one way to fold is the one that
        /// keys and counts it.
        friend class ShapePass;
        ShapeFold() = default;

        /// One triangle's corners, rotated so the least comes first: the winding survives and where
        /// the file happened to start the triangle does not, which is what lets two spellings of one
        /// triangle compare equal.
        struct Corners
        {
            osg::Vec3f mCorner[3];

            bool operator==(const Corners& other) const;

            /// Over the corner bits, with a zero normalised: -0 and 0 compare equal above, and a
            /// spelling that hashed the two apart would never find its twin.
            std::size_t hash() const;
        };

        enum class Fate : std::uint8_t
        {
            Alone,
            Kept,
            Dropped,
        };

        /// An empty table slot, and the end of a chain.
        static constexpr std::uint32_t sNoEntry = ProbeTable::sEmpty;

        static Corners canonical(const osg::Vec3f& a, const osg::Vec3f& b, const osg::Vec3f& c);

        /// Drops the later wall of every pocket in `indices`, compacting it in place, and says
        /// whether any went. See the class's own doc.
        bool dropPockets(std::span<const osg::Vec3f> positions, std::vector<std::uint32_t>& indices);

        /// The triangle a spelling is held under, or `sNoEntry`. The slot holds the chain's head,
        /// because a chunk with four copies of a card pairs them two by two.
        ProbeTable mTable;

        /// Each triangle's spelling and its hash, so a probe compares an index rather than
        /// recomputing corners it has already rotated once.
        std::vector<Corners> mSpelling;
        std::vector<std::size_t> mHashes;

        /// The next triangle sharing a spelling, indexed by triangle, ascending — so a pair keeps
        /// the copy the file wrote first. `sNoEntry` ends a chain.
        std::vector<std::uint32_t> mNext;

        std::vector<Fate> mFates;

        PocketTree mTree;

        /// `dropPockets`'s own, kept between shapes: how many of a triangle's samples opened on a
        /// pocket, the walls across from each triangle as one run per triangle with a trailing end,
        /// and which triangles went.
        std::vector<std::uint8_t> mPocketSamples;
        std::vector<std::uint32_t> mAcrossStarts;
        std::vector<std::uint32_t> mAcross;
        std::vector<std::uint8_t> mPocketDropped;
    };
}
