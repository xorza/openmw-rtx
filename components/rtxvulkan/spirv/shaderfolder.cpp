#include "shaderfolder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>

#include <components/platform/folder.hpp>
#include <components/rtx/common/error.hpp>

#include "spirvfile.hpp"

namespace Rtx
{
    ShaderFolder::ShaderFolder(const std::filesystem::path& folder, const bool census)
    {
        const std::optional<std::vector<std::filesystem::directory_entry>> listed = Platform::listFolder(folder);
        if (!listed.has_value())
            throw InputError("cannot list the shaders in " + spelledPath(folder));

        std::vector<std::filesystem::path> files;
        for (const std::filesystem::directory_entry& entry : *listed)
            if (std::error_code typed; entry.is_regular_file(typed) && entry.path().extension() == ".spv")
                files.push_back(entry.path());
        // By the names `ShaderSet::find` compares, which a path's own order need not be on every system.
        std::ranges::sort(files, {}, [](const std::filesystem::path& file) { return spelledPath(file.filename()); });

        std::vector<std::size_t> starts;
        starts.reserve(files.size() + 1);
        mNames.reserve(files.size());
        for (const std::filesystem::path& file : files)
        {
            const std::vector<std::uint32_t> words = readSpirv(file);
            mNames.push_back(spelledPath(file.filename()));
            starts.push_back(mWords.size());
            mWords.insert(mWords.end(), words.begin(), words.end());
        }
        starts.push_back(mWords.size());

        // The spans last, once the words stop moving.
        mModules.reserve(files.size());
        for (std::size_t at = 0; at < files.size(); ++at)
            mModules.push_back(ShaderModule{
                .mName = mNames[at], .mWords = std::span(mWords).subspan(starts[at], starts[at + 1] - starts[at]) });

        mSet = ShaderSet{ .mModules = mModules, .mDigest = digestShaders(mModules), .mCensus = census };
    }
}
