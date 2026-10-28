#pragma once

#include <cstdint>
#include <optional>

#include <components/rtx/frame/framepast.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>

#include "renderer.hpp"

namespace Rtx
{
    /// What a frame is described with before a backend traces it, and what it carries from one
    /// frame to the next: the air's drift downwind, the histories' losses since the last trace, and
    /// the camera the next frame reprojects against. A picture inside the interface is described
    /// here too, from its viewpoint and its options alone. **API-neutral**, so a backend records
    /// what this composed and composes nothing of its own.
    class FrameDescriber
    {
    public:
        /// The block `request` asks to be traced with, before its sampling: the viewpoint's half
        /// (`constantsFor`), what the eye draws, the sampler's frame, and the world laid over it
        /// (`describeWorld`), which steps the drift by the reading's clock. What rides beside the
        /// block goes into `described`.
        FrameTraced describe(const FrameRequest& request, WorldOptions& described);

        /// The block a picture inside the interface is traced with, off `view` and the light,
        /// classes and backdrop `options` name: its sun and ambient its own, no sky shadows, and a
        /// backdrop that stops where nothing was hit wherever the clear is less than opaque.
        static Shaders::VisibilityConstants describePicture(const Viewpoint& view, const GuiTraceOptions& options);

        /// Stands the air at `air`, so the next frame describes it carried from there.
        void holdAir(const AirClock& air) { mDrift.hold(air.mCarried, air.mSky.mSeconds); }

        /// Notes what the histories lost since the last trace: a resize, a world built again.
        void lose(const FramePast& lost) { mPast |= lost; }

        /// What the next frame's past is worth: every loss noted since the last trace, with what the
        /// frame's own `loss` adds. Spent: the frame after begins with none.
        FramePast takePast(HistoryLoss loss);

        /// The camera to reproject against, or null where `past` lost the reprojection.
        const Shaders::VisibilityConstants* reprojectingFrom(const FramePast& past) const;

        /// Keeps what the next frame reprojects against, of a frame `stated` and traced as `sampled`
        /// (`previousOf`).
        void traced(const Shaders::VisibilityConstants& stated, const Shaders::VisibilityConstants& sampled);

        /// The sampler's frame the last trace was taken at, which says how its fields lie, or
        /// nothing before the first.
        std::optional<std::uint32_t> getTracedFrame() const;

    private:
        FogDrift mDrift;

        /// Everything before the first frame: it has no past.
        FramePast mPast = FramePast::everything();
        std::optional<Shaders::VisibilityConstants> mPrevious;
    };
}
