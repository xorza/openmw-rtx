#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace LuaUi
{
    class LuaAdapter;
    struct Element;
    struct ScriptSettingsPage
    {
        std::string mName;
        std::string mSearchHints;
        std::shared_ptr<Element> mElement;
    };
    std::size_t scriptSettingsPageCount();
    ScriptSettingsPage scriptSettingsPageAt(std::size_t index);
    void ensureScriptSettingsPage(std::size_t index);
    void attachPageAt(std::size_t index, LuaAdapter* adapter);
}
