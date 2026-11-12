#include "harnessfolder.hpp"

#include <components/files/conversion.hpp>
#include <components/rtxvulkan/createrenderer.hpp>

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

    HarnessShaders::HarnessShaders(const Verbs verb, const bool source)
    {
        const bool census = !policyOf(verb).mMeasures;
        const std::filesystem::path folder(census
                ? (source ? OPENMW_RTX_SPIRV_CENSUS_SOURCE_DIR : OPENMW_RTX_SPIRV_CENSUS_DIR)
                : (source ? OPENMW_RTX_SPIRV_SOURCE_DIR : OPENMW_RTX_SPIRV_DIR));
        mName = Files::pathToUnicodeString(folder.filename());
        // The game's own where it is the game's set, so a measured run times the very modules it ships.
        if (source || census)
            mFolder.emplace(folder, census);
    }

    const Rtx::ShaderSet& HarnessShaders::getSet() const
    {
        return mFolder.has_value() ? mFolder->getSet() : Rtx::shippedVulkanShaders();
    }
}
