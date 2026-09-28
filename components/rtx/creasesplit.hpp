#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

namespace Rtx
{
    /// Splits the normals the content smoothed across a hard edge, as a modelling tool's auto-smooth
    /// does, so every normal a mesh ends with describes the surface around it.
    ///
    /// **Why a ray tracer needs it.** A shadow ray leaves from the surface the vertex normals
    /// describe, and not from the flat facet, or a coarse solid shadows itself facet by facet
    /// (`Surface::mLift`). That is sound only where the normals describe a surface — Hanika's own
    /// assumption: a crease is authored as two normals, one for each side. Vanilla content mostly
    /// is not: 94 of every hundred edges two triangles share are smoothed, the right angles of the
    /// furniture included. `furn_de_table_04.nif` carries no split edge at all and 129 smoothed
    /// ones of sixty degrees and more, so its normals describe a dome and its shadow rays would
    /// leave from above it.
    ///
    /// **Only what the content smoothed across an edge of `sHardCosine` and more changes.** At each
    /// position, the corners that carry one authored normal are the artist's smoothing group there.
    /// Where such a group spans a hard edge, it is cut along every hard edge into the pieces its
    /// smooth edges still join, and each piece takes the angle-weighted mean of its own faces
    /// (Thürmer and Wüthrich, "Computing vertex normals from polygonal facets", 1998). A vertex whose
    /// corners fall into two pieces becomes two vertices. A group with no hard edge in it keeps the
    /// normal the content wrote.
    class CreaseSplit
    {
    public:
        /// What a split writes, into the caller's buffers.
        struct Split
        {
            /// One normal a vertex, the added ones last — empty where nothing changed.
            std::vector<osg::Vec3f>& mNormals;

            /// For each vertex past the input's, which of the input's it copies — empty where no
            /// vertex was added.
            std::vector<std::uint32_t>& mSources;
        };

        /// The cosine of the angle between two faces from which the edge they share is hard:
        /// fifty-five degrees, just under the fifty-six the Seyda Neen dry-stone wall smooths its
        /// segments into its cap at. Kept smooth, that crease lit the wall in bands wherever the
        /// cap's lean met the sun. An octagonal prism's forty-five stays round, and a hexagonal
        /// prism's sixty is cut into its faces. Modelling tools default on either side of it —
        /// Blender's auto-smooth at thirty, Unity's importer at sixty. Over the vanilla archives it
        /// moves 1.6 corners in a hundred of the Bitter Coast rocks' and 4.4 of all rocks', none of
        /// the Seyda Neen boulder's, and 62 in a hundred of the tables'.
        static constexpr float sHardCosine = 0.57357644f;

        /// Cuts `normals` along the hard edges of `triangles` over `positions` into `split`, and
        /// rewrites the triangles of every corner that moved to an added vertex.
        void split(std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals,
            std::vector<std::uint32_t>& triangles, Split& split);

    private:
        /// **Made by a `ShapePass` and by nothing else** — `ShapeFold` says why.
        friend class ShapePass;
        CreaseSplit() = default;

        /// An empty slot of the position table, and a corner in no smoothing group.
        static constexpr std::uint32_t sNoEntry = ~std::uint32_t{ 0 };

        /// Gives every vertex the number of its position, equal positions one number: Morrowind
        /// splits a vertex at every UV seam, so a crease is found across copies of one point.
        /// Answers how many positions there are.
        std::uint32_t weld(std::span<const osg::Vec3f> positions);

        /// Cuts `fan` — one position's corners — into pieces and moves every corner of a cut group
        /// to its piece's normal and vertex.
        void cutFan(std::span<const std::uint32_t> fan, std::span<const osg::Vec3f> positions,
            std::span<const osg::Vec3f> normals, std::vector<std::uint32_t>& triangles, Split& split);

        /// The piece a corner of the fan belongs to, the root of its union, with the path halved.
        std::uint32_t pieceOf(std::uint32_t corner);

        /// Each triangle's unit normal, nought for one with no area.
        std::vector<osg::Vec3f> mFaces;

        /// Each vertex's position number, and the table that numbers them: open addressed, a power
        /// of two long and never more than half full.
        std::vector<std::uint32_t> mPositionOf;
        std::vector<std::uint32_t> mTable;

        /// Every corner of every triangle, as its index into the triangle list, sorted by position:
        /// one run a position, with a trailing end, and where the sort writes each run's next.
        std::vector<std::uint32_t> mFanStarts;
        std::vector<std::uint32_t> mFanCorners;
        std::vector<std::uint32_t> mCursor;

        /// The triangles as they came, which the geometry is read off while the moved corners are
        /// written into the caller's.
        std::vector<std::uint32_t> mOriginal;

        /// What a hard edge did to a smoothing group: nothing, found one in it, or cut it in pieces.
        enum class GroupState : std::uint8_t
        {
            Smooth,
            Hard,
            Cut,
        };

        /// The fan being cut, by its corners' order in it: each corner's group, named by the group's
        /// first corner; each one's union parent; each group's state, at its first corner; and what
        /// each piece's normal sums to, at its root.
        std::vector<std::uint32_t> mGroup;
        std::vector<std::uint32_t> mParent;
        std::vector<GroupState> mGroupState;
        std::vector<osg::Vec3f> mPieceNormal;

        /// Which vertex each vertex of the fan went to for each piece: the input's for the first
        /// piece it met, an added one for every other.
        struct Placed
        {
            std::uint32_t mVertex;
            std::uint32_t mPiece;
            std::uint32_t mPlacedAt;
        };
        std::vector<Placed> mPlaced;
    };
}
