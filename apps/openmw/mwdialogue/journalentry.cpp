#include "journalentry.hpp"

#include <cstdint>
#include <stdexcept>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwscript/interpretercontext.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/globals.hpp>
#include <components/esm3/journalentry.hpp>
#include <components/interpreter/defines.hpp>

namespace MWDialogue
{
    Entry::Entry(const ESM::RefId& topic, const ESM::RefId& infoId, const MWWorld::Ptr& actor)
        : mInfoId(infoId)
    {
        const ESM::Dialogue* dialogue = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(topic);

        for (const auto& entry : dialogue->mInfo)
            if (entry.mId == mInfoId)
            {
                if (actor.isEmpty())
                {
                    MWScript::InterpreterContext interpreterContext(nullptr, MWWorld::Ptr());
                    mText = Interpreter::fixDefinesDialog(entry.mResponse, interpreterContext);
                }
                else
                {
                    MWScript::InterpreterContext interpreterContext(&actor.getRefData().getLocals(), actor);
                    mText = Interpreter::fixDefinesDialog(entry.mResponse, interpreterContext);
                }

                return;
            }

        throw std::runtime_error("unknown info ID " + mInfoId.toDebugString() + " for topic " + topic.toDebugString());
    }

    Entry::Entry(const ESM::JournalEntry& record)
        : mInfoId(record.mInfo)
        , mText(record.mText)
        , mActorName(record.mActorName)
    {
    }

    const std::string& Entry::getText() const
    {
        return mText;
    }

    void Entry::write(ESM::JournalEntry& entry) const
    {
        entry.mInfo = mInfoId;
        entry.mText = mText;
        entry.mActorName = mActorName;
    }

    JournalEntry::JournalEntry(const ESM::RefId& topic, const ESM::RefId& infoId, const MWWorld::Ptr& actor)
        : Entry(topic, infoId, actor)
        , mTopic(topic)
    {
    }

    JournalEntry::JournalEntry(const ESM::JournalEntry& record)
        : Entry(record)
        , mTopic(record.mTopic)
    {
    }

    void JournalEntry::write(ESM::JournalEntry& entry) const
    {
        Entry::write(entry);
        entry.mTopic = mTopic;
    }

    JournalEntry JournalEntry::makeFromQuest(const ESM::RefId& topic, std::int32_t index)
    {
        return JournalEntry(topic, idFromIndex(topic, index), MWWorld::Ptr());
    }

    const ESM::RefId& JournalEntry::idFromIndex(const ESM::RefId& topic, std::int32_t index)
    {
        const ESM::Dialogue* dialogue = MWBase::Environment::get().getESMStore()->get<ESM::Dialogue>().find(topic);

        for (const auto& entry : dialogue->mInfo)
            if (entry.mData.mJournalIndex == index)
            {
                return entry.mId;
            }

        throw std::runtime_error("unknown journal index for topic " + topic.toDebugString());
    }

    StampedJournalEntry::StampedJournalEntry()
        : mDay(0)
        , mMonth(0)
        , mDayOfMonth(0)
    {
    }

    StampedJournalEntry::StampedJournalEntry(const ESM::RefId& topic, const ESM::RefId& infoId, std::int32_t day,
        std::int32_t month, std::int32_t dayOfMonth, const MWWorld::Ptr& actor)
        : JournalEntry(topic, infoId, actor)
        , mDay(day)
        , mMonth(month)
        , mDayOfMonth(dayOfMonth)
    {
    }

    StampedJournalEntry::StampedJournalEntry(const ESM::JournalEntry& record)
        : JournalEntry(record)
        , mDay(record.mDay)
        , mMonth(record.mMonth)
        , mDayOfMonth(record.mDayOfMonth)
    {
    }

    void StampedJournalEntry::write(ESM::JournalEntry& entry) const
    {
        JournalEntry::write(entry);
        entry.mDay = mDay;
        entry.mMonth = mMonth;
        entry.mDayOfMonth = mDayOfMonth;
    }

    StampedJournalEntry StampedJournalEntry::makeFromQuest(
        const ESM::RefId& topic, std::int32_t index, const MWWorld::Ptr& actor)
    {
        const std::int32_t day = MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sDaysPassed);
        const std::int32_t month = MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sMonth);
        const std::int32_t dayOfMonth = MWBase::Environment::get().getWorld()->getGlobalInt(MWWorld::Globals::sDay);

        return StampedJournalEntry(topic, idFromIndex(topic, index), day, month, dayOfMonth, actor);
    }
}
