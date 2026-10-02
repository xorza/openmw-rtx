#include "hashstate.hpp"

#include <algorithm>
#include <climits>

#include <smhasher/MurmurHash3.h>

namespace Rtx
{
    void HashState::add(std::span<const std::byte> bytes)
    {
        // The hash takes its length as an `int`, and a run past that is taken in pieces, each
        // chained into the state like any other. An empty run is still a step, so that it moves
        // the state as a run of any other length does. The seed is copied, so that the output
        // never overlaps what the hash reads, whatever its implementation does.
        do
        {
            const std::size_t piece = std::min<std::size_t>(bytes.size(), INT_MAX);
            const DigestWords seed = mWords;
            MurmurHash3_x64_128(bytes.data(), static_cast<int>(piece), seed.data(), mWords.data());
            bytes = bytes.subspan(piece);
        } while (!bytes.empty());
    }
}
