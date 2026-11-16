#include "expectations.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

#include "blockfile.hpp"
#include "finding.hpp"

namespace RtxTool
{
    Expectations::Expectations(const BlockFile& file)
    {
        const std::span<const Block> blocks = file.getBlocks();
        mEntries.reserve(blocks.size());
        for (const Block& block : blocks)
        {
            const BlockField* kind = nullptr;
            const BlockField* detail = nullptr;
            for (const BlockField& field : block.mFields)
            {
                const BlockField** const into
                    = field.mName == "kind" ? &kind : (field.mName == "detail" ? &detail : nullptr);
                if (into == nullptr)
                    file.refuseUnknown(field, "expectation");
                *into = &field;
            }
            if (kind == nullptr || detail == nullptr)
                file.refuse(block.mLine, "an expectation names its kind and its detail");

            const std::optional<FindingKind> named = findingNamed(kind->mValue);
            if (!named.has_value())
                file.refuseValue(*kind, "a kind of finding");

            // The entries are the blocks read so far, one each in order, so the one matched names its block.
            if (const auto earlier = std::ranges::find_if(mEntries,
                    [&](const Expectation& entry) { return entry.mKind == *named && entry.mDetail == detail->mValue; });
                earlier != mEntries.end())
                file.refuseRepeat(block, blocks[static_cast<std::size_t>(earlier - mEntries.begin())], "expectation");

            mEntries.push_back(Expectation{ .mName = block.mName, .mKind = *named, .mDetail = detail->mValue });
        }
    }

    Expectations Expectations::load(const std::filesystem::path& path)
    {
        return Expectations(BlockFile::load(path));
    }

    bool Expectations::expects(const FindingKind kind, const std::string_view detail) const
    {
        return std::ranges::any_of(mEntries,
            [&](const Expectation& expected) { return expected.mKind == kind && expected.mDetail == detail; });
    }
}
