#include "harnessfolder.hpp"

namespace RtxTool
{
    std::filesystem::path harnessDirectory()
    {
        return std::filesystem::path(OPENMW_RTX_HARNESS_DIR);
    }

    Rtx::ShaderSet shadersFor(const std::filesystem::path& resources, const bool source, const bool census)
    {
        if (!source && !census)
            return Rtx::ShaderSet{ .mDirectory = Rtx::shaderDirectory(resources) };

        const char* const name = census ? (source ? "shaders-census-source" : "shaders-census") : "shaders-source";
        return Rtx::ShaderSet{ .mDirectory = harnessDirectory() / name, .mCensus = census };
    }
}
