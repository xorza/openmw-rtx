#include "contentkey.hpp"

#include <algorithm>
#include <climits>

#include <smhasher/MurmurHash3.h>

namespace Rtx
{
    ContentDigest::ContentDigest(const std::string_view pass, const std::uint32_t version)
    {
        add(std::span<const char>(pass));
        addValue(version);

        // The pass is the key's and not the input's, so a report's bytes are the input's alone.
        mBytes = 0;
    }

    void ContentDigest::addBytes(const std::span<const std::byte> bytes)
    {
        const std::uint64_t length = bytes.size();
        step(std::as_bytes(std::span<const std::uint64_t>(&length, 1)));
        step(bytes);
        mBytes += length;
    }

    void ContentDigest::step(std::span<const std::byte> bytes)
    {
        // The hash takes its length as an `int`, and a run past that is taken in pieces, each
        // chained into the state like any other.
        do
        {
            const std::size_t piece = std::min<std::size_t>(bytes.size(), INT_MAX);
            std::array<std::uint64_t, 2> next{};
            MurmurHash3_x64_128(bytes.data(), static_cast<int>(piece), mState.data(), next.data());
            mState = next;
            bytes = bytes.subspan(piece);
        } while (!bytes.empty());
    }
}
