#pragma once

#include <string>

namespace Rtx
{
    class SceneDesc;
}

namespace MWRender
{
    struct WalkReport;
}

namespace RtxTool
{
    /// What the renderer was handed, as `scene` reports it: what was placed and what was not, what was refused,
    /// what arriving has cost since the renderer started, and what a second walk added where one was made.
    std::string describeScene(const Rtx::SceneDesc& scene, const MWRender::WalkReport& walked);
}
