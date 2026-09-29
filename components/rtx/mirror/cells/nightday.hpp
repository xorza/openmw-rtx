#pragma once

#include <cstdint>

namespace Rtx
{
    /// Which child every `NightDaySwitch` shows. The first three are the game's own modes, each the
    /// index of the child `DayNightCallback` shows where the switch has that many, and the first
    /// child where it has fewer. `Authored` is a game that drives no switch — `day night switches`
    /// off — where each shows the child its file opens on.
    enum class NightDayMode : std::uint8_t
    {
        Default,
        ExteriorNight,
        InteriorDay,
        Authored,
    };

    /// The modes a part of a model is shown in, one bit each: the intersection of what every
    /// `NightDaySwitch` above it shows it in, and every mode where no switch is above it.
    struct NightDayModes
    {
        static constexpr std::uint8_t sEvery = 0xf;

        std::uint8_t mBits = sEvery;

        static constexpr NightDayModes only(NightDayMode mode)
        {
            return NightDayModes{ .mBits = static_cast<std::uint8_t>(1u << static_cast<unsigned>(mode)) };
        }

        constexpr bool has(NightDayMode mode) const { return (mBits & only(mode).mBits) != 0; }

        constexpr bool isEvery() const { return mBits == sEvery; }
        constexpr bool isNone() const { return mBits == 0; }

        constexpr NightDayModes operator|(NightDayModes other) const
        {
            return NightDayModes{ .mBits = static_cast<std::uint8_t>(mBits | other.mBits) };
        }

        constexpr NightDayModes operator&(NightDayModes other) const
        {
            return NightDayModes{ .mBits = static_cast<std::uint8_t>(mBits & other.mBits) };
        }

        constexpr bool operator==(const NightDayModes& other) const = default;
    };
}
