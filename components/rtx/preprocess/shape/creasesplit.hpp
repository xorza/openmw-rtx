#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "probetable.hpp"

namespace Rtx
{
    /// Splits the normals the content smoothed across a hard edge, as a modelling tool's auto-smooth
    /// does, and weighs what stays smooth by its faces' areas, as face-weighted normals do, so every
    /// normal a mesh ends with describes the surface around it.
    ///
    /// **Why a ray tracer needs it.** A shadow ray leaves from the surface the vertex normals
    /// describe, and not from the flat facet, or a coarse solid shadows itself facet by facet. That
    /// is sound only where the normals describe a surface: a crease is authored as two normals, one
    /// for each side. Vanilla content mostly is not: 94 of every hundred edges two triangles share
    /// are smoothed, the right angles of the furniture included, so a table's normals describe a dome
    /// and its shadow rays would leave from above it.
    ///
    /// **Every smoothing group that bends takes its faces' weighted mean.** At each position, the
    /// corners that carry one authored normal are the artist's smoothing group there. Where its
    /// faces stand in more than one plane, it is cut along every edge of `sHardCosine` and more into
    /// the pieces its smooth edges still join, and each piece takes the mean of its own faces, each
    /// weighed by its corner angle (Thürmer and Wüthrich, "Computing vertex normals from polygonal
    /// facets", 1998) and by its area (Blender's Weighted Normal modifier), so a broad face stays
    /// flat and the narrow bevel beside it carries the bend: the content chamfers its boxes and
    /// smooths them whole. A vertex whose corners fall into two pieces becomes two vertices.
    ///
    /// **A group whose faces are one plane keeps the normal the content wrote**: a normal bent off a
    /// flat card is bent on purpose.
    ///
    /// **Across the shapes of a model, and not only inside one.** A Morrowind model is one shape a
    /// texture, and its exporter smoothed the normals over the whole model, so a floor's corners along
    /// a wall carry the mean of the two across a right angle. So the faces the model holds beside the
    /// mesh (`Beside`) join its smoothing groups, find their hard edges and weigh in their pieces'
    /// normals, and only the mesh's own corners are written.
    class CreaseSplit
    {
    public:
        /// The faces a mesh's model holds beside it, in the mesh's own space: the other shapes'
        /// positions, the normals the content wrote them, and their triangles over those positions.
        struct Beside
        {
            std::span<const osg::Vec3f> mPositions;
            std::span<const osg::Vec3f> mNormals;
            std::span<const std::uint32_t> mTriangles;
        };

        /// What a split writes, into the caller's buffers.
        struct Split
        {
            /// One normal a vertex, the added ones last — empty where nothing changed.
            std::vector<osg::Vec3f>& mNormals;

            /// For each vertex past the input's, which of the input's it copies — empty where no
            /// vertex was added.
            std::vector<std::uint32_t>& mSources;
        };

        /// The cosine of the angle between two faces from which the edge they share is hard: fifty-five
        /// degrees, just under the fifty-six the Seyda Neen dry-stone wall smooths its segments into its
        /// cap at, which lit the wall in bands where kept smooth. An octagonal prism's forty-five stays
        /// round, and a hexagonal prism's sixty is cut into its faces.
        static constexpr float sHardCosine = 0.57357644f;

        /// The cosine of the angle within which two faces are one plane: one degree. A flat
        /// polygon's triangles part by the rounding of their corners' floats, a millionth of a
        /// radian, and a curve the content smoothed bends by more than a degree a face up to 360
        /// faces round.
        static constexpr float sPlaneCosine = 0.99984769f;

        /// Cuts `normals` along the hard edges of `triangles` over `positions`, and of the faces
        /// `beside` them, into `split`, and rewrites the triangles of every corner that moved to an
        /// added vertex.
        void split(std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals,
            std::vector<std::uint32_t>& triangles, const Beside& beside, Split& split);

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

        /// Cuts `fan` — one position's corners — into pieces and moves every one of the mesh's own
        /// corners in a cut group to its piece's normal and vertex. `positions` and `normals` hold
        /// the mesh's `ownVertices` and then those beside it.
        void cutFan(std::span<const std::uint32_t> fan, std::span<const osg::Vec3f> positions,
            std::span<const osg::Vec3f> normals, std::size_t ownVertices, std::vector<std::uint32_t>& triangles,
            Split& split);

        /// The piece a corner of the fan belongs to, the root of its union, with the path halved.
        std::uint32_t pieceOf(std::uint32_t corner);

        /// Each triangle's unit normal, nought for one with no area, and twice its area.
        std::vector<osg::Vec3f> mFaces;
        std::vector<float> mTwiceAreas;

        /// Each vertex's position number, and the table that numbers them.
        std::vector<std::uint32_t> mPositionOf;
        ProbeTable mTable;

        /// Every corner of every triangle, as its index into the triangle list, sorted by position:
        /// one run a position, with a trailing end, and where the sort writes each run's next.
        std::vector<std::uint32_t> mFanStarts;
        std::vector<std::uint32_t> mFanCorners;
        std::vector<std::uint32_t> mCursor;

        /// The triangles as they came, the mesh's own and then those beside it over the vertices
        /// past its own, which the geometry is read off while the moved corners are written into
        /// the caller's.
        std::vector<std::uint32_t> mOriginal;

        /// How many of `mOriginal`'s corners are the mesh's own, which alone are written.
        std::size_t mOwnCorners = 0;

        /// The mesh's positions and normals and then those beside it, where anything is beside it.
        std::vector<osg::Vec3f> mPositions;
        std::vector<osg::Vec3f> mNormals;

        /// Whether a smoothing group's faces stand in one plane, which keeps what the content wrote,
        /// or bend, which reweighs it.
        enum class GroupState : std::uint8_t
        {
            Flat,
            Bent,
        };

        /// The fan being cut, by its corners' order in it: each corner's group, named by the group's
        /// first corner; each one's union parent; each group's state and the plane of its first face,
        /// at its first corner; and what each piece's normal sums to, at its root.
        std::vector<std::uint32_t> mGroup;
        std::vector<std::uint32_t> mParent;
        std::vector<GroupState> mGroupState;
        std::vector<osg::Vec3f> mGroupPlane;
        std::vector<osg::Vec3f> mPieceNormal;

        /// A corner of the fan under a key of two numbers, sorted by both and then by its place:
        /// its group and a neighbouring position, to find the corners across an edge, or its vertex
        /// and piece, to find where it was placed.
        struct Keyed
        {
            std::uint32_t mFirst;
            std::uint32_t mSecond;
            std::uint32_t mAt;

            auto operator<=>(const Keyed& other) const = default;
        };
        std::vector<Keyed> mKeyed;

        /// The fan's corners that carry a normal, sorted by it.
        std::vector<std::uint32_t> mOrder;

        /// For each cut corner of the fan: the first corner of its vertex and piece, the first corner
        /// of its vertex, and — at a corner that is the first of its vertex and piece — the vertex
        /// it went to, the input's for the vertex's first piece and an added one for every other.
        std::vector<std::uint32_t> mPlacedBy;
        std::vector<std::uint32_t> mVertexFirst;
        std::vector<std::uint32_t> mPlacedAt;
    };
}
