#include "migration.hpp"

#include <array>
#include <utility>

#include "categories.hpp"

namespace Settings
{
    void migrateUserSettings(CategorySettingValueMap& user)
    {
        const CategorySetting marker("Video", "resolution sets the frame");
        if (!user.contains(marker))
        {
            const CategorySetting windowWidth("Video", "window width");
            const CategorySetting windowHeight("Video", "window height");
            const std::array moved{
                std::pair{ CategorySetting("Video", "resolution x"), windowWidth },
                std::pair{ CategorySetting("Video", "resolution y"), windowHeight },
            };

            // **Either side alone is upstream's window**: upstream writes a value only where it
            // differs from its default of 800 × 600, so a window of 800 × 480 is a file that holds
            // `resolution y` alone. A file that names a window of its own is the fork's, from
            // before the marker.
            const bool upstream = (user.contains(moved[0].first) || user.contains(moved[1].first))
                && !user.contains(windowWidth) && !user.contains(windowHeight);
            if (upstream)
                for (const auto& [resolution, window] : moved)
                    if (const auto found = user.find(resolution); found != user.end())
                    {
                        user[window] = found->second;
                        found->second = "0";
                    }
        }

        user[marker] = "true";
    }
}
