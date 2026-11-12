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
    T ShippedDefaults::get(const Settings::SettingValue<T>& setting) const
    {
        const std::string_view category = setting.mCategory;
        const std::string_view name = setting.mName;
        const auto found = mShipped.find(Settings::CategorySetting(category, name));
        if (found == mShipped.end())
            throw std::runtime_error(std::format("defaults.bin names no [{}] {}", category, name));

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
                std::format("defaults.bin's [{}] {} is not a finite number: \"{}\"", category, name, text));
        }
    }

    template float ShippedDefaults::get<float>(const Settings::SettingValue<float>&) const;
    template int ShippedDefaults::get<int>(const Settings::SettingValue<int>&) const;
    template bool ShippedDefaults::get<bool>(const Settings::SettingValue<bool>&) const;
    template std::string ShippedDefaults::get<std::string>(const Settings::SettingValue<std::string>&) const;

    MWRender::RtxSettingValues measuredSettings(const ShippedDefaults& shipped, const ToolLine::Frame& line)
    {
        const Rtx::SamplingSwitches& sampling = line.mReconstruction.mSampling;
        return MWRender::RtxSettingValues{
            .mUpscale = line.mUpscale.mValue,
            .mUpscaler = line.mUpscaler.mValue,
            .mIndirectLighting = sampling.mIndirect,
            .mBounceRays = static_cast<int>(sampling.mBounceRays),
            .mDistantLandCells = shipped.get(Settings::rtx().mDistantLandCells),
            .mViewingDistance = shipped.get(Settings::camera().mViewingDistance),
            .mObjectPaging = shipped.get(Settings::terrain().mObjectPaging),
            .mObjectPagingMinSize = shipped.get(Settings::terrain().mObjectPagingMinSize),
            .mGroundcover = shipped.get(Settings::groundcover().mEnabled),
            .mGroundcoverDistance = shipped.get(Settings::groundcover().mRenderingDistance),
            .mGroundcoverDensity = shipped.get(Settings::groundcover().mDensity),
            .mGroundcoverPointLighting = shipped.get(Settings::groundcover().mPointLighting),
            .mAnisotropy = shipped.get(Settings::general().mAnisotropy),
            .mGamma = shipped.get(Settings::video().mGamma),
            .mLitEnvironmentMaps = shipped.get(Settings::shaders().mApplyLightingToEnvironmentMaps),
        };
    }

    MWRender::RtxSettingValues overLine(const ToolLine::Frame& line, MWRender::RtxSettingValues source)
    {
        const auto take = [](auto& into, const auto& named) {
            if (named.has_value())
                into = *named;
        };
        const auto takeNamed = [](auto& into, const auto& named) {
            if (named.mNamed)
                into = named.mValue;
        };
        takeNamed(source.mUpscale, line.mUpscale);
        takeNamed(source.mUpscaler, line.mUpscaler);
        const Rtx::SamplingSwitches& sampling = line.mReconstruction.mSampling;
        if (line.mBouncesNamed.mIndirect)
            source.mIndirectLighting = sampling.mIndirect;
        if (line.mBouncesNamed.mBounceRays)
            source.mBounceRays = static_cast<int>(sampling.mBounceRays);
        take(source.mGroundcover, line.mGrass);
        take(source.mDistantLandCells, line.mDistantCells);
        take(source.mObjectPaging, line.mDistantStatics);
        take(source.mGamma, line.mGamma);
        return source;
    }

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
