#pragma once

#include <string>

#include <components/esm/refid.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;

    // format 0, saved games only

    struct QuestState
    {
        ESM::RefId mTopic; // lower case id
        int32_t mState{};
        unsigned char mFinished{};

        void load(ESMReader& esm);
        void save(ESMWriter& esm) const;
    };
}
