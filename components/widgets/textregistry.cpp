#include "textregistry.hpp"

#include <cassert>

#include <MyGUI_FactoryManager.h>

#include "trackedtext.hpp"

namespace Gui
{
    TextRegistry::Entry::Entry(TextRegistry& registry)
        : mRegistry(registry)
        , mIndex(registry.mEntries.size())
    {
        registry.mEntries.push_back(this);
    }

    TextRegistry::Entry::~Entry()
    {
        std::vector<Entry*>& entries = mRegistry.mEntries;
        assert(mIndex < entries.size() && entries[mIndex] == this && "a text the registry does not hold");

        Entry* const last = entries.back();
        entries[mIndex] = last;
        last->mIndex = mIndex;
        entries.pop_back();
    }

    TextRegistry::~TextRegistry()
    {
        assert(mEntries.empty() && "a text outlived its registry");
    }

    void TextRegistry::registerFactories()
    {
        // The category and the names `MyGUI::SubWidgetManager` registers MyGUI's own under.
        MyGUI::FactoryManager& factories = MyGUI::FactoryManager::getInstance();
        factories.unregisterFactory("BasisSkin", "EditText");
        factories.unregisterFactory("BasisSkin", "SimpleText");
        factories.registerFactory("BasisSkin", "EditText", MyGUI::newDelegate(this, &TextRegistry::makeText));
        factories.registerFactory("BasisSkin", "SimpleText", MyGUI::newDelegate(this, &TextRegistry::makeSimpleText));
    }

    void TextRegistry::rebind()
    {
        for (Entry* const entry : mEntries)
            entry->rebind();
    }

    void TextRegistry::makeText(MyGUI::IObject*& made)
    {
        made = new TrackedText(*this);
    }

    void TextRegistry::makeSimpleText(MyGUI::IObject*& made)
    {
        made = new TrackedSimpleText(*this);
    }
}
