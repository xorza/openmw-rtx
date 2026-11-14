#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/displaycurve.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/debuglines.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/moon.hpp>
#include <components/rtx/world/sunglare.hpp>
#include <components/rtx/world/wavecascade.hpp>
#include <components/rtx/world/wavespectrum.hpp>
#include <components/rtxvulkan/scene/sceneacceleration.hpp>
#include <components/rtxvulkan/scene/scenebuffers.hpp>
#include <components/rtxvulkan/scene/skinpass.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx::Testing
{
    /// A level sheet of water `extent` across at z = 0, with nothing under it.
    inline SceneDesc makeOpenWater(float extent)
    {
        SceneDesc scene;
        Material water;
        water.mKind = MaterialKind::Water;

        // What `MaterialResolver::resolveWater` says of the sea, because a swimmer looks up at the
        // surface from under it and a ray that draws culls everything the content shows one face of.
        water.mTwoSided = true;
        addQuad(scene, sheetAt(extent, 0.0f), scene.addMaterial(water));

        return scene;
    }

    /// A level bed `depth` units under a level surface of water, both `extent` across.
    ///
    /// The shape most of the water tests want: something to see through the water at, and the
    /// water to see it through.
    inline SceneDesc makeFlooded(float extent, float depth)
    {
        SceneDesc scene = makeOpenWater(extent);
        addQuad(scene, sheetAt(extent, -depth));

        return scene;
    }

    /// How bright the sun is in the tests that measure through water. Named because their arithmetic uses it
    /// too: the shader's number and an expectation's must be one number.
    inline constexpr float sSunOverWater = 2.0f;

    /// Where the sun stands, from how far it is off the vertical. Its light travels back along
    /// this, which is the only sun vector the shader has.
    inline osg::Vec3f sunStandingAt(float zenith)
    {
        return osg::Vec3f(0.0f, -std::sin(zenith), std::cos(zenith));
    }

    /// Where the sun stands over the tests that measure through water, and not straight up: overhead, its mirror
    /// image lands where a camera looking straight down sends its reflection ray, and a saturated disc in the
    /// middle of the frame is not what these measure. Two degrees puts it twelve disc-widths clear and costs
    /// the arithmetic a part in a thousand, which `throughFlatWater` accounts for.
    inline const float sNearlyOverhead = osg::DegreesToRadians(2.0f);

    /// Puts `camera` over a flooded scene with nothing in its sky but the sun.
    ///
    /// The water level is the one `makeFlooded` builds to and the sky is black, so apart from the sun's disc
    /// every byte in the frame has come up through the depth, which lets these tests name an exact value.
    inline void litThroughWater(Shaders::VisibilityConstants& camera, float zenith = sNearlyOverhead)
    {
        camera.mSun
            = Shaders::sunSource(sunStandingAt(zenith), osg::Vec3f(sSunOverWater, sSunOverWater, sSunOverWater));
        camera.mSkyHorizon = osg::Vec3f();
        camera.mSkyZenith = osg::Vec3f();
        camera.mWaterLevel = 0.0f;
    }

    /// An orthographic eye ten degrees over the water, which it sees from 500 to 100 units short of y = 0:
    /// every pixel's rays are parallel to every other's, so Schlick's term is one number,
    /// `0.02 + 0.98 (1 - sin 10°)^5` = 0.40. The reflection meets y = 0 `0.176 |y|` up, 17.6 to 88 units (the
    /// height of `sGrazedWall`); the refraction bends to 47.6 degrees off the vertical and travels 1.09 of
    /// whatever depth it crosses toward it.
    inline Shaders::VisibilityConstants grazingTheWater(std::uint32_t size)
    {
        const float grazing = osg::DegreesToRadians(10.0f);
        const osg::Vec3f along(0.0f, std::cos(grazing), -std::sin(grazing));
        const osg::Vec3f middle(0.0f, -300.0f, 0.0f);
        const osg::Matrixf view = osg::Matrixf::lookAt(middle - along * 1000.0f, middle, osg::Vec3f(0.0f, 0.0f, 1.0f));

        // 400 units of water along the view, which the frame sees `sin 10°` of.
        const float across = 400.0f * std::sin(grazing);
        Shaders::VisibilityConstants camera
            = constantsFor(makeOrthographicCameraFromView(view, across, across, size, size, 1.0f, 5000.0f).value());
        camera.mWaterLevel = 0.0f;
        return camera;
    }

    /// The wall `grazingTheWater` reflects: a hundred units square standing on the water at y = 0,
    /// facing back at the eye, and wider than the frame.
    inline const std::array<osg::Vec3f, 4> sGrazedWall = uprightQuadAt(50.0f, 0.0f, osg::Vec2f(0.0f, 50.0f));

    /// The wall `sWallQuad` names, on its own, as a scene.
    ///
    /// @param scale what to stretch it by, for a frame taken far enough away that four hundred
    ///        units is a fraction of one pixel.
    inline SceneDesc makeWall(float scale = 1.0f)
    {
        SceneDesc scene;
        addQuad(scene, sWallQuad, std::nullopt, osg::Matrixf::scale(scale, 1.0f, scale));
        return scene;
    }

    /// One see-through quad in `scene`, of `colour` and its own alpha, faded as the game fades a
    /// placement. Returns its slot, which a test moves it by.
    ///
    /// **The two numbers an opacity is made of, added the one way.** The shader multiplies a material's alpha
    /// by a placement's fade, and a helper that built either its own way would hold up a surface this renderer
    /// does not have.
    inline Index addPane(SceneDesc& scene, std::span<const osg::Vec3f, 4> quad, const osg::Vec4f& colour,
        float fade = 1.0f, bool twoSided = false, InstanceClass kind = InstanceClass::Static)
    {
        // A test states a pane as a colour and how much of it there is, which is the pair the
        // record states too. Linear already, so there is nothing to decode: `Rtx::decodeColour` is
        // for what a content file wrote.
        const Index glass = scene.addMaterial(Material{
            .mDiffuseColour = osg::Vec3f(colour.r(), colour.g(), colour.b()),
            .mOpacity = colour.a(),
            .mAlphaMode = AlphaMode::Blend,
            .mTwoSided = twoSided,
        });

        return scene.addInstance(
            MeshInstance{ .mMesh = addQuadMesh(scene, quad), .mMaterial = glass, .mOpacity = fade, .mClass = kind });
    }

    /// The sine of a sun shadow cone of two degrees, for a test whose arithmetic needs a cone of its
    /// own and not the look's `SUN_SHADOW_RADIUS`, which is a choice about the picture and may move.
    inline float twoDegreeLimb()
    {
        return std::sin(2.0f * Shaders::PI / 180.0f);
    }

    /// A floor under an overhead sun, seen from above, with the sky black so the sun is the only
    /// light: the bounce escapes to nothing and the frame is the sun's term alone.
    inline Shaders::VisibilityConstants overheadSun(std::uint32_t size)
    {
        Shaders::VisibilityConstants camera
            = makeCamera(osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
        camera.mSkyHorizon = osg::Vec3f();
        camera.mSkyZenith = osg::Vec3f();
        camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
        return camera;
    }

    /// A roof over the floor, facing up, so a shadow ray from the floor meets its back face,
    /// which the light's rule does not cull: `x` from `left` to `right` at `height`.
    inline std::array<osg::Vec3f, 4> roofOver(float left, float right, float height)
    {
        return {
            osg::Vec3f(left, -4000.0f, height),
            osg::Vec3f(right, -4000.0f, height),
            osg::Vec3f(right, 4000.0f, height),
            osg::Vec3f(left, 4000.0f, height),
        };
    }

    /// The eye and the sun every test over that wall stands it under: the sun along +Y, square to the wall,
    /// and the eye off that axis at x = 100 looking at the middle of it.
    ///
    /// One camera, because each test is read as a ratio against the wall's own byte and the figures hold only
    /// while the wall, eye and sun are the same in all of them; off the sun's axis lets a pane on the eye's
    /// ray leave the patch of wall the centre pixel sees fully lit.
    ///
    /// @param origin,target where to aim it instead, for the one test that has to see the pane rather than
    ///        what it shadows.
    inline Shaders::VisibilityConstants wallCamera(std::uint32_t size, const osg::Vec3f& irradiance,
        const osg::Vec3f& origin = osg::Vec3f(100.0f, -100.0f, 0.0f), const osg::Vec3f& target = osg::Vec3f())
    {
        Shaders::VisibilityConstants camera = Testing::makeCamera(origin, target, 60.0f, size, size, 10000.0f);
        camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, -1.0f, 0.0f), irradiance);

        return camera;
    }

    /// Which pixel of a `size` by `size` frame is its middle one — `size / 2` along each axis,
    /// which is the pixel just past the centre where `size` is even.
    inline constexpr std::size_t centreOf(std::uint32_t size)
    {
        return std::size_t{ size / 2 } * size + size / 2;
    }

    /// The first of the four values that pixel holds, which is how a read-back over this fixture is
    /// indexed: a byte frame and a radiance frame both carry four values a pixel.
    inline constexpr std::size_t centreValueOf(std::uint32_t size)
    {
        return centreOf(size) * 4;
    }

    /// How brightly the sky is lit, in the tests that measure a wall through fog or against the world's edge.
    /// Named like `sSunOverWater`: the arithmetic and the shader must use one number.
    ///
    /// The sky and not the cell's ambient, because a wall is lit by what it can see: a sky of one radiance
    /// makes the hemisphere gather exact, every direction returning the same number, so one sample is the
    /// whole answer.
    inline constexpr float sFoggySky = 0.6f;

    /// A texture that is white and wholly opaque, at one level.
    ///
    /// `paintMipLadder` cannot be: its levels encode which one was sampled, so its first alpha is 40 of 255
    /// and a sprite cut from it covers a sixth of what is behind it, no use for asking what happens when a
    /// sprite owns a pixel.
    inline void paintOpaqueSheet(TestTexture& texture)
    {
        constexpr std::uint32_t extent = 4;

        texture.mLevels.push_back(MipLevel{ 0, extent, extent });
        texture.mBytes.assign(std::size_t{ extent } * extent * 4, std::uint8_t{ 255 });

        texture.describe(extent, extent, "opaque sheet");
    }

    /// What one render over this fixture came to: the composite before the display curve, how many primary
    /// rays hit, and what was not finite.
    ///
    /// The radiance and not the picture: `tone.comp`'s curve is a display transform that takes 0.04 off a
    /// shadow and rolls a highlight away, which a test about what the trace computed has no opinion on. A
    /// byte is the display curve over the same radiance, computed when asked for.
    struct Frame
    {
        /// Four values a pixel, row major.
        std::vector<float> mRadiance;

        std::uint32_t mHits = 0;
        /// What every frame of the run stored that was not finite, summed so far.
        Rtx::NotFinite mNotFinite;

        float at(std::size_t value) const { return mRadiance[value]; }

        /// The mean of one channel over the frame.
        ///
        /// The frame and not a pixel, where every pixel is the same measurement: the estimator is one sample a
        /// pixel, so an evenly lit frame's mean has that error divided down by the pixel count, which lets a
        /// test hold a derived number to three decimals over a stochastic renderer.
        float mean(std::size_t channel = 0) const
        {
            float sum = 0.0f;
            for (std::size_t value = channel; value < mRadiance.size(); value += 4)
                sum += mRadiance[value];

            return sum / static_cast<float>(mRadiance.size() / 4);
        }

        /// The root mean square of one channel's difference from `reference`, over the frame: how
        /// far a noisy or a filtered frame stands from a converged one.
        float errorFrom(const Frame& reference, std::size_t channel = 0) const
        {
            float squares = 0.0f;
            for (std::size_t value = channel; value < mRadiance.size(); value += 4)
            {
                const float difference = mRadiance[value] - reference.mRadiance[value];
                squares += difference * difference;
            }

            return std::sqrt(squares / static_cast<float>(mRadiance.size() / 4));
        }

        /// The root mean square of one channel over the frame.
        float rootMeanSquare(std::size_t channel = 0) const
        {
            float squares = 0.0f;
            for (std::size_t value = channel; value < mRadiance.size(); value += 4)
                squares += mRadiance[value] * mRadiance[value];

            return std::sqrt(squares / static_cast<float>(mRadiance.size() / 4));
        }

        /// The byte a test names for one value: the display curve over a colour, and coverage,
        /// which the curve does not touch, over the fourth.
        std::uint8_t byte(std::size_t value) const
        {
            if (value % 4 == 3)
                return static_cast<std::uint8_t>(std::lround(std::clamp(mRadiance[value], 0.0f, 1.0f) * 255.0f));

            return encodeSrgb(mRadiance[value]);
        }

        /// Every value as its byte.
        std::vector<std::uint8_t> bytes() const
        {
            std::vector<std::uint8_t> all(mRadiance.size());
            for (std::size_t value = 0; value < all.size(); ++value)
                all[value] = byte(value);

            return all;
        }
    };

    /// The root mean square by which a running mean of `frames` frames, kept in halves rounded at random
    /// `roundings` times a frame, stands from the mean of its frames in float: the root of its expected
    /// square, at most, which a frame of thousands of pixels drawn apart stands close to.
    ///
    /// A rounding moves a value `x` by a variance `f (1 - f) step²` for the fraction `f` it rounds, so by
    /// `(x 2^-11)²` at most. Frame `k` blends by `1 / k`, which carries the error it made to frame `n` times
    /// `k / n`, and frame `k`'s history is a mean of `k` frames, whose square stands at `level² + noise² / k`:
    /// `level` the converged frame's root mean square, `noise` one raw frame's error from it. The variances
    /// add:
    ///
    ///     error² <= roundings 2^-22 sum over k of (k / n)² (level² + noise² / k)
    inline float halfRoundedMeanError(float level, float noise, std::uint32_t frames, std::uint32_t roundings)
    {
        float variance = 0.0f;
        for (std::uint32_t k = 1; k <= frames; ++k)
        {
            const float carried = static_cast<float>(k) / static_cast<float>(frames);
            variance += carried * carried * (level * level + noise * noise / static_cast<float>(k));
        }

        return std::sqrt(static_cast<float>(roundings) * variance) * 0x1p-11f;
    }

    /// Everything a render over this fixture decides beyond the scene, the camera and the extent.
    ///
    /// Named and not positional: `SeaState{}, 16, false, true` reads like `SeaState{}, 16, true, false` at a
    /// call site, and the two are a jittered run and a filtered one.
    struct Shot
    {
        /// What the water is doing. A state with no height in it is a flat sea, which is what a
        /// test asserting an exact transmittance through one needs.
        SeaState mSea{};

        /// How many frames the run draws, each at its own sampler index counting from `mFirstFrame`, so the
        /// frames are different draws. Zero draws one frame at the camera's own index and leaves it there, for
        /// a test that sets `mFrame` itself.
        std::uint32_t mFrames = 0;

        /// Whether the composite averages the run into one picture, rather than leaving each frame to stand on
        /// its own.
        ///
        /// Off is what a test of the accumulator wants: averaged, nothing is left of how far the frames stood
        /// from each other. Nothing either way where `mFrames` is zero, since one frame is not a run.
        bool mAverage = true;

        /// The sampler's frame the run starts at, so a run can be handed a stream of its own rather
        /// than the one every other run in the test consumed.
        std::uint32_t mFirstFrame = 0;

        /// Whether the denoiser runs, and off by default on purpose. Almost every test over this
        /// fixture asserts a radiance a particular pixel must have, and a filter mixes its
        /// neighbours into it — a test that let one run would be measuring the denoiser rather than
        /// the thing it was written to measure. The tests that are about the filter ask for it.
        bool mFilter = false;

        /// Whether the filters hold their histories to their fast means (`FilterSwitches::mAntilag`):
        /// on, as a frame has it, unless a test holds a filter to the law of its own running mean,
        /// which a clamp to a box of a few frames' noise moves it off.
        bool mAntilag = true;

        /// Whether the sample point moves inside its pixel, which buys nothing on a single frame
        /// and is what several of them cover between them.
        bool mJitter = false;

        /// Levels added to every texture level the trace chooses — `ReconstructionRequest`'s
        /// epsilon, which with no upscaler in the fixture is the whole of the bias.
        float mLevelEpsilon = 0.0f;

        /// The share of a pixel's light under which a source draws no shadow bit —
        /// `ReconstructionRequest`'s, the default unless a test names another.
        float mShadowFloor = Shaders::SHADOW_DRAW_FLOOR;

        /// How many lamps a composing point draws — `ReconstructionRequest`'s, the default unless a
        /// test names another.
        std::uint32_t mLampCandidates = Shaders::LAMP_CANDIDATES;

        /// Whether the eye's surface traces bounces — `ReconstructionRequest`'s, on unless a test says.
        bool mIndirect = true;

        /// How many bounce rays the eye's surface traces — `ReconstructionRequest`'s, one unless a test
        /// names another.
        std::uint32_t mBounceRays = 1;

        /// Whether a split water pixel is blended with its split neighbours — `SamplingSwitches`', on
        /// unless a test names it off.
        bool mWaterInterleave = true;

        /// Where the trace draws from: the tile every figure over this fixture was derived against,
        /// unless a test names the other.
        NoiseSource mNoise = NoiseSource::BlueNoiseTile;

        /// What the past of the run's frame `mLossAt` is worth (`FrameOptions::mLoss`). A shot that sets its
        /// scene has lost every history at its first frame already, as a new world does, so a one-frame
        /// baseline taken after a longer run already has a history in it.
        HistoryLoss mLoss = HistoryLoss::None;
        std::uint32_t mLossAt = 0;

        /// The exposure the composite is held at. `std::nullopt` is the exposure the frame measures
        /// for itself, which is what a test about the exposure pass wants — and what every figure
        /// derived through `shoot` must not have, since those are about what the trace computed.
        std::optional<float> mExposure = 1.0f;

        /// `RenderProfile::mGamma` for the shot. One, which leaves the picture as the curve wrote it.
        float mGamma = 1.0f;

        /// How far the water's clock moves a frame, which the ripple field steps by and the waves run on; nought
        /// stands the field still. The clock stands `mFirstFrame` steps in on the run's first frame. The
        /// scene's `SceneDesc::addRipple` list is pressed once, on the run's first frame: a frame the clock did
        /// not move on presses nothing.
        float mWaterStep = 0.0f;

        /// What the debug modes drew, over the picture. Nothing, for every test not about it.
        DebugLines mDebug{};

        /// What every pixel is painted with, over the harness's profile, which paints the light
        /// (`describeRenderer`).
        std::optional<SurfaceView> mShow{};

        /// `RenderProfile::mLitEnvironmentMaps` over the harness's, which leaves a sheet unlit.
        std::optional<bool> mLitEnvironmentMaps{};

        /// `RenderProfile::mDither` over the harness's, which dithers nothing.
        std::optional<bool> mDither{};

        /// How far the world's clock moved since the frame before, `FrameOptions::mWorldSinceLast`,
        /// which a measured exposure adapts over. Nought, so a frame moves no eye, unless a test settles one.
        float mWorldSinceLast = 0.0f;

        /// `RenderProfile::mAnisotropy` for the shot. One, so the level a cone names is the level
        /// read, which is what every test that measures a level off the mip ladder relies on.
        std::uint32_t mAnisotropy = 1;

        /// Whether the shot hands its scene over, or places the one the shot before it stood, as a
        /// frame of the game does: for a test of a change that has to reach a scene already
        /// standing.
        bool mSetScene = true;

        /// A fixed offset in the pixel for every frame, where the shot does not jitter.
        std::optional<osg::Vec2f> mOffset{};

        /// The sun glare fader over the picture. None, for every test not about it.
        SunGlare mGlare{};

        /// What Night-Eye adds to the ambient (`WorldOptions::mNightEye`). None, for every test not
        /// about it.
        osg::Vec3f mNightEye{};

        /// Run once each frame of the run is finished, with that frame, for a caller measuring what
        /// moves between two frames rather than what a run of them averages to.
        std::function<void(const Frame&)> mEachFrame{};
    };

    /// A run of `frames` filtered frames with the history let build from nothing, each frame standing on its
    /// own.
    ///
    /// @param first the sampler's frame the run starts at, so a run gets a stream of its own.
    /// @param antilag off for a test of a filter's own law, which the clamp moves off it
    ///        (`Shot::mAntilag`).
    inline Shot filteredRun(std::uint32_t frames, std::uint32_t first = 0, bool antilag = true)
    {
        return Shot{ .mFrames = frames,
            .mAverage = false,
            .mFirstFrame = first,
            .mFilter = true,
            .mAntilag = antilag,
            .mLoss = HistoryLoss::Cut };
    }

    class RtxVisibilityTest : public Testing::RendererTest
    {
    protected:
        /// On the renderer the binary shares, validated but for `RtxSettlingTest`'s.
        explicit RtxVisibilityTest(Testing::Validation validation = Testing::Validation::Validated)
            : RendererTest(validation)
        {
        }

        /// Hands `scene` over as the world, with `textures` at the slots it numbers them by, as
        /// `shoot` hands its own: for a test that reads the shown picture, which `shoot` does not.
        void setWorld(const SceneDesc& scene, std::span<const TextureData> textures)
        {
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, inSceneOrder(scene, textures));
        }

        /// What `shoot` hands the renderer before its frames: `scene` at `size` square, and the
        /// shot's bounce rays, sea, anisotropy and gamma. For a test that draws its own frames.
        void prepare(
            const SceneDesc& scene, std::span<const TextureData> textures, std::uint32_t size, const Shot& shot = {})
        {
            // Before the size, so a move across one ray and a new size make the targets once.
            mRenderer.setBounceRays(shot.mBounceRays);
            mRenderer.resize(size, size);
            mProbe.setSea(shot.mSea);
            mRenderer.setAnisotropy(shot.mAnisotropy);
            mRenderer.setGamma(shot.mGamma);
            if (shot.mSetScene)
                setWorld(scene, textures);
            else
                mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
        }

        /// Draws `scene` at `size` square and returns the last frame of the shot.
        ///
        /// **The one render loop over this fixture**, so what a shot means is said once rather than
        /// once per way of reading the answer.
        Frame shoot(const SceneDesc& scene, std::span<const TextureData> textures,
            const Shaders::VisibilityConstants& camera, std::uint32_t size, const Shot& shot = {})
        {
            prepare(scene, textures, size, shot);

            // **Each frame's result collected a frame behind**, as the game runs, so the card draws one frame
            // while the host records the next: the queue orders them, and the renderer's own history barrier
            // makes each sum visible to the next. A shot that reads every frame's pixels waits each one out.
            const std::uint32_t drawn = std::max(shot.mFrames, 1u);
            Frame frame;
            // Every frame hits the same primary geometry, so the last one's count is the answer rather than a
            // sum to be divided back down.
            const auto collect = [&] {
                const std::optional<FrameResult> finished = mRenderer.finishFrame();
                if (!finished.has_value())
                    throw std::runtime_error("the renderer drew a frame and gave none back");

                frame.mHits = finished->mHits;
                frame.mNotFinite.add(finished->mNotFinite);
                EXPECT_EQ(finished->mWalksCut, 0u) << "pixels whose walk the trace cut short";
            };
            for (std::uint32_t at = 0; at < drawn; ++at)
            {
                Shaders::VisibilityConstants sampled = camera;
                if (shot.mFrames > 0)
                    sampled.mFrame = shot.mFirstFrame + at;
                const double waterSeconds
                    = static_cast<double>(shot.mFirstFrame + at) * static_cast<double>(shot.mWaterStep);
                if (shot.mWaterStep > 0.0f)
                    sampled.mWaterTime = splitSeconds(waterSeconds);
                mProbe.traceFrame(sampled,
                    FrameOptions{ .mAccumulate = shot.mFrames > 0 && shot.mAverage ? at + 1 : 0,
                        .mWorldSinceLast = shot.mWorldSinceLast,
                        .mLoss = at == shot.mLossAt ? shot.mLoss : HistoryLoss::None,
                        .mReconstruction = ReconstructionRequest{ .mDenoise = shot.mFilter,
                            .mJitter = shot.mJitter,
                            .mLevelEpsilon = shot.mLevelEpsilon,
                            .mNoise = shot.mNoise,
                            .mSampling = { .mShadowFloor = shot.mShadowFloor,
                                .mLampCandidates = shot.mLampCandidates,
                                .mIndirect = shot.mIndirect,
                                .mBounceRays = shot.mBounceRays,
                                .mWaterInterleave = shot.mWaterInterleave },
                            .mFilters = { .mAntilag = shot.mAntilag } },
                        .mExposure = shot.mExposure.has_value() ? ExposureRule(FixedExposure{ *shot.mExposure })
                                                                : ExposureRule(MeasuredExposure{}),
                        .mShow = shot.mShow,
                        .mLitEnvironmentMaps = shot.mLitEnvironmentMaps,
                        .mDither = shot.mDither,
                        .mJitter = shot.mOffset,
                        .mDebug = shot.mDebug },
                    WorldOptions{ .mGlare = shot.mGlare, .mNightEye = shot.mNightEye, .mWaterSeconds = waterSeconds });

                if (shot.mEachFrame)
                {
                    collect();
                    readFrame(size, frame);
                    shot.mEachFrame(frame);
                }
                else if (at > 0)
                    collect();
            }

            if (!shot.mEachFrame)
            {
                collect();
                readFrame(size, frame);
            }
            return frame;
        }

        /// The luminance of every pixel of a frame that holds nothing but air, from a camera standing in white
        /// fog of one thickness.
        ///
        /// A wall behind the camera, because a scene has to hold something: every ray runs to `FOG_REACH` and
        /// comes back with air and the sky, so the frame measures the air alone.
        ///
        /// @param camera has its fog colour and thickness set here; whatever else a test set on it stays.
        std::vector<float> airThrough(Shaders::VisibilityConstants camera, std::uint32_t size)
        {
            camera.mFogColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
            camera.mFogExtinction = 3.0e-6f;

            const Frame frame = shoot(makeWall(), {}, camera, size);

            std::vector<float> luminance(std::size_t{ size } * size);
            for (std::size_t i = 0; i < luminance.size(); ++i)
                luminance[i] = frame.at(i * 4);
            return luminance;
        }

        /// The picture a display would show: this pass's own tone curve and display curve over the exposure the
        /// frame measured for itself.
        ///
        /// The one thing here that wants the picture, because it measures the exposure pass. Settled: the
        /// scene's first frame has no past and holds a day's exposure (`EXPOSURE_DAY`), so the same frame is
        /// drawn again over a time the eye closes the whole gap in, and that is the picture read.
        void renderPicture(const SceneDesc& scene, std::span<const TextureData> textures,
            const Shaders::VisibilityConstants& camera, std::uint32_t size, std::vector<std::uint8_t>& pixels,
            Shot shot = {})
        {
            shot.mExposure = std::nullopt;
            shoot(scene, textures, camera, size, shot);
            shot.mSetScene = false;
            shot.mWorldSinceLast = 1000.0f;
            shoot(scene, textures, camera, size, shot);
            mRenderer.readPixels(pixels);

            requireFrame(pixels, size);
        }

        /// The frame the caller drew itself through `mRenderer`: for a test that extends or places
        /// the standing world and draws it again, which `shoot` cannot, since it sets the scene.
        Frame readFrame(std::uint32_t size)
        {
            Frame frame;
            readFrame(size, frame);
            return frame;
        }

        /// A wall square to the sun with one pane held in front of it, as the byte its centre pixel comes back
        /// as.
        ///
        /// Shared by the three tests that hold a pane up to different rays (a shadow ray, the eye, a faded
        /// placement); their evidence is that the answer lands where a sun of half the irradiance lands, which
        /// holds only while none can drift from the others' scene.
        ///
        /// @param pane where to put the quad: on the sun's path to the wall for a shadow, on the eye's path
        ///        for a peel.
        /// @param colour what the pane is made of, its own alpha included, or nothing for the wall alone.
        /// @param fade how much of the pane the game is showing, the other number a surface's opacity is made of.
        /// @param where the caller's own line, never passed: one test holds five panes up, and a failure would
        ///        otherwise report at this helper's line.
        std::uint8_t litThroughPane(std::span<const osg::Vec3f, 4> pane, std::optional<osg::Vec4f> colour,
            const osg::Vec3f& irradiance, float fade = 1.0f,
            std::source_location where = std::source_location::current())
        {
            const ::testing::ScopedTrace trace(where.file_name(), static_cast<int>(where.line()), "litThroughPane");

            constexpr std::uint32_t size = 33;

            SceneDesc scene = makeWall();
            if (colour.has_value())
                addPane(scene, pane, *colour, fade);

            const Shaders::VisibilityConstants camera = wallCamera(size, irradiance);

            const Frame frame = shoot(scene, {}, camera, size);
            EXPECT_GT(frame.mHits, 0u);

            return frame.byte(centreValueOf(size));
        }

        /// A wall square to the sun with a stack of panes strung along the eye's own ray, as the byte its
        /// centre pixel comes back as. A stack of one is `litThroughPane`'s own answer, so its figures carry
        /// over.
        ///
        /// The eye stands at x = 100 and looks at the origin, so its ray runs at x = -y: each pane is centred
        /// there, ten units to a side, clear of the sun's path to the middle of the wall (along +Y at x = 0),
        /// and ten units apart, nearest the eye first.
        ///
        /// @param layers what each pane is made of, its own alpha included, from the eye inwards.
        /// @param where the caller's own line, never passed, for the reason `litThroughPane` gives.
        std::uint8_t litThroughStack(std::span<const osg::Vec4f> layers, const osg::Vec3f& irradiance,
            std::source_location where = std::source_location::current())
        {
            const ::testing::ScopedTrace trace(where.file_name(), static_cast<int>(where.line()), "litThroughStack");

            constexpr std::uint32_t size = 33;

            SceneDesc scene = makeWall();
            for (std::size_t at = 0; at < layers.size(); ++at)
            {
                const float away = -60.0f + 10.0f * static_cast<float>(at);

                std::array<osg::Vec3f, 4> pane = uprightQuadAt(10.0f, away);
                for (osg::Vec3f& corner : pane)
                    corner.x() -= away;

                addPane(scene, pane, layers[at]);
            }

            const Shaders::VisibilityConstants camera = wallCamera(size, irradiance);

            const Frame frame = shoot(scene, {}, camera, size);
            EXPECT_GT(frame.mHits, 0u);

            return frame.byte(centreValueOf(size));
        }

        /// A wall square to the sun with a pane held twenty units across at y = -50, as the three bytes its
        /// centre pixel comes back as.
        ///
        /// What the water test and the first-person test share, each placing the pane its own way through
        /// `place`. The camera stands off the sun's axis, so the centre pixel lands on the patch of wall the
        /// pane shadows without the camera's own ray crossing the pane (it passes y = -50 at x = 50, the pane
        /// reaches 20), or at the pane, from ten units above its middle: water is a horizontal plane to the
        /// renderer, which names its sides by which way a ray climbs, and a level ray climbs neither way.
        /// @param where the caller's own line, never passed, for the reason `litThroughPane` gives.
        std::array<std::uint8_t, 3> paneOverWall(
            const std::function<void(SceneDesc&, std::span<const osg::Vec3f, 4>)>& place, bool lookAtIt,
            std::source_location where = std::source_location::current())
        {
            const ::testing::ScopedTrace trace(where.file_name(), static_cast<int>(where.line()), "paneOverWall");

            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = centreValueOf(size);

            const std::array pane = uprightQuadAt(20.0f, -50.0f);

            SceneDesc scene = makeWall();
            place(scene, pane);

            const osg::Vec3f bright(2.0f, 2.0f, 2.0f);
            const Shaders::VisibilityConstants camera = lookAtIt
                ? wallCamera(size, bright, osg::Vec3f(0.0f, -100.0f, 10.0f), osg::Vec3f(0.0f, -50.0f, 0.0f))
                : wallCamera(size, bright);

            const Frame frame = shoot(scene, {}, camera, size);
            EXPECT_GT(frame.mHits, 0u);
            return { frame.byte(centre), frame.byte(centre + 1), frame.byte(centre + 2) };
        }

        /// What a probe read back, against the frame it asked for.
        ///
        /// A throw and not an assertion: every caller indexes the buffer by a number worked out from `size`,
        /// and `<cassert>` is compiled out of the build a figure is taken in, while an `ASSERT_EQ` would
        /// return and leave the caller indexing past the end. The message names both sizes.
        template <class T>
        static void requireFrame(const std::vector<T>& read, std::uint32_t size)
        {
            const std::size_t wanted = std::size_t{ size } * size * 4;
            if (read.size() != wanted)
                throw std::runtime_error("a probe read back " + std::to_string(read.size()) + " values where a "
                    + std::to_string(size) + " by " + std::to_string(size) + " frame is " + std::to_string(wanted));
        }

    private:
        /// The frame's light as the curve maps it, the puffs over it, where nothing upscales; and the
        /// trace's own answer where something does, which a test of the upscaler holds the
        /// reconstruction to.
        void readFrame(std::uint32_t size, Frame& frame)
        {
            if (upscales(mRenderer.getProfile().mUpscale))
                mProbe.readComposite(frame.mRadiance);
            else
                mProbe.readShownRadiance(frame.mRadiance);
            requireFrame(frame.mRadiance, size);
        }

        /// The fixture's textures, numbered the way its scene added them.
        ///
        /// A convention of these tests and not of the renderer: descriptions are built in the order the scene
        /// calls `addTexture`, so position is slot. The slot is written here because a scene that has given a
        /// slot back has a hole in its table and none in its descriptions. The span reaches into `mNumbered`,
        /// which the next render overwrites; safe because `setWorld` is the one caller and hands it straight to
        /// `setScene`. `textures` at the slots `scene` numbers them by, each under the encoding the slot was
        /// taken as, as `SceneTextures` makes of the table.
        std::span<const TextureData> inSceneOrder(const SceneDesc& scene, std::span<const TextureData> textures)
        {
            const std::span<const TextureRow> rows = scene.textures().getRows();
            mNumbered.assign(textures.begin(), textures.end());
            for (std::size_t at = 0; at < mNumbered.size(); ++at)
            {
                mNumbered[at].mSlot = static_cast<std::uint32_t>(at);
                if (at < rows.size() && scene.textures().isLive(static_cast<Index>(at)))
                    mNumbered[at].mEncoding = rows[at].mEncoding;
            }

            return mNumbered;
        }

        std::vector<TextureData> mNumbered;
    };

    /// **A test whose claim is a statistic over many frames**: a history settled, a trail behind a moving
    /// occluder, a mean that holds still from frame to frame, over dozens to hundreds of frames.
    ///
    /// **Unvalidated** (`getUnvalidatedRenderer`), as Dawn's end-to-end tests run at a validation level below full
    /// for the layers' cost, **where the layers held such a test past a second**: they check how a frame is recorded,
    /// which is the same at its fortieth frame as at its second, and cost a small frame two to three times its own.
    /// The layers' checks of these frames are left to the suite's validated tests and to the gate's `check` and
    /// `repeat`, which draw the real places with the layers loaded: a configuration only a settling test draws is
    /// checked by neither.
    class RtxSettlingTest : public RtxVisibilityTest
    {
    protected:
        RtxSettlingTest()
            : RtxVisibilityTest(Testing::Validation::Unvalidated)
        {
        }
    };
}
