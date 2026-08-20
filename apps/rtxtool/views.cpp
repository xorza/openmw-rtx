#include "views.hpp"

#include "placement.hpp"

#include <algorithm>
#include <stdexcept>

#include <components/files/conversion.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/parser.hpp>

namespace RtxTool
{
    std::vector<View> loadViews(const std::filesystem::path& path)
    {
        Settings::CategorySettingValueMap entries;
        Settings::SettingsFileParser parser;

        // Reuses the settings file parser rather than growing a second one: the shape is the same,
        // a section per view and a key per field, and that parser already has tests.
        parser.loadSettingsFile(path, entries);

        std::vector<View> views;
        for (const auto& [key, value] : entries)
        {
            const std::string& section = key.first;
            const std::string& field = key.second;

            if (views.empty() || views.back().mName != section)
                views.push_back(View{ .mName = section });

            View& view = views.back();
            if (field == "cell")
                view.mCell = value;
            else if (field == "pos")
                view.mOrigin = parseVec3(value, "pos");
            else if (field == "look")
                view.mTarget = parseVec3(value, "look");
            else if (field == "note")
                view.mNote = value;
            else
                throw std::runtime_error("view \"" + section + "\" has no field called \"" + field + "\"");
        }

        for (const View& view : views)
            if (view.mCell.empty())
                throw std::runtime_error("view \"" + view.mName + "\" names no cell");

        if (views.empty())
            throw std::runtime_error(Files::pathToUnicodeString(path) + " defines no views");

        return views;
    }

    const View* findView(const std::vector<View>& views, std::string_view name)
    {
        const auto found = std::find_if(views.begin(), views.end(), [&](const View& v) { return v.mName == name; });
        return found == views.end() ? nullptr : &*found;
    }
}
