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

    /// A request a script makes of the picture that is no render mode and no setting.
    enum class ScriptRequest
    {
        /// The console's `ToggleBorders`.
        Borders,

        /// Lua's `debug.triggerShaderReload`.
        ShaderReload,

        /// Lua's `debug.setShaderHotReloadEnabled`.
        LiveShaderReload,
    };

    struct RequestSupport
    {
        ScriptRequest mRequest;
        std::string_view mDeclined;
    };

    /// What the console or a script is told where the renderer declines what it asked for: what it
    /// asked, and the reason, in place of a state the renderer would never have drawn.
    std::string notAvailable(std::string_view what, std::string_view declined);

    /// **One declaration of what a renderer honours**, made once per renderer and asked by every
    /// reader that would otherwise branch on which renderer it has: the settings window greys a
    /// declined control out and shows the reason, the console and Lua answer a declined mode or
    /// request with it, and `Renderer::processChangedSettings` hands the renderer only what it
    /// honours. A key, a mode or a request the declaration does not name is honoured: the tables
    /// list what a renderer has a reason to refuse, and the ray tracer's lists every key the
    /// game's renderers read, so that a key upstream adds is decided before it is shipped.
    class RenderSupport
    {
    public:
        constexpr RenderSupport(std::span<const SettingSupport> settings, std::span<const ModeSupport> modes,
            std::span<const RequestSupport> requests)
            : mSettings(settings)
            , mModes(modes)
            , mRequests(requests)
        {
        }

        /// Why the renderer declines the setting, or empty where it honours it.
        std::string_view declinedSetting(std::string_view category, std::string_view name) const;

        /// Whether the declaration names the setting at all, as its own key or by its category.
        bool namesSetting(std::string_view category, std::string_view name) const;

        std::string_view declinedMode(RenderMode mode) const;
        std::string_view declinedRequest(ScriptRequest request) const;

        std::span<const SettingSupport> getSettings() const { return mSettings; }

    private:
        /// The entry that answers for the setting: its own key, or else its category's.
        const SettingSupport* find(std::string_view category, std::string_view name) const;

        std::span<const SettingSupport> mSettings;
        std::span<const ModeSupport> mModes;
        std::span<const RequestSupport> mRequests;
    };
}
