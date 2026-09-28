#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3d>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/brdf.h>
#include <components/rtx/texturedata.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/device.hpp>
#include <components/rtxvulkan/image.hpp>
#include <components/rtxvulkan/texturepasses.hpp>

#include "support/device/harness.hpp"

namespace Rtx
{
    namespace
    {
        struct RtxNormalSpreadPassTest : Testing::DeviceTest
        {
            /// Uploads a map of `texels`, four bytes a texel, measures its spread on the device into
            /// an image of the test's own, and hands every level of it back as bytes, a byte a texel.
            /// The test's own image and not `Texture`'s, because a spread the trace samples is never
            /// copied back and carries no usage for it.
            std::vector<std::vector<std::uint8_t>> spreadOf(
                std::span<const std::uint8_t> texels, std::uint32_t width, std::uint32_t height)
            {
                Device& device = getDevice();
                const TexturePasses passes(device, Testing::getShaderDirectory());

                const std::uint32_t across = std::max(width / 2, 1u);
                const std::uint32_t down = std::max(height / 2, 1u);
                const std::uint32_t levels = levelsTo1x1(across, down);
                const Image spread(device, across, down, VK_FORMAT_R8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    "normal spread test spread", levels);
                const Image means(device, across, down, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT,
                    "normal spread test means", levels);

                Batch upload(getPool());
                Image map(device, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "normal spread test map", 1);
                std::vector<VkBufferImageCopy> regions{ VkBufferImageCopy{
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageExtent = { width, height, 1 },
                } };
                uploadImage(upload, map, std::as_bytes(texels), regions);
                passes.mSpread.record(upload.getCommands(), map, means, spread);
                upload.flush();

                std::vector<std::vector<std::uint8_t>> read(levels);
                for (std::uint32_t level = 0; level < levels; ++level)
                    spread.read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, read[level], level);
                return read;
            }
        };

        /// A texel's normal as the trace decodes it, `2 rgb / 255 - 1`, made unit.
        osg::Vec3d decoded(std::span<const std::uint8_t> texels, std::size_t at)
        {
            osg::Vec3d normal;
            for (std::size_t axis = 0; axis < 3; ++axis)
                normal[axis] = 2.0 * texels[at * 4 + axis] / 255.0 - 1.0;
            normal.normalize();
            return normal;
        }

        /// What four vectors each `loss` short of unit lose between them: the mean of their own and
        /// the variance of the four, `Σ |mᵢ - mⱼ|² / 16` over the six pairs.
        double lossOf(const osg::Vec3d (&vectors)[4], const double (&losses)[4])
        {
            double apart = 0.0;
            for (int one = 0; one < 4; ++one)
                for (int other = one + 1; other < 4; ++other)
                    apart += (vectors[one] - vectors[other]).length2();
            return (losses[0] + losses[1] + losses[2] + losses[3]) / 4.0 + apart / 16.0;
        }

        /// A lost roughness as the byte it is stored in.
        int byteOf(double loss)
        {
            const float roughness = Shaders::slopeRoughness(Shaders::normalSpreadSlopes(static_cast<float>(loss)));
            return static_cast<int>(std::lround(roughness * 255.0f));
        }

        /// **What each level of a normal map lost of the normals it averages**, by hand and against
        /// the device. A map four texels square: three quarters of it points straight out, and its
        /// top right quarter leans forty-five degrees one way and the other, in bytes that are each
        /// other's mirror, 218 and 37 about 127.5.
        ///
        /// Its second level: a flat quarter loses nothing, to the bit — normals that agree are a
        /// mean one long only to a float's last place, and that place to the fourth root was a
        /// byte of five. The leaning quarter's mean is half its normals' length squared, a loss of
        /// a half, which stands for `2 · 0.5 / (0.7071 · 2.5) = 0.5657` of slopes and a roughness
        /// of its fourth root, `0.8672`, 221. Its third: a quarter of the leaning quarter's half,
        /// and what the four means lose to each other, the leaning one `0.2929` shorter than the
        /// flat ones along z: `0.125 + 3 · 0.0858 / 16 = 0.1411`, a roughness of `0.6141`, 157.
        TEST_F(RtxNormalSpreadPassTest, eachLevelLosesWhatItsNormalsDisagreeBy)
        {
            constexpr std::uint8_t flat[4]{ 128, 128, 255, 255 };
            constexpr std::uint8_t right[4]{ 218, 128, 218, 255 };
            constexpr std::uint8_t left[4]{ 37, 128, 218, 255 };
            const std::uint8_t* rows[4][4]{
                { flat, flat, right, left },
                { flat, flat, left, right },
                { flat, flat, flat, flat },
                { flat, flat, flat, flat },
            };
            std::vector<std::uint8_t> texels;
            for (const auto& row : rows)
                for (const std::uint8_t* texel : row)
                    texels.insert(texels.end(), texel, texel + 4);

            const std::vector<std::vector<std::uint8_t>> spread = spreadOf(texels, 4, 4);
            ASSERT_EQ(spread.size(), 2u) << "a four-texel map's second and third levels";
            ASSERT_EQ(spread[0].size(), 4u);
            ASSERT_EQ(spread[1].size(), 1u);

            // The host's numbers, from the same bytes: each quarter's mean and loss, then the four's.
            osg::Vec3d quarterMeans[4];
            double quarterLosses[4];
            for (std::size_t quarter = 0; quarter < 4; ++quarter)
            {
                const std::size_t x = 2 * (quarter % 2);
                const std::size_t y = 2 * (quarter / 2);
                osg::Vec3d normals[4];
                for (std::size_t at = 0; at < 4; ++at)
                    normals[at] = decoded(texels, (y + at / 2) * 4 + x + at % 2);
                quarterMeans[quarter] = (normals[0] + normals[1] + normals[2] + normals[3]) / 4.0;
                quarterLosses[quarter] = lossOf(normals, { 0.0, 0.0, 0.0, 0.0 });
            }

            EXPECT_EQ(int{ spread[0][0] }, 0) << "a quarter that agrees with itself loses nothing";
            EXPECT_EQ(int{ spread[0][2] }, 0);
            EXPECT_EQ(int{ spread[0][3] }, 0);
            EXPECT_NEAR(quarterLosses[1], 0.5, 1e-5);
            EXPECT_EQ(byteOf(quarterLosses[1]), 221);
            EXPECT_NEAR(int{ spread[0][1] }, byteOf(quarterLosses[1]), 1) << "the leaning quarter";

            const double whole = lossOf(quarterMeans, quarterLosses);
            EXPECT_NEAR(whole, 0.1411, 1e-4);
            EXPECT_EQ(byteOf(whole), 157);
            EXPECT_NEAR(int{ spread[1][0] }, byteOf(whole), 1) << "the whole map";
        }
    }
}
