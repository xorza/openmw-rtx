#pragma once

#include <components/esm/refid.hpp>

#include <map>
#include <string>
#include <tuple>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only
    struct MagicEffects
    {
        // <Effect Id, Base value, Modifier>
        std::map<ESM::RefId, std::pair<int32_t, float>> mEffects;

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };

    struct SummonKey
    {
        SummonKey(ESM::RefId effectId, const ESM::RefId& sourceId, int32_t index)
            : mEffectId(effectId)
            , mSourceId(sourceId)
            , mEffectIndex(index)
        {
        }

        ESM::RefId mEffectId;
        ESM::RefId mSourceId;
        int32_t mEffectIndex;
    };

    inline auto makeTupleRef(const SummonKey& value) noexcept
    {
        return std::tie(value.mEffectId, value.mSourceId, value.mEffectIndex);
    }

    // An exception here ends the process, which is what this function means.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    inline bool operator==(const SummonKey& l, const SummonKey& r) noexcept
    {
        return makeTupleRef(l) == makeTupleRef(r);
    }

    // An exception here ends the process, which is what this function means.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    inline bool operator<(const SummonKey& l, const SummonKey& r) noexcept
    {
        return makeTupleRef(l) < makeTupleRef(r);
    }
}
