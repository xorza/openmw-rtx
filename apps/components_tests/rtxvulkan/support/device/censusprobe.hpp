#pragma once

#include <cstdint>
#include <string_view>

#include <components/rtxvulkan/device/notfinitecensus.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx::Testing
{
    /// What a test writes into the device's census, which the device's modules alone write otherwise: stores
    /// counted against a module, as a pass that stored a value not finite leaves them, and the census cleared
    /// again for the teardown, which reports every store nothing took. Host writes between submits, which the
    /// next submit makes visible.
    class CensusProbe
    {
    public:
        explicit CensusProbe(const NotFiniteCensus& census)
            : mCensus(census)
        {
        }

        void count(std::string_view module, std::uint32_t stores) const
        {
            words().mNotFinite[mCensus.kernelOf(module)] += stores;
        }

        void clear() const { words() = Shaders::Census{}; }

    private:
        Shaders::Census& words() const { return *static_cast<Shaders::Census*>(mCensus.mWords.map()); }

        const NotFiniteCensus& mCensus;
    };
}
