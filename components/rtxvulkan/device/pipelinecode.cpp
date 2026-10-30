#include "pipelinecode.hpp"

#include <format>
#include <fstream>
#include <utility>

#include <components/debug/debuglog.hpp>

namespace Rtx
{
    std::filesystem::path pipelineCodeFile(const std::filesystem::path& folder, const std::string_view pipeline,
        const std::uint32_t copy, const std::uint32_t executable, const std::uint32_t executables)
    {
        std::string name(pipeline);
        for (char& c : name)
        {
            const bool kept = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
                || c == '_' || c == '-';
            if (!kept)
                c = '-';
        }

        if (copy > 0)
            name += std::format("@{}", copy + 1);
        if (executables > 1)
            name += std::format(".{}", executable);
        return folder / (name + ".txt");
    }

    std::string pipelineCodeText(const std::span<const PipelineCodeForm> forms)
    {
        std::string text;
        for (const PipelineCodeForm& form : forms)
        {
            if (!text.empty())
                text += '\n';
            text += std::format("## {} — {}\n\n", form.mName, form.mDescription);
            text += form.mText;
            if (!form.mText.empty() && form.mText.back() != '\n')
                text += '\n';
        }
        return text;
    }

    PipelineCodeFolder::PipelineCodeFolder(std::filesystem::path folder)
        : mFolder(std::move(folder))
    {
        std::filesystem::create_directories(mFolder);
    }

    std::uint32_t PipelineCodeFolder::claim(const std::string_view pipeline)
    {
        const std::lock_guard locked(mLock);
        const auto found = mClaimed.find(pipeline);
        if (found == mClaimed.end())
        {
            mClaimed.emplace(pipeline, 1u);
            return 0;
        }
        return found->second++;
    }

    void PipelineCodeFolder::write(const std::string_view pipeline, const std::uint32_t copy,
        const std::uint32_t executable, const std::uint32_t executables,
        const std::span<const PipelineCodeForm> forms) const
    {
        const std::filesystem::path path = pipelineCodeFile(mFolder, pipeline, copy, executable, executables);
        std::ofstream file(path, std::ios::trunc);
        file << pipelineCodeText(forms);
        file.close();
        if (file.fail())
            Log(Debug::Warning) << "pipeline " << pipeline << ": its code could not be written to " << path;
    }
}
