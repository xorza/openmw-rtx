#pragma once

#include <vector>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>

#include "fogvolume.hpp"
#include "ripplepass.hpp"
#include "wavepass.hpp"

namespace Rtx
{
    class Device;
    class DeviceScene;
    class GpuTimer;
    class SceneDesc;
    struct TraceSubject;

    /// What every trace reads beside its scene and its chain: the sea, the wake in it, the fog's
    /// field and the list of no sprites. One of each for everything traced, the doll and the map
    /// included, because none of them is a property of a scene.
    class TraceMedia
    {
    public:
        explicit TraceMedia(const Device& device);

        /// What a trace of `held` under `camera` reads, for the copy its last placement wrote, and
        /// what its launches bind beside it — but for the chain's own images, which
        /// `TraceChain::record` names. One description for a frame and for a picture inside the
        /// interface, which differ in the arguments alone.
        ///
        /// @param counts the census the launches sum into.
        /// @param sunGlare the counts the eye's launch adds to: the fader's for a frame,
        ///        `DisplayChain::getGlareCounts`.
        /// @param traceSlot which of the chain's slots the trace takes.
        TraceSubject describe(const DeviceScene& held, const Shaders::VisibilityConstants& camera, const Buffer& counts,
            const Buffer& sunGlare, FrameSlot traceSlot) const;

        const WavePass& getWaves() const { return mWaves; }
        const RipplePass& getRipples() const { return mRipples; }
        const FogTile& getFog() const { return mFog; }

        /// Where the list of no sprites is, naming it for the next submit: what a camera that draws
        /// none reads in place of its bin's list.
        VkDeviceAddress describeNoSprites() const { return mNoSprites.addressFor(); }

        /// What the sea's amplitudes were last drawn for.
        const SeaState& getSea() const { return mWaves.getSea(); }

        /// Draws another sea's amplitudes. Nothing may be in flight: `WavePass::describe` says why.
        void describeSea(const SeaState& sea) { mWaves.describe(sea); }

        /// Copies what the world's scene says disturbed the water, at the two places the world's
        /// scene passes through: built and placed. A copy and not a span, because the scene's list
        /// is cleared by the next walk and nothing here would say so.
        void keepRipples(const SceneDesc& scene);

        /// Drops the wake, for a world that was replaced rather than moved through.
        void resetRipples() { mRipples.reset(); }

        /// Steps the wake under the world's frame, anchored at `eye`, and presses in what
        /// `keepRipples` kept. Only for a trace with a sea: a frame with none leaves the tiles as
        /// they were.
        ///
        /// @param timer null where the run is not being timed.
        void stepRipples(
            VkCommandBuffer commands, FrameSlot slot, const osg::Vec2f& eye, double waterSeconds, GpuTimer* timer);

        /// Tells `sampled` where the wake's field lies, as the last step left it.
        void placeRipples(Shaders::VisibilityConstants& sampled) const;

    private:
        WavePass mWaves;
        RipplePass mRipples;

        /// Refilled and never freed.
        std::vector<RippleImpulse> mImpulses;

        /// Drawn once for the life of the device. Nothing about it turns on the weather or the cell
        /// — those decide the extinction and the layer's height, which are numbers the shader
        /// already has.
        FogTile mFog;

        /// An empty sprite tiles' list, for a camera that draws no sprites and so binned none. An
        /// empty list is two words, so one buffer serves every extent.
        Buffer mNoSprites;
    };
}
