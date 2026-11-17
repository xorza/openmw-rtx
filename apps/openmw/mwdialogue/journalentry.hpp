#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <components/esm/refid.hpp>

namespace ESM
{
    struct JournalEntry;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWDialogue
{
    /// \brief Basic quest/dialogue/topic entry
    struct Entry
    {
        ESM::RefId mInfoId;
        std::string mText;
        std::string mActorName; // optional

        Entry() = default;

        /// actor is optional
        Entry(const ESM::RefId& topic, const ESM::RefId& infoId, const MWWorld::Ptr& actor);

        // An entry converts from the journal record it reads.
        // NOLINTNEXTLINE(google-explicit-constructor)
        Entry(const ESM::JournalEntry& record);

        const std::string& getText() const;

        void write(ESM::JournalEntry& entry) const;
    };

    /// \brief A dialogue entry
    ///
    /// Same as entry, but store TopicID
    struct JournalEntry : public Entry
    {
        ESM::RefId mTopic;

        JournalEntry() = default;

        JournalEntry(const ESM::RefId& topic, const ESM::RefId& infoId, const MWWorld::Ptr& actor);

        // NOLINTNEXTLINE(google-explicit-constructor)
        JournalEntry(const ESM::JournalEntry& record);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void write(ESM::JournalEntry& entry) const;

        static JournalEntry makeFromQuest(const ESM::RefId& topic, std::int32_t index);

        static const ESM::RefId& idFromIndex(const ESM::RefId& topic, std::int32_t index);
    };

    /// \brief A quest entry with a timestamp.
    struct StampedJournalEntry : public JournalEntry
    {
        std::int32_t mDay;
        std::int32_t mMonth;
        std::int32_t mDayOfMonth;

        StampedJournalEntry();

        StampedJournalEntry(const ESM::RefId& topic, const ESM::RefId& infoId, std::int32_t day, std::int32_t month,
            std::int32_t dayOfMonth, const MWWorld::Ptr& actor);

        // NOLINTNEXTLINE(google-explicit-constructor)
        StampedJournalEntry(const ESM::JournalEntry& record);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void write(ESM::JournalEntry& entry) const;

        static StampedJournalEntry makeFromQuest(
            const ESM::RefId& topic, std::int32_t index, const MWWorld::Ptr& actor);
    };
}
