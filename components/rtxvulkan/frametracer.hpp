#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>

namespace Rtx
{
    class Device;
    struct TracePasses;

    /// The world's frame on the device: its chain and the arena the chain and the upscaler use
    /// inside one stretch of a frame, laid over each other. What `PictureTracer` is for a picture,
    /// for the frame, so the two are made, measured and let go of alike. The upscaler is the
    /// renderer's, handed in at each call that places or measures it: it is made before the trace's
    /// kernels, which are specialized on the masks it reads.
    class FrameTracer
    {
    public:
        /// @param passes what the chain traces with, which outlives it.
        FrameTracer(const Device& device, const TracePasses& passes, RadianceWidth radiance);

        /// What the frame uses inside one stretch of it at `render` and `output` under `profile`, which
        /// its arena places: the chain's shared images, then the upscaler's where the profile upscales.
        static void declare(VkExtent2D render, VkExtent2D output, const RenderProfile& profile,
            const Upscaler& upscaler, std::vector<ArenaImage>& into);

        /// What the chain, the arena of `declared` and the upscaler where `upscale` upscales take of
        /// the device at `render` and `output`.
        static VkDeviceSize bytesAt(const Device& device, VkExtent2D render, VkExtent2D output, Upscale upscale,
            const Upscaler& upscaler, RadianceWidth radiance, std::span<const ArenaImage> declared);

        /// **The one answer to what a move to `render` and `output` under `upscale` moved**, with the
        /// arena `declared`, which every part that keeps something across it is handed: the extents
        /// against the last `resize`'s, and what is made again — the chain where it stands at another
        /// extent or filter, the upscaler where it stands at other extents or a mode turned it off,
        /// and the arena where its declarations moved. A part made again is let go first (`release`),
        /// so a part asked to stand where it stands only takes its place in the new arena.
        struct Moves
        {
            /// The traced extent, and the shown one, where they differ from the last `resize`'s: no
            /// history reprojects across either.
            bool mRenderExtent = false;
            bool mOutputExtent = false;

            bool mTrace = false;
            bool mUpscaler = false;
            bool mArena = false;
        };
        Moves movesTo(VkExtent2D render, VkExtent2D output, Upscale upscale, const std::vector<ArenaImage>& declared,
            const Upscaler& upscaler) const;

        /// Whether letting go of what `moves` makes again lets go of anything that stands, which the
        /// device is drained of first.
        bool releases(const Moves& moves) const
        {
            return moves.mTrace || moves.mUpscaler || (moves.mArena && !mArena.isEmpty());
        }

        /// Lets go of what `moves` makes again, the device drained of what read it.
        void release(const Moves& moves, Upscaler& upscaler);

        /// Makes the arena of `declared` and places the chain and, where `upscale` upscales, the
        /// upscaler in it, where `moves` says the declarations moved: the chain writes its sets anew,
        /// and a set a frame in flight reads is not written. After `release(moves)`.
        void resize(const Moves& moves, VkExtent2D render, VkExtent2D output, Upscale upscale,
            std::vector<ArenaImage>&& declared, Upscaler& upscaler);

        TraceChain& getChain() { return mChain; }
        const TraceChain& getChain() const { return mChain; }

    private:
        const Device& mDevice;

        /// Before the two that place their images in it, which go first.
        ImageArena mArena;
        std::vector<ArenaImage> mDeclared;

        /// What the last `resize` was to, which `movesTo` measures against: none before the first.
        VkExtent2D mRender{};
        VkExtent2D mOutput{};

        TraceChain mChain;
    };
}
