#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>

namespace Rtx
{
    /// **What an upscaler's runtime was measured to hold for its feature, by the extents the feature was made at**:
    /// memory the driver allocates and the allocator does not, known only once a feature stands. One output's
    /// measurements at a time, a mode each at most.
    class FeatureBytes
    {
    public:
        /// Keeps `bytes` for these extents, in place of what was measured at them before and of everything measured
        /// at another output: a window resized measures its modes again, and what was measured at the old output
        /// names extents no mode traces at now.
        void remember(const VkExtent2D render, const VkExtent2D output, const VkDeviceSize bytes)
        {
            std::erase_if(mMeasured,
                [&](const Measured& kept) { return !same(kept.mOutput, output) || same(kept.mRender, render); });
            mMeasured.push_back(Measured{ .mRender = render, .mOutput = output, .mBytes = bytes });
            assert(mMeasured.size() <= sUpscaleMenu.size() && "more features measured at one output than modes");
        }

        /// Whether a feature at these extents was measured.
        bool holds(const VkExtent2D render, const VkExtent2D output) const { return find(render, output) != nullptr; }

        /// What a feature at these extents holds: as measured, and where these were not, the most any feature
        /// measured held, nought before the first. **The most known, for the room made ahead of a feature**: the
        /// frame's reserve counts the feature before it stands, so that content gives way first and not the driver.
        VkDeviceSize at(const VkExtent2D render, const VkExtent2D output) const
        {
            if (const Measured* const measured = find(render, output))
                return measured->mBytes;

            VkDeviceSize most = 0;
            for (const Measured& measured : mMeasured)
                most = std::max(most, measured.mBytes);
            return most;
        }

    private:
        struct Measured
        {
            VkExtent2D mRender{};
            VkExtent2D mOutput{};
            VkDeviceSize mBytes = 0;
        };

        static bool same(const VkExtent2D a, const VkExtent2D b) { return a.width == b.width && a.height == b.height; }

        const Measured* find(const VkExtent2D render, const VkExtent2D output) const
        {
            const auto found = std::ranges::find_if(mMeasured,
                [&](const Measured& kept) { return same(kept.mRender, render) && same(kept.mOutput, output); });
            return found != mMeasured.end() ? &*found : nullptr;
        }

        std::vector<Measured> mMeasured;
    };
}
