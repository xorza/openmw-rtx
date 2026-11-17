#include "meshdigest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/misc/hashstate.hpp>

#include "mesharrivals.hpp"

namespace Rtx
{
    namespace
    {
        /// One corner of a triangle, as the picture sees it.
        struct Corner
        {
            osg::Vec3f mPosition;
            std::uint32_t mNormal = 0;
            osg::Vec2f mTexCoord;

            /// `Shaders::GpuVertexColour`'s two words, which order as a pair.
            std::array<std::uint32_t, 2> mColour{};
            std::uint32_t mTangent = 0;

            bool operator<(const Corner& other) const
            {
                return std::tie(mPosition, mNormal, mTexCoord, mColour, mTangent)
                    < std::tie(other.mPosition, other.mNormal, other.mTexCoord, other.mColour, other.mTangent);
            }
        };

        void addCorner(Misc::HashState& digest, const Corner& corner)
        {
            digest.add(corner.mPosition);
            digest.add(corner.mNormal);
            digest.add(corner.mTexCoord);
            digest.add(corner.mColour);

            // Only where there is one, so a corner with none digests to the words on record.
            if (corner.mTangent != 0)
                digest.add(corner.mTangent);
        }

        Misc::DigestWords digestTriangles(const MeshGeometry& geometry)
        {
            // Summed, so the order the triangles stand in does not move it.
            Misc::DigestWords triangles{};
            const std::span<const std::uint32_t> indices = geometry.mIndices;
            for (std::size_t at = 0; at + 2 < indices.size(); at += 3)
            {
                std::array<Corner, 3> corners;
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t vertex = indices[at + corner];
                    corners[corner]
                        = Corner{ geometry.mPositions[vertex], geometry.mNormals[vertex], geometry.mTexCoords[vertex],
                              { geometry.mColours[vertex].mRedGreen, geometry.mColours[vertex].mBlue },
                              geometry.mTangents[vertex] };
                }

                const auto least
                    = static_cast<std::size_t>(std::min_element(corners.begin(), corners.end()) - corners.begin());

                Misc::HashState triangle;
                for (std::size_t corner = 0; corner < 3; ++corner)
                    addCorner(triangle, corners[(least + corner) % 3]);
                triangles[0] += triangle.getWords()[0];
                triangles[1] += triangle.getWords()[1];
            }
            return triangles;
        }

        template <class T>
        Misc::DigestWords wordsOf(const std::span<const T> values)
        {
            Misc::HashState digest;
            digest.add(values);
            return digest.getWords();
        }
    }

    MeshDigest digestMesh(const MeshGeometry& geometry)
    {
        Misc::HashState normals;
        normals.add(geometry.mNormals);
        if (std::ranges::any_of(geometry.mTangents, [](const std::uint32_t word) { return word != 0; }))
            normals.add(geometry.mTangents);

        Misc::HashState texCoords;
        texCoords.add(geometry.mTexCoords);
        if (!geometry.mSecondTexCoords.empty())
            texCoords.add(geometry.mSecondTexCoords);

        return MeshDigest{
            .mPositions = wordsOf(geometry.mPositions),
            .mNormals = normals.getWords(),
            .mTexCoords = texCoords.getWords(),
            .mColours = wordsOf(geometry.mColours),
            .mIndices = wordsOf(geometry.mIndices),
            .mTriangles = digestTriangles(geometry),
        };
    }
}
