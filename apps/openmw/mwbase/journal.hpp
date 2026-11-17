#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>

#include <apps/openmw/mwdialogue/journalentry.hpp>
#include <apps/openmw/mwdialogue/quest.hpp>
#include <apps/openmw/mwdialogue/topic.hpp>

namespace Loading
{
    class Listener;
}

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MWBase
{
    /// \brief Interface for the player's journal (implemented in MWDialogue)
    class Journal
    {
        ///< not implemented

        ///< not implemented

    public:
        Journal(const Journal&) = delete;
        Journal& operator=(const Journal&) = delete;
        typedef std::deque<MWDialogue::StampedJournalEntry> TEntryContainer;
        typedef std::map<ESM::RefId, MWDialogue::Quest> TQuestContainer; // topic, quest
        typedef std::map<ESM::RefId, MWDialogue::Topic> TTopicContainer; // topic-id, topic-content

    public:
        Journal() {}

        virtual void clear() = 0;

        virtual ~Journal() = default;

        virtual MWDialogue::Quest& getOrStartQuest(const ESM::RefId& id) = 0;
        ///< Gets the quest requested. Creates it and inserts it in quests if it is not yet started.
        virtual MWDialogue::Quest* getQuestOrNull(const ESM::RefId& id) = 0;
        ///< Gets a pointer to the requested quest. Will return nullptr if the quest has not been started.

        virtual void addEntry(const ESM::RefId& id, std::int32_t index, const MWWorld::Ptr& actor) = 0;
        ///< Add a journal entry.
        /// @param actor Used as context for replacing of escape sequences (%name, etc).

        virtual void setJournalIndex(const ESM::RefId& id, std::int32_t index) = 0;
        ///< Set the journal index without adding an entry.

        virtual std::int32_t getJournalIndex(const ESM::RefId& id) const = 0;
        ///< Get the journal index.

        virtual void addTopic(const ESM::RefId& topicId, const ESM::RefId& infoId, const MWWorld::Ptr& actor) = 0;
        /// \note topicId must be lowercase

        virtual void removeLastAddedTopicResponse(const ESM::RefId& topicId, std::string_view actorName) = 0;
        ///< Removes the last topic response added for the given topicId and actor name.
        /// \note topicId must be lowercase

        virtual const TEntryContainer& getEntries() const = 0;

        virtual const TTopicContainer& getTopics() const = 0;

        virtual const TQuestContainer& getQuests() const = 0;

        virtual std::size_t countSavedGameRecords() const = 0;

        virtual void write(ESM::ESMWriter& writer, Loading::Listener& progress) const = 0;

        virtual void readRecord(ESM::ESMReader& reader, std::uint32_t type) = 0;
    };
}
