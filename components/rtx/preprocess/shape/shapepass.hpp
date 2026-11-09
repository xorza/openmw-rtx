#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

#include "creasesplit.hpp"
#include "shapefold.hpp"

namespace Rtx
{
    /// What a drawable's triangles come to, as a pass: the reversed twins and pockets folded away
    /// (`ShapeFold`), and then the normals the content smoothed across a hard edge split and the
    /// rest weighed by their faces (`CreaseSplit`) on what survived. One pass and not two, because
    /// the split reads the folded triangles and a key over one input is one entry a cache keeps.
    class ShapePass
    {
    public:
        struct Input
        {
            std::span<const osg::Vec3f> mPositions;

            /// Empty where the drawable names none, which nothing is split for.
            std::span<const osg::Vec3f> mNormals{};

            /// As the drawable named them.
            std::span<const std::uint32_t> mTriangles;

            /// Whether the normals may be split. Not for a mesh that deforms, whose skin or morph
            /// targets are one per vertex the content wrote: a skinned body is round, and no copy
            /// of a vertex would be posed.
            bool mSplits = false;

            /// The faces the drawable's model holds beside it, which the split cuts across and
            /// never writes — `CreaseSplit::Beside`. Empty where nothing stands beside it.
            CreaseSplit::Beside mBeside{};
        };

        struct Output
        {
            /// The triangles the fold kept, in the order they were named, over the vertices the
            /// split left — added ones past the input's.
            std::vector<std::uint32_t>& mKept;

            /// `CreaseSplit::Split`'s two: the normals where any changed, and what each added vertex
            /// copies.
            std::vector<osg::Vec3f>& mNormals;
            std::vector<std::uint32_t>& mSources;

            FoldedShape mShape{};
        };

        static constexpr ContentPassId sPass = ContentPassId::Shape;
        static constexpr std::uint32_t sVersion = 4;
        static constexpr bool sStored = true;

        static void digest(const Input& input, ContentDigest& digest);
        void run(const Input& input, Output& output);

        /// Whether a stored `output` can be `input`'s: whole triangles, every kept corner one of the
        /// input's vertices or an added one, every added vertex a copy of an input's, and normals for
        /// none or for every vertex. What the mesh table writes and reads past a mesh's run where it is
        /// not (`ContentCache::find`).
        static bool fits(const Input& input, const Output& output);

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why.
        friend class ContentPreprocessor;
        ShapePass() = default;

        ShapeFold mFold;
        CreaseSplit mSplit;
    };
}
