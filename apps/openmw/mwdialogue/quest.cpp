#include "quest.hpp"

#include <algorithm>
#include <cstdint>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/luamanager.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/queststate.hpp>

namespace MWDialogue
{
    Quest::Quest()
        : Topic()
        , mIndex(0)
        , mFinished(false)
    {
    }

    Quest::Quest(const ESM::RefId& topic)
        : Topic(topic)
        , mIndex(0)
        , mFinished(false)
    {
    }

    Quest::Quest(const ESM::QuestState& state)
        : Topic(state.mTopic)
        , mIndex(state.mState)
        , mFinished(state.mFinished != 0)
    {
    }

    std::string_view Quest::getName() const
    {
        const ESM::Dialogue* dialogue = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(mTopic);

        for (const auto& entry : dialogue->mInfo)
            if (entry.mQuestStatus == ESM::DialInfo::QS_Name)
                return entry.mResponse;

        return {};
    }

    std::int32_t Quest::getIndex() const
    {
        return mIndex;
    }

    void Quest::setIndex(std::int32_t index)
    {
        // The index must be set even if no related journal entry was found
        MWBase::Environment::get().getLuaManager()->questUpdated(mTopic, index);
        mIndex = index;
    }

    bool Quest::isFinished() const
    {
        return mFinished;
    }

    void Quest::setFinished(bool finished)
    {
        mFinished = finished;
    }

    bool Quest::addEntry(const JournalEntry& entry)
    {
        const ESM::Dialogue* dialogue
            = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(entry.mTopic);

        auto info = std::find_if(
            dialogue->mInfo.begin(), dialogue->mInfo.end(), [&](const auto& i) { return i.mId == entry.mInfoId; });

        if (info == dialogue->mInfo.end() || info->mData.mJournalIndex == -1)
            throw std::runtime_error("unknown journal entry for topic " + mTopic.toDebugString());

        if (info->mQuestStatus == ESM::DialInfo::QS_Finished || info->mQuestStatus == ESM::DialInfo::QS_Restart)
            mFinished = info->mQuestStatus == ESM::DialInfo::QS_Finished;

        if (info->mData.mJournalIndex > mIndex)
        {
            mIndex = info->mData.mJournalIndex;
            MWBase::Environment::get().getLuaManager()->questUpdated(mTopic, mIndex);
        }

        for (const auto& eachEntry : mEntries)
            if (eachEntry.mInfoId == entry.mInfoId)
                return info->mQuestStatus == ESM::DialInfo::QS_Restart;

        mEntries.push_back(entry); // we want slicing here
        return info->mQuestStatus == ESM::DialInfo::QS_Restart;
    }

    void Quest::write(ESM::QuestState& state) const
    {
        state.mTopic = getTopic();
        state.mState = mIndex;
        state.mFinished = mFinished;
    }
}
