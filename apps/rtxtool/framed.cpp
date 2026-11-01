#include "framed.hpp"

#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <components/files/configurationmanager.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>
#include <components/settings/windowmode.hpp>

namespace RtxTool
{
    ShippedDefaults::ShippedDefaults(const Files::ConfigurationManager& config)
        : ShippedDefaults(config.getActiveConfigPaths().front() / "defaults.bin")
    {
    }

    ShippedDefaults::ShippedDefaults(const std::filesystem::path& file)
    {
        Settings::SettingsFileParser parser;
        parser.loadSettingsFile(file, mShipped, true, false);
    }

    template <class T>
    T ShippedDefaults::get(const std::string_view category, const std::string_view setting) const
    {
        const auto found = mShipped.find(Settings::CategorySetting(category, setting));
        if (found == mShipped.end())
            throw std::runtime_error(std::format("defaults.bin names no [{}] {}", category, setting));

        const std::string& text = found->second;
        if constexpr (std::is_same_v<T, std::string>)
            return text;
        else if constexpr (std::is_same_v<T, bool>)
            return Misc::StringUtils::ciEqual(text, "true");
        else
        {
            if (const std::optional<T> number = Misc::StringUtils::toNumeric<T>(text))
                return *number;

            throw std::runtime_error(
                std::format("defaults.bin's [{}] {} is not a finite number: \"{}\"", category, setting, text));
        }
    }

    template float ShippedDefaults::get<float>(std::string_view, std::string_view) const;
    template int ShippedDefaults::get<int>(std::string_view, std::string_view) const;
    template bool ShippedDefaults::get<bool>(std::string_view, std::string_view) const;
    template std::string ShippedDefaults::get<std::string>(std::string_view, std::string_view) const;

    void WindowRequest::keepPlayersLimit()
    {
        mFramerateLimit = Settings::video().mFramerateLimit;
    }

    void WindowRequest::writeSettings() const
    {
        // The frame at the window's size, which the run states in pixels (`RunSetup::mWindowPixels`).
        Settings::video().mResolutionX.set(static_cast<int>(mWidth));
        Settings::video().mResolutionY.set(static_cast<int>(mHeight));
        Settings::video().mWindowMode.set(Settings::WindowMode::Windowed);
        // **Never the player's vertical sync**: on a display with a fixed refresh it holds each frame
        // to whole refreshes, a mix of one, two and three wherever the frame falls between them, and
        // a run is read for the frame and not for the display's cadence.
        Settings::video().mVsyncMode.set(SDLUtil::VSyncMode::Disabled);
        Settings::video().mFramerateLimit.set(mFramerateLimit);
        Settings::camera().mFieldOfView.set(mFieldOfView);
    }
}
