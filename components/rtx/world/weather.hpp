#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Rtx
{
    /// Morrowind's ten weathers, in the order `MWWorld::WeatherManager` registers them.
    ///
    /// That order is not an arrangement of this renderer's: it is what a weather's script id counts
    /// along, and it is the order the `Weather_<name>_*` keys sit in a content file. Naming them
    /// here is what lets the game hand over a script id and the harness a name off a command line
    /// and have the two mean one sky, through `ESM::Weather`'s table.
    enum class Weather : std::uint8_t
    {
        Clear,
        Cloudy,
        Foggy,
        Overcast,
        Rain,
        Thunderstorm,
        Ashstorm,
        Blight,
        Snow,
        Blizzard,
        Count,
    };

    inline constexpr std::size_t sWeatherCount = static_cast<std::size_t>(Weather::Count);

    /// The weather `name` spells, in any case, as the game reads a weather's id; nothing for a
    /// name that is none of the ten.
    std::optional<Weather> weatherNamed(std::string_view name);

    /// The weather the game's script id `id` numbers, or nothing for an id past the ten.
    std::optional<Weather> weatherOfScriptId(int id);

    /// The game's script id for `weather`, which `ESM::Weather` counts along.
    constexpr int scriptIdOf(Weather weather)
    {
        return static_cast<int>(weather);
    }

    /// How `ESM::Weather` spells `weather`.
    std::string_view nameOf(Weather weather);
}
