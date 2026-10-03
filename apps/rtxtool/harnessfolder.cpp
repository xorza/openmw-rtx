#include "harnessfolder.hpp"

namespace RtxTool
{
    std::filesystem::path harnessDirectory()
    {
        return std::filesystem::path(OPENMW_RTX_HARNESS_DIR);
    }

    std::filesystem::path shaderSourceDirectory()
    {
        return harnessDirectory() / "shaders-source";
    }
}
