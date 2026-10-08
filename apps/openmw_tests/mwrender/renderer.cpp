#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_ITexture.h>
#include <SDL3/SDL_video.h>
#include <osg/Camera>
#include <osg/FrameStamp>
#include <osg/Group>
#include <osg/Image>
#include <osg/Stats>
#include <osg/Timer>
#include <osg/Vec2i>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/ground.hpp>
#include <apps/openmw/mwrender/mapoverlay.hpp>
#include <apps/openmw/mwrender/offscreenview.hpp>
#include <apps/openmw/mwrender/renderer.hpp>
#include <apps/openmw/mwrender/renderingmanager.hpp>
#include <apps/openmw/mwrender/rendermode.hpp>
#include <apps/openmw/mwrender/rendersupport.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/misc/frameclock.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWRender
{
    namespace
    {
        /// Water declined whole and one of its keys honoured by name, which beats the category, and
        /// one key of another category declined.
        constexpr auto sDeclared = std::to_array<SettingSupport>({
            { "Water", "", "no water here" },
            { "Water", "shader", {} },
            { "RTX", "upscale", "no upscaler here" },
        });
        constexpr std::array sDeclaredModes{ ModeSupport{ Render_Wireframe, "no wireframe here" } };
        constexpr std::array sDeclaredRequests{ RequestSupport{ ScriptRequest::ShaderReload, "no shaders here" } };
        constexpr RenderSupport sSupport(sDeclared, sDeclaredModes, sDeclaredRequests);

        /// A renderer that draws nothing and records what the seam tells it about the world.
        class RecordingRenderer final : public Renderer
        {
        public:
            /// Whether the world was to be drawn, at each `applyWorldShown`.
            /// What each `applyWorldShown` left a frame to draw: the world's view mask, or nothing under
            /// a cover.
            std::vector<unsigned int> mApplied;

            /// The simulation time of each `advance`, and how many times the interface was drawn.
            std::vector<double> mAdvanced;
            std::size_t mGuiDrawn = 0;

            /// How many times the presentation was applied.
            std::size_t mPresented = 0;

            /// What each settings change handed over.
            std::vector<Settings::CategorySettingVector> mHonoured;

            /// How many times a world was attached and detached.
            std::size_t mAttached = 0;
            std::size_t mDetached = 0;

            using Renderer::adopt;
            using Renderer::getLastHold;
            using Renderer::presentIn;

            void configureResources(Resource::ResourceSystem&) override {}
            SDL_Window* getWindow() const override { return nullptr; }
            std::unique_ptr<Ground> createGround(const GroundSpec&) override { return nullptr; }
            float getGroundReach() const override { return 0.0f; }
            osg::ref_ptr<osg::Group> createSceneRoot() override { return new osg::Group; }
            void advance(double simulationTime) override { mAdvanced.push_back(simulationTime); }
            void eventTraversal() override {}
            void updateTraversal() override {}
            void renderFrame(const SceneFrame&) override {}
            std::unique_ptr<OffscreenView> createWorldView(const OffscreenViewSpec&) override { return nullptr; }
            std::unique_ptr<SubjectView> createSubjectView(const OffscreenViewSpec&) override { return nullptr; }
            std::unique_ptr<MapOverlay> createMapOverlay(const MapOverlaySpec&) override { return nullptr; }
            MyGUI::ITexture& freezeFrame() override { throw std::logic_error("not asked"); }
            void renderGui() override { ++mGuiDrawn; }
            void capture(osg::Image&, int, int) override {}
            void saveScreenshot() override {}
            void setVSync(SDLUtil::VSyncMode) override {}
            osg::Timer_t getStartTick() const override { return 0; }
            const RenderSupport& support() const override { return sSupport; }
            std::unique_ptr<MyGUIPlatform::Platform> createGuiPlatform(
                float, VFS::Path::NormalizedView, const std::filesystem::path&) override
            {
                return nullptr;
            }

        protected:
            void onAttachWorld(RenderingManager&, osg::Group&, SceneUtil::UnrefQueue&) override { ++mAttached; }
            void onDetachWorld() override { ++mDetached; }
            void adoptTraversalRoot(osg::Group&) override {}
            void applyViewMask() override {}
            void applyWorldShown() override { mApplied.push_back(isWorldShown() ? worldViewMask() : 0u); }
            void applyPresentation() override { ++mPresented; }
            void applyChangedSettings(const Settings::CategorySettingVector& honoured) override
            {
                mHonoured.push_back(honoured);
            }
        };

        /// **A declaration answers by the key, then by its category, and honours what it does not
        /// name**, and a renderer is handed only the changed settings it honours, and nothing at all
        /// for a change it declines whole.
        TEST(RendererTest, aRendererIsHandedTheChangedSettingsItsDeclarationHonours)
        {
            EXPECT_EQ(sSupport.declinedSetting("Water", "refraction"), "no water here") << "by the category";
            EXPECT_EQ(sSupport.declinedSetting("Water", "shader"), "") << "the key beats its category";
            EXPECT_EQ(sSupport.declinedSetting("RTX", "upscale"), "no upscaler here");
            EXPECT_EQ(sSupport.declinedSetting("RTX", "enabled"), "") << "a key the declaration does not name";
            EXPECT_TRUE(sSupport.namesSetting("Water", "refraction"));
            EXPECT_FALSE(sSupport.namesSetting("RTX", "enabled"));
            EXPECT_EQ(sSupport.declinedMode(Render_Wireframe), "no wireframe here");
            EXPECT_EQ(sSupport.declinedMode(Render_Pathgrid), "");
            EXPECT_EQ(sSupport.declinedRequest(ScriptRequest::ShaderReload), "no shaders here");
            EXPECT_EQ(sSupport.declinedRequest(ScriptRequest::LiveShaderReload), "");
            EXPECT_EQ(notAvailable("Wireframe Rendering", sSupport.declinedMode(Render_Wireframe)),
                "Wireframe Rendering -> not available under this renderer: no wireframe here")
                << "what the console says in place of a state";

            RecordingRenderer renderer;
            renderer.processChangedSettings({ { "Water", "refraction" }, { "Water", "shader" }, { "RTX", "upscale" },
                { "Camera", "field of view" } });
            renderer.processChangedSettings({ { "Water", "refraction" }, { "RTX", "upscale" } });

            ASSERT_EQ(renderer.mHonoured.size(), 1u) << "a change of declined settings alone was handed over";
            EXPECT_EQ(renderer.mHonoured[0],
                (Settings::CategorySettingVector{ { "Water", "shader" }, { "Camera", "field of view" } }));
        }

        /// **A cover that ends while `tws` is off leaves the world hidden, and `tws` under a cover
        /// brings nothing back.** The two are two reasons to the game and two answers to a frame:
        /// a cover draws nothing, and `tws` draws the view less the world's own bits — the sky, the
        /// water, the player and the effects stay. The renderer hears about each change once: the
        /// window manager asks every frame.
        TEST(RendererTest, aCoverAndTwsAreTwoReasonsAndTwoAnswers)
        {
            constexpr unsigned int hidden = ~sToggleWorldMask;

            RecordingRenderer renderer;
            EXPECT_EQ(renderer.worldViewMask(), ~0u);

            renderer.showWorld(false);
            renderer.showWorld(false);
            EXPECT_FALSE(renderer.isWorldShown());
            EXPECT_TRUE(renderer.isWorldToggled());
            EXPECT_EQ(renderer.mApplied, (std::vector<unsigned int>{ 0u }));

            EXPECT_FALSE(renderer.toggleRenderMode(Render_Scene));
            EXPECT_FALSE(renderer.isWorldToggled());
            EXPECT_EQ(renderer.mApplied, (std::vector<unsigned int>{ 0u, 0u }));

            renderer.showWorld(true);
            EXPECT_TRUE(renderer.isWorldShown());
            EXPECT_EQ(renderer.mApplied, (std::vector<unsigned int>{ 0u, 0u, hidden }));

            // The view mask's own word survives `tws`: the host's camera inside the player.
            renderer.setViewMask(~static_cast<unsigned int>(Mask_Player));
            EXPECT_EQ(renderer.worldViewMask(), hidden & ~static_cast<unsigned int>(Mask_Player));
            renderer.setViewMask(~0u);

            EXPECT_TRUE(renderer.toggleRenderMode(Render_Scene));
            EXPECT_EQ(renderer.mApplied, (std::vector<unsigned int>{ 0u, 0u, hidden, ~0u }));

            // The rest are the game's own nodes, and a renderer that has none says so.
            EXPECT_FALSE(renderer.toggleRenderMode(Render_Wireframe));
            EXPECT_EQ(renderer.mApplied.size(), 4u);
        }

        /// **`awaitFrame` is upstream's own frame limiter**: it sleeps to the limit and answers the
        /// limit's own length where it slept, and the wall where it did not. Both renderers keep
        /// exactly the pacing upstream had, one call earlier in the loop. What it slept is the hold
        /// `getLastHold` says, which is the frame's `Sleep` row.
        TEST(RendererTest, awaitFrameSleepsToTheLimitAndAnswersIt)
        {
            using Clock = std::chrono::steady_clock;

            RecordingRenderer renderer;
            renderer.setFrameRateLimit(200.0f);
            const Clock::duration limit
                = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(1.0f / 200.0f));

            // The first frame after the limit was set, which nothing before it can have made late:
            // on a Windows runner a 5 ms sleep came back 5.9 ms later, so a second frame measured
            // after a first one had nothing left to sleep.
            const Clock::time_point began = Clock::now();
            const Clock::duration stood = renderer.awaitFrame();
            const Clock::duration slept = Clock::now() - began;
            EXPECT_EQ(stood, limit) << "a frame it slept for stood for the limit, as the limiter answers";
            EXPECT_GE(slept, std::chrono::milliseconds(4)) << "and it slept for it";
            EXPECT_GE(renderer.getLastHold(), std::chrono::milliseconds(4)) << "and says it held";
            EXPECT_LE(renderer.getLastHold(), slept) << "no longer than the wall saw";

            renderer.setFrameRateLimit(0.0f);
            renderer.awaitFrame();
            const Clock::time_point again = Clock::now();
            const Clock::duration free = renderer.awaitFrame();
            EXPECT_LT(Clock::now() - again, std::chrono::milliseconds(2)) << "no limit is no sleep";
            EXPECT_LT(free, std::chrono::milliseconds(2)) << "and the wall is what stood";
            EXPECT_LT(renderer.getLastHold(), std::chrono::milliseconds(2)) << "and nothing held it";
        }

        /// **A nested frame is opened as the loop opens its own**: held to the limit, and a wall
        /// clock moved on by what it stood for, so a loading screen or a message box stamps and
        /// steps the interface by its own time and not the outer frame's. At 200 frames a second the
        /// hold is 5 ms, so the step is never less, and the frame answers the step the clock took.
        /// How much more is the system's sleep: on a Windows runner, a frame held for 5 ms stood for 10.5.
        ///
        /// **A clock that states its step moves by the loop's frames alone**: a nested frame of a
        /// measured run stands for nothing and leaves the clock where the loop put it, because how
        /// many a run draws is the wall's answer.
        TEST(RendererTest, aNestedFrameStepsAWallClockAndLeavesAStatedOne)
        {
            RecordingRenderer renderer;
            renderer.setFrameRateLimit(200.0f);

            Misc::FrameClock wall;
            renderer.setFrameClock(wall);
            renderer.awaitFrame();
            const float step = renderer.openNestedFrame();
            EXPECT_EQ(step, static_cast<float>(wall.getStep()));
            EXPECT_GE(wall.getStep(), 0.005);

            Misc::FrameClock stated(1.0f / 60.0f);
            renderer.setFrameClock(stated);
            EXPECT_EQ(renderer.openFrame(), static_cast<double>(1.0f / 60.0f))
                << "the loop's frame takes the stated step";
            const double now = stated.getNow();
            EXPECT_EQ(now, static_cast<double>(1.0f / 60.0f));
            EXPECT_EQ(renderer.openNestedFrame(), 0.0f);
            EXPECT_EQ(stated.getNow(), now) << "a nested frame moved a clock whose steps are the loop's";

            // **And whatever reads the clock inside the nested frame reads the same nought**: the
            // interface stepped by the clock's step, which was still the loop frame's, once for
            // every loading frame the wall happened to draw.
            EXPECT_EQ(stated.getStep(), 0.0) << "the nested frame left the loop frame's step open";

            EXPECT_EQ(renderer.openFrame(), static_cast<double>(1.0f / 60.0f));
            EXPECT_EQ(stated.getNow(), 2.0 * static_cast<double>(1.0f / 60.0f));
        }

        /// **A hidden window's interface frame draws nothing and ends as a drawn one does**: with the
        /// advance at the frame's own simulation time, so the frame the caller is in the middle of
        /// is numbered right whether or not the window is shown.
        TEST(RendererTest, aHiddenGuiFrameDrawsNothingAndAdvancesAsADrawnOne)
        {
            RecordingRenderer renderer;
            const osg::ref_ptr<osg::Camera> camera = new osg::Camera;
            const osg::ref_ptr<osg::FrameStamp> stamp = new osg::FrameStamp;
            const osg::ref_ptr<osg::Stats> stats = new osg::Stats("renderer test");
            renderer.adopt(*camera, *stamp, *stats);
            stamp->setSimulationTime(12.5);

            renderer.renderGuiFrame();
            EXPECT_EQ(renderer.mGuiDrawn, 1u);
            renderer.skipGuiFrame();
            EXPECT_EQ(renderer.mGuiDrawn, 1u) << "a hidden window's frame drew the interface";
            EXPECT_EQ(renderer.mAdvanced, (std::vector<double>{ 12.5, 12.5 }));
        }

        /// **The presentation is applied when it moves, and only then**: a window resized, another
        /// resolution chosen, and nothing for either said again. 1280 × 720 in 2560 × 1080 fills the
        /// height, 1280 × 1080 / 720 = 1920 wide, and (2560 − 1920) / 2 = 320 beside it.
        TEST(RendererTest, thePresentationIsAppliedWhenItMovesAndOnlyThen)
        {
            Settings::video().mResolutionX.set(0);
            Settings::video().mResolutionY.set(0);
            RecordingRenderer renderer;

            renderer.presentIn(osg::Vec2i(1920, 1080));
            renderer.presentIn(osg::Vec2i(1920, 1080));
            EXPECT_EQ(renderer.mPresented, 1u);
            EXPECT_EQ(renderer.getPresentation().mFrame, osg::Vec2i(1920, 1080)) << "Native";

            Settings::video().mResolutionX.set(1280);
            Settings::video().mResolutionY.set(720);
            const Settings::CategorySettingVector resized{ { "Video", "resolution x" }, { "Video", "resolution y" } };
            renderer.processChangedSettings(resized);
            renderer.processChangedSettings(resized);
            EXPECT_EQ(renderer.mPresented, 2u) << "both halves of one change are one presentation";
            renderer.processChangedSettings({ { "Camera", "field of view" } });
            EXPECT_EQ(renderer.mPresented, 2u) << "a change of anything else presented the frame again";
            EXPECT_EQ(renderer.getPresentation().mFrame, osg::Vec2i(1280, 720));
            EXPECT_EQ(renderer.getPresentation().mShownSize, osg::Vec2i(1920, 1080));

            renderer.presentIn(osg::Vec2i(2560, 1080));
            EXPECT_EQ(renderer.mPresented, 3u);
            EXPECT_EQ(renderer.getPresentation().mFrame, osg::Vec2i(1280, 720)) << "the frame kept its size";
            EXPECT_EQ(renderer.getPresentation().mShownOrigin, osg::Vec2i(320, 0));
            EXPECT_EQ(renderer.getPresentation().mShownSize, osg::Vec2i(1920, 1080));

            Settings::video().mResolutionX.set(0);
            Settings::video().mResolutionY.set(0);
        }

        /// The storage a world would stand in, which none does: what `attachWorld` is handed where
        /// the renderer reads nothing of the world. A reference bound to an object whose lifetime
        /// has not begun is one of the uses the language allows.
        union UnbuiltWorld
        {
            UnbuiltWorld() {}
            ~UnbuiltWorld() {}

            RenderingManager mWorld;
        };

        /// **A world is detached once for each attach, by whatever ends its attachment**: an owner
        /// whose constructor throws after the attach, a reset, an end of scope, and an assignment
        /// over it. A moved-from attachment detaches nothing.
        TEST(RendererTest, aWorldIsDetachedOnceByWhateverEndsItsAttachment)
        {
            RecordingRenderer renderer;
            UnbuiltWorld unbuilt;
            const osg::ref_ptr<osg::Group> root = new osg::Group;
            SceneUtil::UnrefQueue queue;

            struct Throwing
            {
                WorldAttachment mAttachment;

                Throwing(Renderer& renderer, RenderingManager& world, osg::Group& root, SceneUtil::UnrefQueue& queue)
                {
                    mAttachment = renderer.attachWorld(world, root, queue);
                    throw std::runtime_error("after the attach");
                }
            };
            EXPECT_THROW({ const Throwing owner(renderer, unbuilt.mWorld, *root, queue); }, std::runtime_error);
            EXPECT_EQ(renderer.mAttached, 1u);
            EXPECT_EQ(renderer.mDetached, 1u) << "a constructor that threw after the attach";

            {
                WorldAttachment first = renderer.attachWorld(unbuilt.mWorld, *root, queue);
                WorldAttachment moved = std::move(first);
                first.reset();
                EXPECT_EQ(renderer.mDetached, 1u) << "the moved-from attachment";
                moved.reset();
                EXPECT_EQ(renderer.mDetached, 2u) << "a reset";
                moved.reset();
                EXPECT_EQ(renderer.mDetached, 2u) << "a second reset";
            }

            {
                const WorldAttachment scoped = renderer.attachWorld(unbuilt.mWorld, *root, queue);
            }
            EXPECT_EQ(renderer.mDetached, 3u) << "an end of scope";

            WorldAttachment replaced = renderer.attachWorld(unbuilt.mWorld, *root, queue);
            replaced = WorldAttachment();
            EXPECT_EQ(renderer.mDetached, 4u) << "an assignment over it";
            EXPECT_EQ(renderer.mAttached, 4u);
        }

        /// The two kinds by the words the log and a crash report have always named them by.
        TEST(RendererTest, eachKindIsNamedByTheWordTheLogPrints)
        {
            EXPECT_EQ(nameOf(RendererKind::OpenGl), "opengl");
            EXPECT_EQ(nameOf(RendererKind::RayTraced), "raytrace");
        }
    }
}
