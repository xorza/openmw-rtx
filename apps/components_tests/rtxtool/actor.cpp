#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include <components/esm3/loadnpc.hpp>

#include <apps/rtxtool/actor.hpp>
#include <apps/rtxtool/npc.hpp>
#include <apps/rtxtool/options.hpp>
#include <apps/rtxtool/world.hpp>

#include "../rtx/harness.hpp"

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// A Morrowind installation as the harness finds one, or null where there is none.
        ///
        /// The same route the tool takes: the configuration manager reads `openmw.cfg`, which says
        /// where the game is installed. A machine without it is a legitimate skip; a machine with it
        /// configured wrongly fails out of `World`'s own constructor.
        std::unique_ptr<World> openWorld(Files::ConfigurationManager& config, bpo::variables_map& variables)
        {
            bpo::options_description options = makeOptionsDescription(false);
            bpo::store(bpo::command_line_parser(std::vector<std::string>{}).options(options).run(), variables);
            bpo::notify(variables);

            config.processPaths(variables, std::filesystem::current_path());
            config.readConfiguration(variables, options);

            if (variables["content"].as<std::vector<std::string>>().empty())
                return nullptr;

            return std::make_unique<World>(
                config, variables, Rtx::Testing::getShaderDirectory().parent_path().parent_path());
        }

        /// A cliff racer, which is a skinned creature whose animation is a wing beat: the widest
        /// travel of any vertex in the game against the shortest track, so a pose apart in time is a
        /// pose apart in space by an amount nothing has to be careful about measuring.
        constexpr VFS::Path::NormalizedView sCreature("meshes/r/cliffracer.nif");

        /// How far apart the two furthest vertices of `mesh` are between two scenes.
        ///
        /// A maximum rather than a mean, because most of a wing is near its root and barely moves;
        /// averaging a beat over the whole body is how a real animation comes out looking still.
        float travelled(const Rtx::SceneDesc& before, const Rtx::SceneDesc& after, Rtx::Index mesh)
        {
            const std::span<const osg::Vec3f> was = before.getMeshPositions(mesh);
            const std::span<const osg::Vec3f> is = after.getMeshPositions(mesh);
            EXPECT_EQ(was.size(), is.size()) << "a pose does not change how many vertices there are";

            float furthest = 0.0f;
            for (std::size_t at = 0; at < std::min(was.size(), is.size()); ++at)
                furthest = std::max(furthest, (is[at] - was[at]).length());

            return furthest;
        }

        /// An actor loads, poses, and poses differently at a different time.
        ///
        /// **The headless half of what the game window was being opened for.** A skinned body's
        /// vertices are computed during the cull traversal, so a harness that only walks the graph
        /// sees the bind pose for ever; this drives the real traversals and asserts the vertices
        /// actually moved.
        ///
        /// It is written as one test over one loaded world because opening one costs about a second:
        /// the assertions are cheap and the fixture is not.
        TEST(RtxActorTest, aCreatureLoadsPosedAndMovesBetweenPoses)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            Actor actor(*world, loadCreature(*world, sCreature), osg::Matrixf::identity());

            EXPECT_EQ(actor.getSkeleton().value(), "meshes/r/xcliffracer.nif")
                << "an actor's skeleton is a second file beside its model";
            ASSERT_GT(actor.getPosedBones(), 0u) << "keyframes that reach no bone pose nothing";
            ASSERT_GT(actor.getDuration(), 0.0f);

            // Two poses a quarter of the track apart, which for a wing beat is most of the way from
            // one extreme to the other whatever the track happens to contain.
            Rtx::SceneDesc first;
            {
                RtxBridge::SceneExtractor extractor(first);
                actor.pose(0.0f);
                const RtxBridge::ExtractionStats stats = extractor.extract(actor.getRoot(), actor.getTransform());

                EXPECT_GT(stats.mDeformed, 0u) << "a creature is skinned geometry";
                EXPECT_EQ(stats.mSkippedUnknown, 0u);
                ASSERT_GT(stats.mInstances, 0u);
            }

            Rtx::SceneDesc second;
            {
                RtxBridge::SceneExtractor extractor(second);
                actor.pose(actor.getDuration() * 0.25f);
                extractor.extract(actor.getRoot(), actor.getTransform());
            }

            ASSERT_EQ(first.getMeshes().size(), second.getMeshes().size());

            float furthest = 0.0f;
            for (Rtx::Index mesh = 0; mesh < first.getMeshes().size(); ++mesh)
                furthest = std::max(furthest, travelled(first, second, mesh));

            // A Morrowind unit is about seven centimetres and a cliff racer's wings span some three
            // hundred of them, so a quarter of a beat is tens of units. Ten is well under anything a
            // beat does and well over anything rounding could produce.
            EXPECT_GT(furthest, 10.0f) << "the two poses are the same pose";

            // And back where it started, which is the assertion that says the pose is a function of
            // the time rather than a walk that accumulates: an actor stepped round its own loop has
            // to come back, or a long run drifts.
            Rtx::SceneDesc again;
            {
                RtxBridge::SceneExtractor extractor(again);
                actor.pose(actor.getDuration());
                extractor.extract(actor.getRoot(), actor.getTransform());
            }

            float drifted = 0.0f;
            for (Rtx::Index mesh = 0; mesh < first.getMeshes().size(); ++mesh)
                drifted = std::max(drifted, travelled(first, again, mesh));

            EXPECT_LT(drifted, 1e-3f) << "a whole track later is the pose it started in";
        }

        /// What one assembled person came to.
        struct Assembled
        {
            VFS::Path::Normalized mSkeleton;
            std::uint32_t mInstances = 0;
            std::uint32_t mDeformed = 0;
        };

        /// A person is assembled out of their race's body parts, and race and sex decide which.
        ///
        /// **The equal counts are the assertion that matters.** Morrowind's base animation files
        /// carry placeholder geometry — the female one most visibly — and a person built without
        /// stripping it comes out wearing a magenta torso and a grey chevron. That defect shows up
        /// here as a woman made of more meshes than a man of the same race, and as nothing else.
        TEST(RtxActorTest, aPersonIsAssembledFromTheirRacesBodyParts)
        {
            Files::ConfigurationManager config;
            bpo::variables_map variables;
            const std::unique_ptr<World> world = openWorld(config, variables);
            if (world == nullptr)
                GTEST_SKIP() << "no Morrowind installation configured";

            Rtx::SceneDesc scene;
            RtxBridge::SceneExtractor extractor(scene);

            const auto assemble = [&](const char* id) {
                const ESM::NPC* who = findNpc(*world, id);
                EXPECT_NE(who, nullptr) << id;
                if (who == nullptr)
                    return Assembled{};

                Actor actor(*world, buildNpc(*world, *who), osg::Matrixf::identity());
                actor.pose(0.0f);

                const RtxBridge::ExtractionStats stats = extractor.extract(actor.getRoot(), actor.getTransform());
                return Assembled{ actor.getSkeleton(), stats.mInstances, stats.mDeformed };
            };

            const Assembled man = assemble("madres navur");
            const Assembled woman = assemble("galsa gindu");
            const Assembled cat = assemble("ra'virr");

            ASSERT_GT(man.mInstances, 0u) << "a person made of nothing is not a person";
            EXPECT_EQ(man.mInstances, woman.mInstances)
                << "two Dunmer are the same body part for body part, so the skeleton brought geometry of its own";

            // **Some of a person is skinned and most of one is not**, which is worth pinning because
            // it decides how they move: a forearm is a rigid mesh parented to the forearm bone and
            // rides the transform, and only the pieces that span a joint — the chest, the hands —
            // are deformed vertex by vertex. Both routes have to work or half a person animates.
            EXPECT_GT(woman.mDeformed, 0u) << "the parts that span a joint are skinned";
            EXPECT_LT(woman.mDeformed, woman.mInstances) << "and the rest ride their bone";

            // Three races and sexes, three skeletons — and the beast one serves both sexes, which is
            // Morrowind's own arrangement rather than something to correct.
            EXPECT_NE(man.mSkeleton, woman.mSkeleton) << "a woman is not built on a man's skeleton";
            EXPECT_NE(cat.mSkeleton, man.mSkeleton) << "and a Khajiit is built on neither";
            EXPECT_NE(cat.mSkeleton, woman.mSkeleton);
        }
    }
}
