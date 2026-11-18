#pragma once

#include <cstdint>

#include <components/toutf8/toutf8.hpp>

#include "bookpage.hpp"
#include "journalviewmodel.hpp"

namespace MWGui
{
    std::int32_t getCyrillicIndexPageCount();

    const MyGUI::Colour journalHeaderColour = MyGUI::Colour(0.60f, 0.00f, 0.00f);

    struct JournalBooks
    {
        std::shared_ptr<JournalViewModel> mModel;

        JournalBooks(std::shared_ptr<JournalViewModel> model, ToUTF8::FromType encoding);

        std::shared_ptr<TypesetBook> createEmptyJournalBook();
        std::shared_ptr<TypesetBook> createJournalBook();
        std::shared_ptr<TypesetBook> createTopicBook(const MWDialogue::Topic& topic);
        std::shared_ptr<TypesetBook> createQuestBook(std::string_view questName);
        std::shared_ptr<TypesetBook> createTopicIndexBook();

        ToUTF8::FromType mEncoding;
        std::int32_t mIndexPagesCount;

    private:
        std::shared_ptr<BookTypesetter> createTypesetter();
        std::shared_ptr<BookTypesetter> createLatinJournalIndex();
        std::shared_ptr<BookTypesetter> createCyrillicJournalIndex();
    };
}
