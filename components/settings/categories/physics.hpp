#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

namespace Settings
{
    struct PhysicsCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<std::int32_t> mAsyncNumThreads{ mIndex, "Physics", "async num threads", makeMaxSanitizerInt(0) };
        SettingValue<std::int32_t> mLineofsightKeepInactiveCache{ mIndex, "Physics", "lineofsight keep inactive cache",
            makeMaxSanitizerInt(-1) };
    };
}
