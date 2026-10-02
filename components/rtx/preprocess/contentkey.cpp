#include "contentkey.hpp"

#include <cstdint>

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
        mState.add(length);
        mState.add(bytes);
        mBytes += length;
    }
}
