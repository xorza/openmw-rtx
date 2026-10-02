#include "migration.hpp"

#include <string>

namespace Settings
{
    void migrateUserSettings(CategorySettingValueMap& user)
    {
        const auto width = user.find(CategorySetting("Video", "resolution x"));
        if (width == user.end() || user.contains(CategorySetting("Video", "window width")))
            return;

        user[CategorySetting("Video", "window width")] = width->second;
        user[CategorySetting("Video", "resolution x")] = "0";

        const auto height = user.find(CategorySetting("Video", "resolution y"));
        if (height != user.end())
        {
            user[CategorySetting("Video", "window height")] = height->second;
            height->second = "0";
        }
    }
}
