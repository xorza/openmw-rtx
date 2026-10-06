#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec2f>
#include <osg/Vec2i>
#include <vulkan/vulkan_core.h>

#include <components/rtx/scene/ripple.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/ripple.h>

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// What walked through the water, as a height field around the eye the sea reads beside its
    /// cascades: stepped by the wave equation, pressed where the game says something disturbed
    /// it, and unpacked into a slope tile and a curvature tile with a chain each, in the wave
    /// tiles' own layout. `shaders/ripple.h` says whose field this is.
    ///
    /// **State across frames, and deterministic.** The field is what the steps and the impulses
    /// made of it, and both are the run's own — the step by the water's clock, the impulses by the
    /// simulation — so two runs of one build stand the same field on every frame.
    ///
    /// **The water's clock, `Shaders::VisibilityConstants::mWaterTime`, and not the sky's**: the
    /// simulation's seconds, which upstream's `RipplesSurface::updateState` steps by and the waves
    /// run on. The sky's clock stands still wherever no sky is shown, which is every interior with
    /// water in it, and races under a sped-up `timescale`.
    class RipplePass
    {
    public:
        /// Makes the field still, and leaves every tile in the layout the trace samples it in.
        /// Submits and waits.
        explicit RipplePass(const Device& device);

        /// Steps the field by the time `waterSeconds` moved since the last step, in as few steps as
        /// keep each within `getLongestStep` and no more than `RIPPLE_SUBSTEPS_MOST`, presses
        /// `impulses` with the first, and unpacks the tiles. The window follows `eye` by whole
        /// texels. Nothing at all where the clock did not move, which leaves the tiles as they were.
        ///
        /// **Every frame and not once a sixtieth**, as upstream steps, which is a look of its own:
        /// the same springs over the same time, and a wake that keeps its pace under sixty frames a
        /// second, where upstream's slows. AGENTS.md's Accepted diff names it.
        ///
        /// @param slot the frame's, which names the copy of the impulse buffer this frame writes.
        /// @param timer where the step's zone goes, or nothing where nobody is counting. Opened
        ///        only on a frame that steps, so a frame that stands still reports no zone.
        void record(VkCommandBuffer commands, FrameSlot slot, std::span<const RippleImpulse> impulses,
            const osg::Vec2f& eye, double waterSeconds, GpuTimer* timer);

        /// The longest step the springs stand, in sixtieths.
        ///
        /// **The five-point Laplacian's highest mode**, the checkerboard, is pulled by `8 a + udamp`
        /// a sixtieth squared; a step of `s` keeps it bounded while `s² (8 a + udamp) ≤ 2 (1 + c)`,
        /// `c` the carry `(1 − vdamp)^s`. Under two sixtieths the carry is at least `(1 − vdamp)²`,
        /// which this takes, so the bound holds over the step it names: 1.298 sixtieths, a frame
        /// of 46 a second.
        static float getLongestStep()
        {
            const float carried = (1.0f - Shaders::RIPPLE_VELOCITY_DAMPING) * (1.0f - Shaders::RIPPLE_VELOCITY_DAMPING);
            return std::sqrt(
                2.0f * (1.0f + carried) / (8.0f * Shaders::RIPPLE_STIFFNESS + Shaders::RIPPLE_HEIGHT_DAMPING));
        }

        /// Drops what the field holds, for a world that was replaced rather than moved through.
        void reset() { mReset = true; }

        /// Linear, mipmapped and clamped to a border of nothing: past the field's edge is still
        /// water.
        VkSampler getSampler() const { return mSampler.get(); }

        const Image& getSurface() const { return mSurface; }
        const Image& getCurvature() const { return mCurvature; }

        /// Where the field's window begins, in world units, and how wide it is.
        const osg::Vec2f& getOrigin() const { return mOrigin; }
        static float getExtent() { return Shaders::RIPPLE_TEXEL * static_cast<float>(Shaders::RIPPLE_GRID); }

    private:
        /// Which texel the window would begin at for an eye at `eye`: the eye's own texel less
        /// half the grid, so the eye stands in the middle.
        static osg::Vec2i windowOf(const osg::Vec2f& eye);

        /// Moves the window to `window`, in texels and in world units together.
        void standWindow(const osg::Vec2i& window);

        const Device& mDevice;

        ComputePipeline<Shaders::RippleStepConstants> mStepPipeline;
        ComputePipeline<NoConstants> mComposePipeline;

        Sampler mSampler;

        /// The two heights, this step's and the one before, ping-ponged between two images so a
        /// step reads one whole and writes the other.
        std::array<Image, 2> mFields;
        std::size_t mLatest = 0;

        Image mSurface;
        Image mCurvature;

        /// The frame's impulses, one copy a frame slot so a write never lands under a submit, and
        /// the ones waiting for a step to be due.
        PerSlot<Buffer> mImpulses;
        std::vector<Shaders::GpuRippleImpulse> mImpulseScratch;
        std::vector<RippleImpulse> mPending;

        /// Where the window begins, in texels and in world units.
        osg::Vec2i mWindow;
        osg::Vec2f mOrigin;

        /// The water's clock at the last step, and how long that step was in sixtieths, or nought
        /// for a field that has taken none since it was reset.
        double mSteppedSeconds = 0.0;
        float mLastStep = 0.0f;
        bool mReset = true;
    };
}
