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
    struct LuaCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mLuaDebug{ mIndex, "Lua", "lua debug" };
        SettingValue<std::int32_t> mLuaNumThreads{ mIndex, "Lua", "lua num threads", makeEnumSanitizerInt({ 0, 1 }) };
        SettingValue<bool> mLuaProfiler{ mIndex, "Lua", "lua profiler" };
        SettingValue<std::uint64_t> mSmallAllocMaxSize{ mIndex, "Lua", "small alloc max size" };
        SettingValue<std::uint64_t> mMemoryLimit{ mIndex, "Lua", "memory limit" };
        SettingValue<bool> mLogMemoryUsage{ mIndex, "Lua", "log memory usage" };
        SettingValue<std::uint64_t> mInstructionLimitPerCall{ mIndex, "Lua", "instruction limit per call",
            makeMaxSanitizerUInt64(1001) };
        SettingValue<std::int32_t> mGcStepsPerFrame{ mIndex, "Lua", "gc steps per frame", makeMaxSanitizerInt(0) };
    };
}
