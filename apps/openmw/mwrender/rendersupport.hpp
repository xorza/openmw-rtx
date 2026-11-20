#pragma once

#include <span>
#include <string>
#include <string_view>

#include "rendermode.hpp"

namespace MWRender
{
    /// What a renderer makes of one setting the game's renderers read: honoured, or declined for
    /// the reason a player is shown. A category with no name is every key of it.
    struct SettingSupport
    {
        std::string_view mCategory;
        std::string_view mName;

        /// Empty where the renderer honours the setting.
        std::string_view mDeclined;
    };

    /// The same of a render mode the console and Lua toggle.
    struct ModeSupport
    {
        RenderMode mMode;
        std::string_view mDeclined;
    };

    /// What the console or a script is told where the renderer declines what it asked for: what it
    /// asked, and the reason, in place of a state the renderer would never have drawn.
    std::string notAvailable(std::string_view what, std::string_view declined);

    /// **One declaration of what a renderer honours**, made once per renderer and asked by every
    /// reader that would otherwise branch on which renderer it has: the console and Lua answer a
    /// declined mode with it, and `Renderer::processChangedSettings` hands the renderer only what it
    /// honours. A request that is a call of its own answers its refusal itself (`Renderer::reloadShaders`).
    /// A key or a mode the declaration does not name is honoured:
    /// the tables list what a renderer has a reason to refuse, and the ray tracer's lists every key
    /// the game's renderers read, so that a key upstream adds is decided before it is shipped. The
    /// settings window and the launcher offer only the keys the ray tracer honours, which
    /// `RtxSupportTest` holds them to.
    class RenderSupport
    {
    public:
        constexpr RenderSupport(std::span<const SettingSupport> settings, std::span<const ModeSupport> modes)
            : mSettings(settings)
            , mModes(modes)
        {
        }

        /// Why the renderer declines the setting, or empty where it honours it.
        std::string_view declinedSetting(std::string_view category, std::string_view name) const;

        /// Whether the declaration names the setting at all, as its own key or by its category.
        bool namesSetting(std::string_view category, std::string_view name) const;

        std::string_view declinedMode(RenderMode mode) const;

        std::span<const SettingSupport> getSettings() const { return mSettings; }
        std::span<const ModeSupport> getModes() const { return mModes; }

    private:
        /// The entry that answers for the setting: its own key, or else its category's.
        const SettingSupport* find(std::string_view category, std::string_view name) const;

        std::span<const SettingSupport> mSettings;
        std::span<const ModeSupport> mModes;
    };
}
