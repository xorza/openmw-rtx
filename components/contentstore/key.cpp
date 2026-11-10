#include "key.hpp"

#include <cstdint>

namespace ContentStore
{
    Digest::Digest(const std::string_view maker, const std::uint32_t version)
    {
        add(std::span<const char>(maker));
        add(version);

        // The maker is the key's and not the input's, so a report's bytes are the input's alone.
        mBytes = 0;
    }

    void Digest::addBytes(const std::span<const std::byte> bytes)
    {
        const std::uint64_t length = bytes.size();
        mState.add(length);
        mState.add(bytes);
        mBytes += length;
    }
}
