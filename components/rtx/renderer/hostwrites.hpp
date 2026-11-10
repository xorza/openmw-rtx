#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// How what the host rewrites reaches the device: every table a frame writes, and the vertices
    /// the interface and the debug lines draw. One answer for every device, the backend's to state
    /// (`docs/rtx/architecture.md`, "Host writes travel one way").
    enum class HostWrites : std::uint8_t
    {
        /// Written into video memory the host maps: as fast as a write gets, and only where that
        /// window is the whole of video memory, which resizable BAR makes it.
        Mapped,

        /// Written into staging and copied into video memory on the queue, ahead of what reads it:
        /// a copy more, and no window needed.
        Staged,
    };

    /// How a `HostWrites` is spelled in a report and a bench record.
    inline constexpr NamedEnum sHostWritesNames{ std::array{
        std::pair{ HostWrites::Mapped, std::string_view("mapped") },
        std::pair{ HostWrites::Staged, std::string_view("staged") },
    } };
}
