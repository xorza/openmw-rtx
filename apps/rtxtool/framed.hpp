#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <apps/openmw/mwrender/rtx/rtxsettings.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/settingvalue.hpp>

#include "toolline.hpp"

namespace Files
{
    struct ConfigurationManager;
}

namespace RtxTool
{
    /// How long `check` holds the queue after every frame's trace, in milliseconds, where the line
    /// names no `--hold` of its own.
    ///
    /// Always, so a hazard that needs two frames in flight shows on the first frame of every run rather than
    /// one run in four: a held queue keeps the device that far behind the host, so every frame is recorded
    /// over a frame still running (`Rtx::StressPass`). Eight is half a frame at the target, so a place is not
    /// much slower for it.
    inline constexpr double sCheckHoldMs = 8.0;

    /// The frame `check` draws where the line names no `--size` of its own, and the one `omw repeat` walks at.
    ///
    /// **A quarter of 1920x1080's pixels**, since none of `check`'s claims but `finite` reads a pixel and what a
    /// place holds is the same at every size: 179 s became 91 on a low-end AMD integrated GPU, where the card bounds
    /// the larger frame and the host this one (`docs/research/2026-11-13-gate-step-times.md`). What it costs:
    /// `finite` and a pair's hashes see a quarter of the pixels a frame, so what one pixel in a million meets is met
    /// a quarter as often.
    ///
    /// **And odd on purpose.** Traced at 638x359 under the default upscale, so no extent of the frame shown or
    /// traced is a multiple of the 8 the workgroups are a side, and three of the four are odd: every 8x8 pass draws
    /// a partial group and every chain that halves meets an odd extent by its second level, which 1920x1080 over
    /// 1280x720 never did. It costs what 960x540 does. A resized window gives a player such a frame, and Khronos's
    /// OpenGL ES conformance run draws at 64x64 and then at 113x47.
    inline constexpr FrameSize sCheckFrame{ .mWidth = 957, .mHeight = 539 };

    /// What a frame is upscaled by when nobody names a mode. The one knob whose default is the
    /// harness's own and not `settings-default.cfg`'s. Quality rather than Performance, so a plain
    /// run is the renderer with everything on and not one that quietly quartered its pixels.
    inline constexpr Rtx::Upscale sUpscaleByDefault = Rtx::Upscale::Quality;

    /// What every film is made under, and no line chooses another: native, every pixel traced and
    /// the upscaler still reconstructing across frames, because a film is watched and never timed,
    /// and the one picture a person keeps is the best one the renderer draws.
    inline constexpr Rtx::Upscale sFilmUpscale = Rtx::Upscale::Native;

    /// Where a hosted run's frames are presented: what goes into the settings the engine makes its
    /// window from, and nothing the renderer is made with.
    struct WindowRequest
    {
        /// The size the frame is presented at. What it is traced at follows from the profile's
        /// upscaling.
        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;

        float mFieldOfView = 60.0f;

        /// The rate the engine's limiter holds the frame to, nought for none: none for a run nobody
        /// watches, where a frame held back is a frame spent waiting. The present never waits for
        /// the refresh (`writeSettings`).
        float mFramerateLimit = 0.0f;

        /// Takes the player's own limit, which a watched window keeps: it is the played game with
        /// the walls off.
        void keepPlayersLimit();

        /// Writes the window into the settings the engine makes it from, vertical sync off whatever
        /// the player chose.
        void writeSettings() const;
    };

    /// What a command's frames are traced with, read once off the command line into the two records
    /// the engine takes — the window, and the `RunSetup` the renderer is made with — and the one
    /// thing the place takes. Where a run stands is not here: the hour and the sky belong to the
    /// place, and `stopFor` is where the command line meets it.
    struct Framed
    {
        WindowRequest mWindow;

        /// What the renderer is made with, whole: the line's profile, mirror, layers,
        /// shaders and budget, hidden until a command says otherwise. The request `sessionFor`
        /// builds carries it as it is, so a knob `RunSetup` gains reaches every command by being
        /// read here.
        MWRender::RunSetup mSetup{ .mRun = { .mProfile = { .mUpscale = sUpscaleByDefault } }, .mHeadless = true };

        /// The step every frame stands for — `SessionRequest::mStep` — the harness's own rate until
        /// a command says otherwise.
        std::optional<float> mStep = sStepSeconds;

        /// Which day, counted from the one a new game begins on. Only the moons read it.
        int mDay = 0;
    };

    /// What a setting is where nobody has set it: the shipped defaults, out of the `defaults.bin` beside the
    /// first configuration file the run found, read once and never the player's own values, so two measured
    /// runs are one run whatever a settings file says. Not `Settings::Manager::mDefaultSettings`, which
    /// layers every configuration directory but the last over the shipped file, and the harness's own
    /// directory is the last.
    class ShippedDefaults
    {
    public:
        explicit ShippedDefaults(const Files::ConfigurationManager& config);

        /// Out of `file`, a `defaults.bin`: what a test reads its own of.
        explicit ShippedDefaults(const std::filesystem::path& file);

        /// What the file says of `setting`, the registry's own, read as the game reads its text: a
        /// number through `Misc::StringUtils::toNumeric`, which refuses one that is not finite, and a
        /// switch as `true` in any case. Throws naming the setting where the file names none, or the
        /// text is no number. For `float`, `int`, `bool` and `std::string`, the text itself.
        template <class T>
        T get(const Settings::SettingValue<T>& setting) const;

    private:
        Settings::CategorySettingValueMap mShipped;
    };

    /// A measured run's settings where its line names none: the shipped defaults, so two machines differing
    /// only in `settings.cfg` measure one scene under one line. The upscale mode, upscaler and bounces take
    /// the harness's own defaults (`sUpscaleByDefault`, FSR, the request's), so a picture does not change
    /// with the card it was measured on.
    MWRender::RtxSettingValues measuredSettings(const ShippedDefaults& shipped, const ToolLine::Frame& line);

    /// **The one rule for what a run's line names**: each setting the line names is the line's,
    /// and every other is `source`'s — the player's registry for a window they watch, which is the
    /// played game with the walls off, and `measuredSettings` for a measured run.
    MWRender::RtxSettingValues overLine(const ToolLine::Frame& line, MWRender::RtxSettingValues source);
}
