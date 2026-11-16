#include "mesh.hpp"

#include <components/misc/hashstate.hpp>

namespace Rtx
{
    MeshKey meshKeyOf(const MeshArrays& arrays, const FoldedShape shape)
    {
        static_assert(sizeof(FoldedShape) == 3, "a fact of the shape the key does not digest");

        // Each array a step of its own, so where one ends and the next begins is part of the key, and an empty one
        // moves the state as any other does (`Misc::HashState::add`).
        Misc::HashState hash;
        hash.add(arrays.mPositions);
        hash.add(arrays.mNormals);
        hash.add(arrays.mTexCoords);
        hash.add(arrays.mSecondTexCoords);
        hash.add(arrays.mColours);
        hash.add(arrays.mTangents);
        hash.add(arrays.mIndices);
        hash.add(arrays.mUnitStreams);
        hash.add(static_cast<std::uint8_t>(shape.mSheet));
        hash.add(static_cast<std::uint8_t>(shape.mFolded));
        hash.add(static_cast<std::uint8_t>(shape.mPocketed));
        return MeshKey{ .mWords = hash.getWords() };
    }
}
