#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "findingkind.hpp"

namespace RtxTool
{
    class BlockFile;

    /// One finding a content is known to meet: its kind and its detail as the report spells them, under a section
    /// whose name says why it is known.
    struct Expectation
    {
        std::string mName;
        FindingKind mKind = FindingKind::Count;
        std::string mDetail;
    };

    /// **The findings a content is known to meet** (`--expected`), so a tour fails only on what is not among them and
    /// says which of them it never met: a mod that packs a normal map as a colour, a blend no trace can make, met on
    /// every run of that content and on no other. Kept beside the profile's tour task, as web-platform-tests and
    /// Chromium keep a suite's known failures beside the suite.
    class Expectations
    {
    public:
        Expectations() = default;

        /// The sections of `file`, each `[why it is known]` with `kind`, a finding's name as `--fail-on` spells it,
        /// and `detail`, the finding's detail as the report spells it: `refused` and `a texture "textures/a.dds"`.
        /// Refuses a section without both, a field past them, a kind no finding has, and a kind and detail an
        /// earlier section named.
        explicit Expectations(const BlockFile& file);

        /// The same, from the file at `path`, which is refused where it cannot be read.
        static Expectations load(const std::filesystem::path& path);

        /// Whether a finding of `kind` and `detail` is one of these.
        bool expects(FindingKind kind, std::string_view detail) const;

        std::span<const Expectation> getEntries() const { return mEntries; }

    private:
        std::vector<Expectation> mEntries;
    };
}
