#include "harnessfolder.hpp"

namespace RtxTool
{
    std::filesystem::path harnessDirectory()
    {
        return std::filesystem::path(OPENMW_RTX_HARNESS_DIR);
    }

    std::filesystem::path viewsFile()
    {
        return std::filesystem::path(OPENMW_RTX_VIEWS_FILE);
    }

    std::filesystem::path suitesFile()
    {
        return std::filesystem::path(OPENMW_RTX_SUITES_FILE);
    }

    std::filesystem::path keysDirectory()
    {
        return std::filesystem::path(OPENMW_RTX_KEYS_DIR);
    }

    Rtx::ShaderSet shadersFor(const std::filesystem::path& resources, const Verbs verb, const bool source)
    {
        const bool census = !policyOf(verb).mMeasures;
        if (!source && !census)
            return Rtx::ShaderSet{ .mDirectory = Rtx::shaderDirectory(resources) };

        const char* const directory = census
            ? (source ? OPENMW_RTX_SPIRV_CENSUS_SOURCE_DIR : OPENMW_RTX_SPIRV_CENSUS_DIR)
            : OPENMW_RTX_SPIRV_SOURCE_DIR;
        return Rtx::ShaderSet{ .mDirectory = std::filesystem::path(directory), .mCensus = census };
    }
}
