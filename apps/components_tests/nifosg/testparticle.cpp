#include <cstdlib>

#include <gtest/gtest.h>

#include <osg/CopyOp>
#include <osg/Vec3f>
#include <osg/ref_ptr>
#include <osgParticle/Particle>
#include <osgParticle/range>

#include <components/misc/rng.hpp>
#include <components/nifosg/particle.hpp>

namespace
{
    osg::ref_ptr<NifOsg::BoxPlacer> makeBox()
    {
        osg::ref_ptr<NifOsg::BoxPlacer> placer = new NifOsg::BoxPlacer;
        placer->setCenter(osg::Vec3f(100.f, 0.f, 0.f));
        placer->setXRange(-1.f, 1.f);
        placer->setYRange(0.f, 2.f);
        placer->setZRange(10.f, 20.f);
        return placer;
    }

    osg::Vec3f placeOne(const NifOsg::BoxPlacer& placer)
    {
        osgParticle::Particle particle;
        placer.place(&particle);
        return particle.getPosition();
    }

    /// **A particle is placed by the engine's generator, x then y then z, and the C library's draws move nothing**:
    /// a graphics driver draws from `rand` on the frame's thread as often as it likes, so a placer that drew from it
    /// placed a seeded run's particles elsewhere each run.
    TEST(NifOsgBoxPlacerTest, placesByTheEnginesGeneratorAlone)
    {
        const osg::ref_ptr<NifOsg::BoxPlacer> placer = makeBox();

        // The three draws the placer makes, taken by hand: centre plus each range's minimum and its span's share.
        Misc::Rng::init(7);
        const float u = Misc::Rng::rollClosedProbability();
        const float v = Misc::Rng::rollClosedProbability();
        const float w = Misc::Rng::rollClosedProbability();
        const osg::Vec3f expected(100.f + (-1.f + 2.f * u), 0.f + 2.f * v, 10.f + 10.f * w);

        // The same C library stream twice, on purpose: a draw the placer took shows as the next number moved.
        Misc::Rng::init(7);
        std::srand(3); // NOLINT(bugprone-random-generator-seed)
        const int untouched = std::rand();
        std::srand(3); // NOLINT(bugprone-random-generator-seed)
        EXPECT_EQ(placeOne(*placer), expected);
        EXPECT_EQ(std::rand(), untouched) << "the placer drew from the C library's generator";

        // The C library's stream drawn from between: the same place.
        Misc::Rng::init(7);
        for (int draw = 0; draw < 5; ++draw)
            std::rand();
        EXPECT_EQ(placeOne(*placer), expected);

        // Another seed of the engine's generator places elsewhere.
        Misc::Rng::init(8);
        EXPECT_NE(placeOne(*placer), expected);
    }

    /// **A copy is the same placer**: a model's instance clones its template's emitter, and a copy that fell back to
    /// `osgParticle::BoxPlacer` would draw from the C library again.
    TEST(NifOsgBoxPlacerTest, aCopyIsTheSamePlacer)
    {
        const osg::ref_ptr<NifOsg::BoxPlacer> placer = makeBox();
        const osg::ref_ptr<osg::Object> cloned = placer->clone(osg::CopyOp::DEEP_COPY_ALL);
        const auto* copy = dynamic_cast<const NifOsg::BoxPlacer*>(cloned.get());
        ASSERT_NE(copy, nullptr);
        EXPECT_EQ(copy->getCenter(), placer->getCenter());
        EXPECT_EQ(copy->getXRange().minimum, -1.f);
        EXPECT_EQ(copy->getXRange().maximum, 1.f);
        EXPECT_EQ(copy->getZRange().maximum, 20.f);

        Misc::Rng::init(7);
        const osg::Vec3f original = placeOne(*placer);
        Misc::Rng::init(7);
        EXPECT_EQ(placeOne(*copy), original);
    }
}
