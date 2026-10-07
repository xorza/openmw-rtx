#include "notfinitecensus.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <system_error>

#include <components/crashcatcher/crash.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>

namespace Rtx
{
    static_assert(
        Shaders::CENSUS_KERNELS <= sMaxCensusKernels, "a census telling apart more modules than a report holds");

    namespace
    {
        constexpr std::string_view sSpirv = ".spv";

        std::string_view withoutSpirv(const std::string_view module)
        {
            return module.ends_with(sSpirv) ? module.substr(0, module.size() - sSpirv.size()) : module;
        }
    }

    NotFiniteCensus::NotFiniteCensus(const Device& device, const std::filesystem::path& shaderDirectory)
        : mWords(Buffer::readBack(device, sBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            "not-finite census"))
    {
        // Nought before the first frame clears it: a module may count while the renderer loads.
        std::memset(mWords.map(), 0, sBytes);

        // A directory that cannot be read names no module, and the first pipeline is refused by
        // name in `kernelOf`, as `ShaderCode` refuses it where nothing counts.
        std::error_code failed;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(shaderDirectory, failed))
        {
            const std::string file = Files::pathToUnicodeString(entry.path().filename());
            if (!std::string_view(file).ends_with(sSpirv))
                continue;

            Crash::contract(mNamed < Shaders::CENSUS_KERNELS, "more shader modules than the census tells apart");
            mNames[mNamed++] = std::string(withoutSpirv(file));
        }

        std::sort(mNames.begin(), mNames.begin() + mNamed);
    }

    std::uint32_t NotFiniteCensus::kernelOf(const std::string_view module) const
    {
        const std::string_view name = withoutSpirv(module);
        const auto end = mNames.begin() + mNamed;
        const auto found = std::lower_bound(mNames.begin(), end, name);
        if (found == end || *found != name)
            throw InputError(std::format("no shader module {} in the shader directory", module));

        return static_cast<std::uint32_t>(found - mNames.begin());
    }

    void NotFiniteCensus::record(const VkCommandBuffer commands, const Buffer& into) const
    {
        mWords.transition(commands, Use::sBufferAnyReadWrite, Use::sBufferCopyRead);
        mWords.copyTo(commands, into, sBytes);
        mWords.transition(commands, Use::sBufferCopyRead, Use::sBufferClearWrite);
        mWords.clear(commands);
        mWords.transition(commands, Use::sBufferClearWrite, Use::sBufferAnyReadWrite);
    }

    void NotFiniteCensus::readInto(const Buffer& copied, NotFinite& into) const
    {
        const auto* const stores = static_cast<const Shaders::Census*>(copied.map());
        for (std::uint32_t kernel = 0; kernel < mNamed; ++kernel)
            into.add(mNames[kernel], stores->mNotFinite[kernel]);
    }
}
