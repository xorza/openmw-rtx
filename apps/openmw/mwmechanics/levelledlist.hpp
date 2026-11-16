#pragma once

#include <optional>

#include <components/misc/rng.hpp>

namespace ESM
{
    struct LevelledListBase;
    class RefId;
}

namespace MWMechanics
{

    /// @return ID of resulting item, or empty if none
    ESM::RefId getLevelledItem(
        const ESM::LevelledListBase* levItem, bool creature, Misc::Rng::Generator& prng, std::optional<int> level = {});

}
