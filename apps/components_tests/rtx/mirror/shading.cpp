#include <span>

#include <gtest/gtest.h>

#include <osg/StateSet>
#include <osg/Uniform>
#include <osg/ref_ptr>

#include <components/rtx/mirror/shading.hpp>

namespace Rtx
{
    namespace
    {
        /// A state set carrying no uniform at all inherits what the chain already had.
        ///
        /// **Nearly every state set in the world is this one**, which is why the empty answer is
        /// asked for before the two names are. `fadeThrough` runs at every node and every drawable a
        /// walk enters, and `osg::StateSet::getUniform` searches a map keyed on `std::string`.
        TEST(RtxShadingTest, aStateSetWithNoUniformInheritsWhatTheChainHad)
        {
            const osg::ref_ptr<osg::StateSet> bare = new osg::StateSet;

            EXPECT_EQ(fadeThrough(*bare, Fade{}).mPlacement, 1.0f);
            const Fade faded = fadeThrough(*bare, Fade{ .mPlacement = 0.25f, .mActor = 0.5f });
            EXPECT_EQ(faded.mPlacement, 0.25f) << "the chain's own fade was not carried through";
            EXPECT_EQ(faded.mActor, 0.5f);

            // **And a link built under a chain carries it**, as both walks build theirs: the first
            // link from the defaults, a controller's link animated through everything under it,
            // and the fade of the link above carried down.
            const Shading first = Shading::under({}, *bare, false);
            EXPECT_EQ(first.mStateSet, bare.get());
            EXPECT_EQ(first.mFade.mPlacement, 1.0f);
            EXPECT_FALSE(first.mAnimatedThrough);

            const Shading controller{ .mStateSet = bare.get(),
                .mFade = Fade{ .mPlacement = 0.25f, .mActor = 0.5f },
                .mAnimated = true,
                .mAnimatedThrough = true };
            const Shading below = Shading::under(std::span(&controller, 1), *bare, false);
            EXPECT_FALSE(below.mAnimated) << "a link was animated for its controller above";
            EXPECT_TRUE(below.mAnimatedThrough) << "what stands under a controller is not animated by it";
            EXPECT_EQ(below.mFade.mPlacement, 0.25f) << "the fade above was not carried down";
        }

        /// A state set carrying a uniform that is not the fade inherits too.
        ///
        /// **The guard is on the list and not on the answer**, so a state set with something else on
        /// it still has to reach the two lookups and come back with what it was given.
        TEST(RtxShadingTest, aStateSetWithAnotherUniformInheritsAsWell)
        {
            const osg::ref_ptr<osg::StateSet> other = new osg::StateSet;
            other->addUniform(new osg::Uniform("useNormalAsColor", 1));

            EXPECT_EQ(fadeThrough(*other, Fade{ .mPlacement = 0.5f, .mActor = 0.5f }).mPlacement, 0.5f);
        }

        /// The fade the game writes is read, and the alpha beside it is multiplied into it.
        ///
        /// `MWRender::TransparencyUpdater` writes `actorFade`, and Invisibility and Chameleon ride
        /// `alpha` on the same state set. Three quarters of a half is three eighths, and the
        /// inherited fade is replaced rather than multiplied — the nearest state set that carries
        /// the pair is the whole answer.
        ///
        /// **And a nearer `alpha` replaces the actor's and keeps its fade**, as the rasterizer's
        /// state stack resolves the two uniforms apart: a cast effect's root on an invisible caster
        /// states an `alpha` of one, so the placement under it is faded by the actor's three
        /// quarters alone, and the material reads its own `alpha`.
        TEST(RtxShadingTest, theFadeAndTheAlphaAreMultipliedAndANearerAlphaReplacesTheActors)
        {
            const osg::ref_ptr<osg::StateSet> fading = new osg::StateSet;
            fading->addUniform(new osg::Uniform("actorFade", 0.75f));

            EXPECT_FLOAT_EQ(fadeThrough(*fading, Fade{}).mPlacement, 0.75f);
            EXPECT_FLOAT_EQ(fadeThrough(*fading, Fade{ .mPlacement = 0.1f, .mActor = 0.1f }).mPlacement, 0.75f)
                << "an inherited fade was multiplied rather than replaced";

            fading->addUniform(new osg::Uniform("alpha", 0.5f));
            const Fade actor = fadeThrough(*fading, Fade{});
            EXPECT_FLOAT_EQ(actor.mPlacement, 0.375f);
            EXPECT_FLOAT_EQ(actor.mActor, 0.75f);

            const osg::ref_ptr<osg::StateSet> cast = new osg::StateSet;
            cast->addUniform(new osg::Uniform("alpha", 1.0f));
            const Fade under = fadeThrough(*cast, actor);
            EXPECT_FLOAT_EQ(under.mPlacement, 0.75f) << "the invisibility's alpha reached a cast effect";
            EXPECT_FLOAT_EQ(under.mActor, 0.75f);
        }
    }
}
