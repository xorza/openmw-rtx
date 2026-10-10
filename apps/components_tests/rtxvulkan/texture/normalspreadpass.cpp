#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3d>

#include <volk.h>
#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/brdf.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/texture/texturepasses.hpp>

namespace Rtx
{
    namespace
    {
        /// A map to measure the spread of: its texels, four bytes each unless `mFormat` says
        /// otherwise.
        struct SpreadMap
        {
            std::span<const std::uint8_t> mTexels;
            std::uint32_t mWidth = 1;
            std::uint32_t mHeight = 1;
            VkFormat mFormat = VK_FORMAT_R8G8B8A8_UNORM;
        };

        /// What a run measured: every map's spread, level by level, a byte a texel, and the means as
        /// the device left them, four floats a texel, in the room of the run's last group.
        struct Spreads
        {
            std::vector<std::vector<std::vector<std::uint8_t>>> mLevels;
            std::vector<float> mMeans;
        };

        struct RtxNormalSpreadPassTest : Testing::DeviceTest
        {
            /// Uploads `maps`, measures their spreads on the device in one run whose groups work in
            /// `room`, into images of the test's own, and hands them back with the means. The test's
            /// own images and not `Texture`'s, because a spread the trace samples is never copied
            /// back and carries no usage for it.
            Spreads spreadsOf(std::span<const SpreadMap> maps, const VkDeviceSize room = sSpreadMeansRoom)
            {
                Device& device = getDevice();
                const TexturePasses passes(device);

                std::vector<Image> spreads;
                std::vector<Image> images;
                spreads.reserve(maps.size());
                images.reserve(maps.size());

                Batch upload(getPool());
                TextureArrival arrival(device, room);
                arrival.open(maps.size());
                for (const SpreadMap& map : maps)
                {
                    const std::uint32_t across = std::max(map.mWidth / 2, 1u);
                    const std::uint32_t down = std::max(map.mHeight / 2, 1u);
                    const Image& spread = spreads.emplace_back(device, across, down, VK_FORMAT_R8_UNORM,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        "normal spread test spread", levelsTo1x1(across, down));
                    const Image& image = images.emplace_back(device, map.mWidth, map.mHeight, map.mFormat,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "normal spread test map", 1);
                    const std::array regions{ wholeLevel(0, 0, VkExtent3D{ map.mWidth, map.mHeight, 1 }) };
                    arrival.upload(upload, image, std::as_bytes(map.mTexels), regions);
                    arrival.spread(image, spread);
                }
                arrival.record(upload, passes);
                upload.flush();

                Spreads read;
                for (const Image& spread : spreads)
                {
                    std::vector<std::vector<std::uint8_t>>& levels = read.mLevels.emplace_back(spread.getMipLevels());
                    for (std::uint32_t level = 0; level < spread.getMipLevels(); ++level)
                        spread.read(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, levels[level], level);
                }

                const Buffer& means = arrival.getMeans();
                const Buffer copy = Buffer::readBack(device, means.getSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test");
                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    const VkBufferCopy whole{ .srcOffset = 0, .dstOffset = 0, .size = means.getSize() };
                    vkCmdCopyBuffer(commands, means.getHandle(), copy.getHandle(), 1, &whole);
                    copy.orderForHostRead(commands);
                });
                read.mMeans.resize(means.getSize() / sizeof(float));
                std::memcpy(read.mMeans.data(), copy.map(), read.mMeans.size() * sizeof(float));
                return read;
            }

            /// One map's spread, and the first level's means into `firstMeans` where one is given.
            std::vector<std::vector<std::uint8_t>> spreadOf(std::span<const std::uint8_t> texels, std::uint32_t width,
                std::uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
                std::vector<float>* firstMeans = nullptr)
            {
                const std::array maps{ SpreadMap{
                    .mTexels = texels, .mWidth = width, .mHeight = height, .mFormat = format } };
                Spreads read = spreadsOf(maps);
                if (firstMeans != nullptr)
                {
                    const std::size_t first = std::size_t{ std::max(width / 2, 1u) } * std::max(height / 2, 1u) * 4;
                    firstMeans->assign(read.mMeans.begin(), read.mMeans.begin() + static_cast<std::ptrdiff_t>(first));
                }
                return std::move(read.mLevels.front());
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
        ///
        /// **And an odd extent's last texel is in its level** (`axisTaps`): a line of three, two flat
        /// and the last leaning, is one texel a third each, and loses what each pair of the three
        /// apart does, a ninth of each: the two flat ones nothing and the leaning one, whose bytes
        /// lean 44.77°, `2 - 2 cos 44.77° = 0.5802` from each, `2 · 0.5802 / 9 = 0.1289`. Halved as
        /// two, the leaning texel was dropped and the line lost nothing.
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

            std::vector<std::uint8_t> line;
            for (const std::uint8_t* texel : { flat, flat, right })
                line.insert(line.end(), texel, texel + 4);
            const double third = 2.0 * (decoded(line, 0) - decoded(line, 2)).length2() / 9.0;
            EXPECT_NEAR(third, 0.1289, 1e-4);
            const std::vector<std::vector<std::uint8_t>> spreadLine = spreadOf(line, 3, 1);
            ASSERT_EQ(spreadLine.size(), 1u);
            EXPECT_NEAR(int{ spreadLine[0][0] }, byteOf(third), 1) << "the odd line's last texel was dropped";
        }

