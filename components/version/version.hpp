#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Version
{
    std::string_view getVersion();
    std::string_view getCommitHash();
    std::string_view getTagHash();
    std::int32_t getLuaApiRevision();
    std::int32_t getPostprocessingApiRevision();

    /// The fork's release, as the package names it: `26.11.0`, or `26.11.0-12-gabc1234` past it.
    std::string_view getRtxVersion();

    /// The upstream commit the fork's newest merge of upstream took in, or nothing in a clone shallower than it.
    std::string_view getUpstreamCommit();

    /// What a build names itself by.
    struct VersionNames
    {
        std::string_view mRtx;
        std::string_view mUpstream;
        std::string_view mUpstreamCommit;
    };

    /// `OpenMW-RTX <release>` and, on a line of its own, `based on OpenMW <upstream version> (<upstream commit, ten
    /// digits>)`, the commit left out where it is not known. Two lines, as upstream's version and revision were: the
    /// main menu's corner is 280 units wide.
    std::string describeVersion(const VersionNames& names);

    /// `describeVersion` of this build: the log's first line, `--version`, a crash report, the main menu, the editor's
    /// About and the launcher.
    std::string getOpenmwVersionDescription();

    bool checkResourcesVersion(const std::filesystem::path& resourcePath);

    /// Upstream's documentation of its master, which the fork merges: no upstream release is the fork's, and the
    /// newest tag's commit is the fork's own release, whose upstream version upstream may not have published.
    std::string_view getDocumentationUrl();
}
