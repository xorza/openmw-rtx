#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>

#include <components/rtx/frame/bouncepairing.hpp>
#include <components/rtx/shaders/bouncereuse.h>

namespace Rtx
{
    namespace
    {
        /// **Every texel's partner's partner is itself**, at both sizes the reuse reads and at a
        /// deviation that wraps a few steps at the texture's edge.
        TEST(RtxBouncePairingTest, everyTexelsPartnersPartnerIsItself)
        {
            for (const std::uint32_t size : { Shaders::BOUNCE_PAIRING_SIZE_0, Shaders::BOUNCE_PAIRING_SIZE_1 })
            {
                const BouncePairing pairing(size, 11.5f, 7);
                const auto side = static_cast<std::int32_t>(size);
                for (std::uint32_t down = 0; down < size; ++down)
                    for (std::uint32_t across = 0; across < size; ++across)
                    {
                        const PairingStep step = pairing.stepAt(across, down);
                        ASSERT_FALSE(step.mAcross == 0 && step.mDown == 0) << "a texel paired with itself";
                        const auto partnerAcross = static_cast<std::uint32_t>(
                            ((static_cast<std::int32_t>(across) + step.mAcross) % side + side) % side);
                        const auto partnerDown = static_cast<std::uint32_t>(
                            ((static_cast<std::int32_t>(down) + step.mDown) % side + side) % side);
                        const PairingStep back = pairing.stepAt(partnerAcross, partnerDown);
                        ASSERT_EQ(back.mAcross, -step.mAcross) << across << ", " << down;
                        ASSERT_EQ(back.mDown, -step.mDown) << across << ", " << down;
                    }
            }
        }

        /// **The steps spread as far as asked, and one shuffle further at most**, over the deviations
        /// the reuse asks for: from the least disc of three pixels (0.532 × 3) to a disc three per
        /// cent of 1440 rows tall (0.532 × 43.2), and the 11.5 of 720 rows. One shuffle adds about two
        /// pixels squared to the square of the deviation (the paper's `n ≈ σ² / 2`), so the square is
        /// over by less than that step: measured at most 1.32, which is 11% at 1.6, where no whole
        /// number of shuffles lands nearer, 4% at 4 and 2% at 23.
        TEST(RtxBouncePairingTest, theStepsSpreadAsFarAsAsked)
        {
            for (const float deviation : { 1.6f, 4.0f, 11.5f, 23.0f })
            {
                const BouncePairing pairing(Shaders::BOUNCE_PAIRING_SIZE_0, deviation, 3);
                EXPECT_GE(pairing.getDeviation(), deviation);
                EXPECT_LT(pairing.getDeviation() * pairing.getDeviation() - deviation * deviation, 2.5f) << deviation;
            }
        }

        /// One seed is one texture, and two seeds are two.
        TEST(RtxBouncePairingTest, aSeedIsATexture)
        {
            const BouncePairing first(Shaders::BOUNCE_PAIRING_SIZE_1, 4.0f, 1);
            const BouncePairing again(Shaders::BOUNCE_PAIRING_SIZE_1, 4.0f, 1);
            const BouncePairing other(Shaders::BOUNCE_PAIRING_SIZE_1, 4.0f, 2);
            EXPECT_TRUE(std::ranges::equal(first.getSteps(), again.getSteps()));
            EXPECT_FALSE(std::ranges::equal(first.getSteps(), other.getSteps()));
        }

        /// **Reflected, transposed and moved, the pairs stay pairs.** Over every pixel of a frame 64
        /// across and 40 down, under all eight turns and two offsets: the partner `pairedStep` names
        /// reads the texel the step leads to, and its own step leads back. The frame stands a texture
        /// in from the origin, so no partner falls below it.
        TEST(RtxBouncePairingTest, aTurnedPairingKeepsItsPairs)
        {
            const BouncePairing pairing(Shaders::BOUNCE_PAIRING_SIZE_1, 11.5f, 5);
            const std::uint32_t size = pairing.getSize();
            for (std::uint32_t turn = 0; turn < 8; ++turn)
                for (const Shaders::uvec2 offset : { Shaders::uvec2(0, 0), Shaders::uvec2(17, 93) })
                    for (std::uint32_t down = size; down < size + 40; ++down)
                        for (std::uint32_t across = size; across < size + 64; ++across)
                        {
                            const Shaders::uvec2 pixel(across, down);
                            const Shaders::uvec2 texel = Shaders::pairingTexel(pixel, turn, offset, size);
                            const PairingStep step = pairing.stepAt(texel[0], texel[1]);
                            const Shaders::ivec2 moved
                                = Shaders::pairedStep(Shaders::ivec2(step.mAcross, step.mDown), turn);
                            const Shaders::uvec2 partner(
                                static_cast<std::uint32_t>(static_cast<std::int32_t>(across) + moved[0]),
                                static_cast<std::uint32_t>(static_cast<std::int32_t>(down) + moved[1]));

                            const Shaders::uvec2 partnerTexel = Shaders::pairingTexel(partner, turn, offset, size);
                            const auto side = static_cast<std::int32_t>(size);
                            EXPECT_EQ(static_cast<std::int32_t>(partnerTexel[0]),
                                ((static_cast<std::int32_t>(texel[0]) + step.mAcross) % side + side) % side)
                                << "turn " << turn << " at " << across << ", " << down;
                            EXPECT_EQ(static_cast<std::int32_t>(partnerTexel[1]),
                                ((static_cast<std::int32_t>(texel[1]) + step.mDown) % side + side) % side);

                            const PairingStep back = pairing.stepAt(partnerTexel[0], partnerTexel[1]);
                            const Shaders::ivec2 returned
                                = Shaders::pairedStep(Shaders::ivec2(back.mAcross, back.mDown), turn);
                            ASSERT_EQ(
                                static_cast<std::int32_t>(partner[0]) + returned[0], static_cast<std::int32_t>(across));
                            ASSERT_EQ(
                                static_cast<std::int32_t>(partner[1]) + returned[1], static_cast<std::int32_t>(down));
                        }
        }

        /// **Both textures, made at a resize, cost what a resize can pay.** At 720 traced rows the
        /// deviation is 11.5 and at 1440 rows 23.0.
        TEST(RtxBouncePairingTest, bothTexturesAreMadeQuickly)
        {
            for (const float deviation : { 11.5f, 23.0f })
            {
                const auto start = std::chrono::steady_clock::now();
                const BouncePairing first(Shaders::BOUNCE_PAIRING_SIZE_0, deviation, 0);
                const BouncePairing second(Shaders::BOUNCE_PAIRING_SIZE_1, deviation, 1);
                const double milliseconds
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                RecordProperty("milliseconds at " + std::to_string(deviation), std::to_string(milliseconds));
                std::printf("PAIRING deviation %.1f: %.1f ms\n", double(deviation), milliseconds);
                EXPECT_LT(milliseconds, 100.0);
            }
        }
    }
}
