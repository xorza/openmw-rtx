#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/tangent.h>

namespace Rtx
{
    namespace
    {
        /// The six axes come back as they went, either way round, because an odd count of steps puts nought
        /// and one on a step, and the decode's reciprocal of the steps times them is one.
        ///
        /// Along x is the square's `(1, 0)`: steps `0x7FFE` and `0x3FFF`, the second fifteen bits up, which
        /// is `0x1FFF8000`, and the present bit over them — the flipped bit too for the other handedness.
        TEST(RtxTangentTest, theAxesComeBackExactlyWithTheirHandedness)
        {
            // **The decode multiplies by the steps' reciprocal**, which lands the ends and the middle
            // exactly only because each count times its float reciprocal rounds to one.
            EXPECT_EQ(static_cast<float>(Shaders::TANGENT_STEPS) * Shaders::TANGENT_STEP, 1.0f);
            EXPECT_EQ(static_cast<float>(Shaders::SURFACE_NORMAL_STEPS) * Shaders::SURFACE_NORMAL_STEP, 1.0f);

            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(1.0f, 0.0f, 0.0f), false), 0x9FFFFFFEu);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(1.0f, 0.0f, 0.0f), true), 0xDFFFFFFEu);

            const std::array axes{
                osg::Vec3f(1.0f, 0.0f, 0.0f),
                osg::Vec3f(-1.0f, 0.0f, 0.0f),
                osg::Vec3f(0.0f, 1.0f, 0.0f),
                osg::Vec3f(0.0f, -1.0f, 0.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, -1.0f),
            };
            for (const osg::Vec3f& axis : axes)
                for (const float handedness : { 1.0f, -1.0f })
                {
                    EXPECT_EQ(Shaders::unpackTangent(Shaders::packTangent(axis, handedness < 0.0f)),
                        osg::Vec4f(axis, handedness))
                        << axis.x() << ' ' << axis.y() << ' ' << axis.z() << ' ' << handedness;
                }
        }

        /// A tangent of no length is none, whatever its handedness, and so is one the generator
        /// left undefined or a file gave past every float. None reads back as nought.
        ///
        /// **Past every float in its sum as well as in a component.** Two halves of the largest
        /// float sum to an infinity as surely as an infinity does.
        TEST(RtxTangentTest, noLengthIsNoTangent)
        {
            constexpr float infinity = std::numeric_limits<float>::infinity();
            constexpr float largest = std::numeric_limits<float>::max();
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(0.0f, 0.0f, 0.0f), false), 0u);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(0.0f, 0.0f, 0.0f), true), 0u);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(std::nanf(""), 0.0f, 0.0f), false), 0u);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(infinity, 0.0f, 0.0f), false), 0u);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(0.0f, -infinity, 1.0f), true), 0u);
            EXPECT_EQ(Shaders::packTangent(osg::Vec3f(largest, largest, 0.0f), false), 0u);
            EXPECT_NE(Shaders::packTangent(osg::Vec3f(largest, 0.0f, 0.0f), false), 0u) << "the largest is a number";
            EXPECT_EQ(Shaders::unpackTangent(0u), osg::Vec4f());
        }

        /// Every direction with whole coordinates from minus three to three, above and below the
        /// equator and on it, comes back within the packing's bound, with its handedness, and the
        /// length it went in with is not stored.
        ///
        /// **The bound.** A stored coordinate is at most half a step, `1 / 32766`, from the one packed,
        /// which moves the point on the octahedron by at most `sqrt(1.5) / 16383` on either side of the
        /// fold. The octahedron is nowhere nearer the centre than `1 / sqrt(3)`, so the projection onto the
        /// sphere stretches that by at most `sqrt(3)`: `sqrt(4.5) / 16383`, about `1.2948e-4`; the `1e-6`
        /// over it is the float arithmetic's.
        TEST(RtxTangentTest, everyDirectionComesBackWithinTheBoundOfTheSteps)
        {
            const float bound = std::sqrt(4.5f) / 16383.0f + 1e-6f;

            for (int x = -3; x <= 3; ++x)
                for (int y = -3; y <= 3; ++y)
                    for (int z = -3; z <= 3; ++z)
                    {
                        if (x == 0 && y == 0 && z == 0)
                            continue;

                        const osg::Vec3f direction(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                        for (const float handedness : { 1.0f, -1.0f })
                        {
                            const bool flipped = handedness < 0.0f;
                            const std::uint32_t packed = Shaders::packTangent(direction, flipped);
                            const osg::Vec4f unpacked = Shaders::unpackTangent(packed);

                            osg::Vec3f unit = direction;
                            unit.normalize();
                            EXPECT_LE((osg::Vec3f(unpacked.x(), unpacked.y(), unpacked.z()) - unit).length(), bound)
                                << x << ' ' << y << ' ' << z;
                            EXPECT_EQ(unpacked.w(), handedness) << x << ' ' << y << ' ' << z;
                            EXPECT_EQ(Shaders::packTangent(direction * 2.0f, flipped), packed)
                                << "the length was stored: " << x << ' ' << y << ' ' << z;
                        }
                    }
        }

        /// **A normal's word reads back to its direction**, which a stored shape's normals rest on
        /// (`ShapePass::run`): decoded and packed again, a word names the direction it named, and a word
        /// the packing made packs back to itself. Every code was checked once, 2³⁰ of them
        /// (`docs/research/2026-11-09-normal-word-round-trip.md`); here every 97th along each axis,
        /// and the square's four edges whole, where two codes name one direction.
        TEST(RtxTangentTest, aNormalsWordReadsBackToItsDirection)
        {
            constexpr std::uint32_t last = 2 * Shaders::TANGENT_STEPS;
            const auto check = [](std::uint32_t x, std::uint32_t y) {
                const std::uint32_t word = Shaders::TANGENT_PRESENT | x | (y << Shaders::TANGENT_COORDINATE_BITS);
                const osg::Vec3f direction = Shaders::unpackNormal(word);
                const std::uint32_t packed = Shaders::packNormal(direction);
                EXPECT_EQ(Shaders::unpackNormal(packed), direction) << x << ", " << y;
                EXPECT_EQ(Shaders::packNormal(Shaders::unpackNormal(packed)), packed) << x << ", " << y;
            };
            for (std::uint32_t y = 0; y <= last; y += 97)
                for (std::uint32_t x = 0; x <= last; x += 97)
                    check(x, y);
            for (std::uint32_t along = 0; along <= last; ++along)
            {
                check(along, 0);
                check(along, last);
                check(0, along);
                check(last, along);
            }
        }

        /// **A normal is a tangent's word with no handedness**: the flipped bit never set, whichever
        /// way the normal points, and the direction read back as a tangent's is; a normal of no
        /// length is the word nought, which reads back as the zeros "the triangle's plane" is.
        TEST(RtxTangentTest, aNormalIsATangentWordWithNoHandedness)
        {
            EXPECT_EQ(Shaders::packNormal(osg::Vec3f(1.0f, 0.0f, 0.0f)), 0x9FFFFFFEu);
            EXPECT_EQ(Shaders::packNormal(osg::Vec3f(0.0f, 0.0f, 1.0f)), 0x9FFFBFFFu);
            for (int x = -2; x <= 2; ++x)
                for (int z = -2; z <= 2; ++z)
                {
                    const osg::Vec3f direction(static_cast<float>(x), 1.0f, static_cast<float>(z));
                    const std::uint32_t packed = Shaders::packNormal(direction);
                    EXPECT_EQ(packed, Shaders::packTangent(direction, false));
                    EXPECT_EQ(packed & Shaders::TANGENT_FLIPPED, 0u);
                    const osg::Vec4f tangent = Shaders::unpackTangent(packed);
                    EXPECT_EQ(Shaders::unpackNormal(packed), osg::Vec3f(tangent.x(), tangent.y(), tangent.z()));
                }
            EXPECT_EQ(Shaders::packNormal(osg::Vec3f()), 0u);
            EXPECT_EQ(Shaders::unpackNormal(0u), osg::Vec3f());
        }
    }
}