        /// **The spreads of a group work in the room the group before used**, and come out as they
        /// do where every map has room of its own. Two maps of four texels a side, the leaning one of
        /// `eachLevelLosesWhatItsNormalsDisagreeBy` and the same mirrored, each a spread of four
        /// texels and one, so five texels of means, eighty bytes: under a room of eighty they are two
        /// groups over eighty bytes, under the whole room one group over a hundred and sixty, which
        /// says the room is what splits them. The second map's spread reads the same either way.
        TEST_F(RtxNormalSpreadPassTest, aGroupOfSpreadsWorksInTheRoomTheGroupBeforeUsed)
        {
            constexpr std::uint8_t flat[4]{ 128, 128, 255, 255 };
            constexpr std::uint8_t right[4]{ 218, 128, 218, 255 };
            constexpr std::uint8_t left[4]{ 37, 128, 218, 255 };
            std::vector<std::uint8_t> leaning;
            std::vector<std::uint8_t> mirrored;
            for (std::size_t y = 0; y < 4; ++y)
                for (std::size_t x = 0; x < 4; ++x)
                {
                    const bool tilted = y < 2 && x >= 2;
                    const std::uint8_t* texel = tilted ? ((x + y) % 2 == 0 ? right : left) : flat;
                    const std::uint8_t* other = tilted ? ((x + y) % 2 == 0 ? left : right) : flat;
                    leaning.insert(leaning.end(), texel, texel + 4);
                    mirrored.insert(mirrored.end(), other, other + 4);
                }
            const std::array maps{ SpreadMap{ .mTexels = leaning, .mWidth = 4, .mHeight = 4 },
                SpreadMap{ .mTexels = mirrored, .mWidth = 4, .mHeight = 4 } };
            constexpr VkDeviceSize one = 5 * Shaders::NORMAL_SPREAD_MEAN_BYTES;

            const Spreads apart = spreadsOf(maps, one);
            const Spreads together = spreadsOf(maps);
            EXPECT_EQ(apart.mMeans.size() * sizeof(float), one) << "two groups did not share one map's room";
            EXPECT_EQ(together.mMeans.size() * sizeof(float), 2 * one) << "one group did not hold both maps";
            EXPECT_EQ(apart.mLevels, together.mLevels) << "a group in the room the one before used came out otherwise";
            EXPECT_NE(int{ apart.mLevels[1][0][1] }, 0) << "the second map's leaning quarter lost nothing";
        }

        /// **A texel that points nowhere stands in as the flat normal**, where it was normalised to a
        /// NaN that the chain's means carried for the texture's life. Half floats, because no byte
        /// is a half: (0.5, 0.5, 0.5) decodes to no direction at all, beside three that point
        /// straight out. With it read as straight out, the four agree, so their mean is (0, 0, 1) to
        /// the bit — a quarter of one, four times — and they lose nothing.
        TEST_F(RtxNormalSpreadPassTest, aTexelThatPointsNowhereStandsInAsTheFlatNormal)
        {
            constexpr std::uint16_t half = 0x3800;
            constexpr std::uint16_t one = 0x3C00;
            const std::array<std::uint16_t, 16> words{ half, half, half, one, half, half, one, one, half, half, one,
                one, half, half, one, one };

            std::vector<float> means;
            const std::span<const std::uint8_t> texels(
                reinterpret_cast<const std::uint8_t*>(words.data()), sizeof(words));
            const std::vector<std::vector<std::uint8_t>> spread
                = spreadOf(texels, 2, 2, VK_FORMAT_R16G16B16A16_SFLOAT, &means);
            ASSERT_EQ(spread.size(), 1u);
            ASSERT_EQ(means.size(), 4u);
            EXPECT_EQ(means[0], 0.0f);
            EXPECT_EQ(means[1], 0.0f);
            EXPECT_EQ(means[2], 1.0f) << "the mean of a normal that points nowhere";
            EXPECT_TRUE(std::isfinite(means[3]));
            EXPECT_EQ(int{ spread[0][0] }, 0) << "four that agree lost something";
        }
    }
}
