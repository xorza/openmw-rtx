#pragma once

#include <components/rtx/shaders/visibility.h>

#include "reconstruction.hpp"

namespace Rtx
{
    struct FrameOptions;
    struct InstanceCounts;

    /// A frame as its trace samples it: what the caller stated, and every field that follows from
    /// the statement and the renderer's own state — the jitter, the noise and the level bias the
    /// reconstruction decides, how the surfaces are shown, what the scene holds, and where the eye
    /// was. The one place those are written, for a frame and for a picture inside the interface
    /// alike. **The one writer of them**: a host states a viewpoint and a world
    /// (`Renderer::renderFrame`), which leaves them at nought, and what a frame may ask of them it
    /// asks through `options`.
    ///
    /// @param options what the frame asked for over `profile`; a picture's asks nothing.
    /// @param counts what the traced scene holds.
    /// @param previous what `previousOf` kept of the last frame, to reproject against, or null
    ///        where there is no frame before it: a picture, and a frame after a resize, a new scene
    ///        or a reset.
    Shaders::VisibilityConstants sampleFrame(const Shaders::VisibilityConstants& stated, const FrameOptions& options,
        const RenderProfile& profile, const Reconstruction& reconstruction, const InstanceCounts& counts,
        const Shaders::VisibilityConstants* previous);

    /// What the next frame reprojects against, of a frame `stated` and traced as `sampled`: the
    /// eyes where the caller put them, and the jitter `sampled` drew, which is where inside a pixel
    /// this frame's rays went and not where an eye stood. The next frame's histories rebuild this
    /// frame's points along those rays (`heldSurfaceMatches`), and the upscaler takes the jitter as
    /// the previous dispatch's, as FSR's own context keeps it (`previousFrameJitterOffset`).
    Shaders::VisibilityConstants previousOf(
        const Shaders::VisibilityConstants& stated, const Shaders::VisibilityConstants& sampled);
}
