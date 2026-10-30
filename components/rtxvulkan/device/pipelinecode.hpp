#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace Rtx
{
    /// One of the forms a driver gives an executable of a pipeline in, as text: its name and what it
    /// says it is, as `VkPipelineExecutableInternalRepresentationKHR` holds them — RADV's NIR, its
    /// compiler's own IR and the instructions the card runs.
    struct PipelineCodeForm
    {
        std::string_view mName;
        std::string_view mDescription;
        std::string_view mText;
    };

    /// The file the executable `executable` of the `executables` the `copy`th pipeline named
    /// `pipeline` has is written to in `folder`, counting from nought: the name with every character
    /// but a letter, a digit, `.`, `_` and `-` made `-`, so a variant's words (`landing sun moons`)
    /// are one path segment on every system; `@` and the copy's number from the second; and the
    /// executable's number where there is more than one.
    std::filesystem::path pipelineCodeFile(const std::filesystem::path& folder, std::string_view pipeline,
        std::uint32_t copy, std::uint32_t executable, std::uint32_t executables);

    /// What a `pipelineCodeFile` holds: each form under a heading of its name and its description, in
    /// the order the driver gave them.
    std::string pipelineCodeText(std::span<const PipelineCodeForm> forms);

    /// Where the driver's own code of every pipeline a device makes is written
    /// (`DeviceReport::mCodeFolder`), and how many of each name were.
    class PipelineCodeFolder
    {
    public:
        /// Makes `folder` where it is not there.
        explicit PipelineCodeFolder(std::filesystem::path folder);

        /// Which pipeline named `pipeline` this one is, from nought. **A name is not a pipeline**:
        /// the shadow filter's three fields are made under one, and each writes beside the others
        /// rather than over them. Under a lock, because the kernels compile on several hands at
        /// once.
        std::uint32_t claim(std::string_view pipeline);

        /// Writes `forms` to the `pipelineCodeFile` of the arguments, and says where it could not.
        void write(std::string_view pipeline, std::uint32_t copy, std::uint32_t executable, std::uint32_t executables,
            std::span<const PipelineCodeForm> forms) const;

    private:
        std::filesystem::path mFolder;
        std::mutex mLock;
        std::map<std::string, std::uint32_t, std::less<>> mClaimed;
    };
}
