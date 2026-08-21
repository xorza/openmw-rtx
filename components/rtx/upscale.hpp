#pragma once

namespace Rtx
{
    /// How the frame gets from the size it is traced at to the size it is shown at.
    ///
    /// **A quality level rather than a ratio**, because the ratio is the upscaler's to choose: what
    /// to render at for a given output is asked of it, and the answer has changed between versions
    /// of the network.
    ///
    /// Each backend reads this as whatever its platform offers — Ray Reconstruction on Vulkan. A
    /// build without one refuses anything but `Off` rather than quietly ignoring it.
    enum class Upscale
    {
        /// Trace and present at the same size, with no upscaler in the frame at all. What every test
        /// and every reference render uses, because a converged average is of the trace and not of
        /// a network's opinion of it.
        Off,

        /// Half the output's width and height, so a quarter of its pixels. What the frame budget in
        /// `docs/rtx/plan.md` §5.3 is written against — 1920×1080 internal to 3840×2160.
        Performance,
        Balanced,
        Quality,

        /// **No upscaling, but still the upscaler**: render and output are the same size and it only
        /// denoises and antialiases. What separates the two halves of what it does, when a frame
        /// comes out softer than the reference and the question is which half softened it.
        Dlaa,
    };
}
