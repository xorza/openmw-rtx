#include "fontusers.hpp"

#include <cassert>

#include <MyGUI_FactoryManager.h>

#include "listbox.hpp"
#include "trackedtext.hpp"

namespace Gui
{
    FontUsers::Entry::Entry(FontUsers& users)
        : mUsers(users)
        , mIndex(users.mEntries.size())
    {
        assert(!users.mRefitting && "a font user made while its registry refits");
        users.mEntries.push_back(this);
    }

    FontUsers::Entry::~Entry()
    {
        std::vector<Entry*>& entries = mUsers.mEntries;
        assert(!mUsers.mRefitting && "a font user destroyed while its registry refits");
        assert(mIndex < entries.size() && entries[mIndex] == this && "a font user the registry does not hold");

        Entry* const last = entries.back();
        entries[mIndex] = last;
        last->mIndex = mIndex;
        entries.pop_back();
    }

    FontUsers::~FontUsers()
    {
        assert(mEntries.empty() && "a font user outlived its registry");
    }

    FontUsers::FontUsers(const Users users, const int textHeight)
        : mTextHeight(textHeight)
    {
        MyGUI::FactoryManager& factories = MyGUI::FactoryManager::getInstance();
        switch (users)
        {
            case Users::Texts:
                // The category and the names `MyGUI::SubWidgetManager` registers MyGUI's own under.
                factories.unregisterFactory("BasisSkin", "EditText");
                factories.unregisterFactory("BasisSkin", "SimpleText");
                factories.registerFactory("BasisSkin", "EditText", MyGUI::newDelegate(this, &FontUsers::makeText));
                factories.registerFactory(
                    "BasisSkin", "SimpleText", MyGUI::newDelegate(this, &FontUsers::makeSimpleText));
                return;
            case Users::Lists:
                // The category and the name `MyGUI::WidgetManager` registers MyGUI's own under.
                factories.unregisterFactory("Widget", "ListBox");
                factories.registerFactory("Widget", "ListBox", MyGUI::newDelegate(this, &FontUsers::makeList));
                return;
            case Users::Entered:
                return;
        }
    }

    void FontUsers::refit(const int textHeight)
    {
        assert(textHeight > 0 && "a text laid out at no height");
        mTextHeight = textHeight;
        mRefitting = true;
        for (Entry* const entry : mEntries)
            entry->drop();
        for (Entry* const entry : mEntries)
            entry->refit();
        mRefitting = false;
    }

    void FontUsers::makeText(MyGUI::IObject*& made)
    {
        made = new TrackedText(*this);
    }

    void FontUsers::makeSimpleText(MyGUI::IObject*& made)
    {
        made = new TrackedSimpleText(*this);
    }

    void FontUsers::makeList(MyGUI::IObject*& made)
    {
        made = new ListBox(*this);
    }
}
