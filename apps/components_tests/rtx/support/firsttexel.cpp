#include "firsttexel.hpp"

#include <cstdlib>
#include <cstring>

#include <rgbcx/bc7decomp.h>
#include <rgbcx/rgbcx.h>

namespace Rtx::Testing
{
    Texel firstTexelOf(const TextureFormat format, const std::span<const std::byte> bytes)
    {
        Texel texel{};
        if (bytes.size() < std::size_t{ layoutOf(format).mBytes })
        {
            ADD_FAILURE() << "a level of " << bytes.size() << " bytes, short of its format's block or texel";
            return texel;
        }
        if (format == TextureFormat::Bc7Srgb || format == TextureFormat::Bc7Unorm)
        {
            std::array<bc7decomp::color_rgba, 16> block{};
            EXPECT_TRUE(bc7decomp::unpack_bc7(bytes.data(), block.data())) << "a BC7 block of a reserved mode";
            texel = { block[0].r, block[0].g, block[0].b, block[0].a };
        }
        else if (isBc1(format))
        {
            std::array<std::uint8_t, 64> block{};
            rgbcx::unpack_bc1(bytes.data(), block.data());
            std::memcpy(texel.data(), block.data(), texel.size());
        }
        else
            std::memcpy(texel.data(), bytes.data(), texel.size());
        return texel;
    }

    ::testing::AssertionResult isNear(const Texel& got, const Texel& want, const int step)
    {
        for (std::size_t channel = 0; channel < got.size(); ++channel)
            if (std::abs(int(got[channel]) - int(want[channel])) > step)
                return ::testing::AssertionFailure()
                    << "channel " << channel << " is " << int(got[channel]) << " where " << int(want[channel])
                    << " was asked, within " << step << " (" << int(got[0]) << ", " << int(got[1]) << ", "
                    << int(got[2]) << ", " << int(got[3]) << ")";
        return ::testing::AssertionSuccess();
    }
}
