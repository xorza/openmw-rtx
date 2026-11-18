#pragma once

#include <cstdint>
#include <string>

#include <Recast.h>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>

#include "tileposition.hpp"

namespace DetourNavigator
{
    struct AgentBounds;
    struct Version;

    class RecastContext final : public rcContext
    {
    public:
        explicit RecastContext(ESM::RefId worldspace, const TilePosition& tilePosition, const AgentBounds& agentBounds,
            const Version& version, Debug::Level maxLogLevel);

        const std::string& getPrefix() const { return mPrefix; }

    private:
        Debug::Level mMaxLogLevel;
        std::string mPrefix;

        void doLog(rcLogCategory category, const char* msg, std::int32_t len) override;
    };
}
