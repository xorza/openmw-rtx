#include "framed.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <components/files/configurationmanager.hpp>
#include <components/misc/constants.hpp>
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

    const std::string& ShippedDefaults::getText(const Settings::BaseSettingValue& setting) const
    {
        const auto found = mShipped.find(Settings::CategorySetting(setting.mCategory, setting.mName));
        if (found == mShipped.end())
            throw std::runtime_error(std::format("defaults.bin names no [{}] {}", setting.mCategory, setting.mName));
        return found->second;
    }

    template <class T>
    T ShippedDefaults::get(const Settings::SettingValue<T>& setting) const
    {
        const std::string_view category = setting.mCategory;
        const std::string_view name = setting.mName;
        const std::string& text = getText(setting);
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

    MWRender::RtxSettingValues touredSettings(const ShippedDefaults& shipped, MWRender::RtxSettingValues measured)
    {
        measured.mUpscale = Rtx::sUpscaleNames.require(shipped.getText(Settings::rtx().mUpscale), "an upscale mode");
        measured.mUpscaler = Rtx::sUpscalerNames.require(shipped.getText(Settings::rtx().mUpscaler), "an upscaler");
        measured.mDistantLandCells = sTourCells;
        measured.mViewingDistance = sTourCells * static_cast<float>(Constants::CellSizeInUnits);
        return measured;
    }

    void CellPreload::writeSettings() const
    {
        Settings::CellsCategory& cells = Settings::cells();
        cells.mPreloadDistance.set(mDistance);
        cells.mPreloadNumThreads.set(mThreads);
        cells.mPreloadCellCacheMin.set(mCacheMin);
        cells.mPreloadCellCacheMax.set(mCacheMax);
    }

    CellPreload preloadFlownAt(const ShippedDefaults& shipped, const float speed)
    {
        const Settings::CellsCategory& cells = Settings::cells();
        const float faster = speed / sPlayerRunSpeed;
        const int threads
            = static_cast<int>(std::ceil(static_cast<float>(shipped.get(cells.mPreloadNumThreads)) * faster));
        // No more than the ring around the active grid holds cells, (2r + 3)² - (2r + 1)², which is the most the
        // preloader has to load at once on a flight, nor than the processor spares beside the frame's thread.
        constexpr int sRing = (2 * Constants::CellGridRadius + 3) * (2 * Constants::CellGridRadius + 3)
            - (2 * Constants::CellGridRadius + 1) * (2 * Constants::CellGridRadius + 1);
        const int processors = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        // What the preloader may hold at once: the grid and its ring around the centre the player stands at and
        // around the one the flight is predicted to reach, (2r + 3)² each, 50 at a radius of one; past the 43 and 44
        // the game asked for under the three contents' tours, a door's surroundings among them.
        constexpr int sCacheTheGameAsksFor
            = 2 * (2 * Constants::CellGridRadius + 3) * (2 * Constants::CellGridRadius + 3);
        return CellPreload{ .mDistance = shipped.get(cells.mPreloadDistance) * faster,
            .mThreads = std::clamp(threads, 1, std::min(sRing, processors)),
            // The least kept stays the shipped file's, so what expired makes room: a cache held full at its most
            // asked for more on every door, 58 against 50.
            .mCacheMin = shipped.get(cells.mPreloadCellCacheMin),
            .mCacheMax = std::max(shipped.get(cells.mPreloadCellCacheMax), sCacheTheGameAsksFor) };
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
