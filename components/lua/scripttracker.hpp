#pragma once

#include <memory>
#include <queue>
#include <utility>

#include "scriptscontainer.hpp"

namespace LuaUtil
{
    class ScriptTracker
    {
        using Frame = unsigned int;
        using TrackedScriptContainer = std::pair<ScriptsContainerWeakPtr, Frame>;
        std::queue<TrackedScriptContainer> mLoadedScripts;
        Frame mFrame = 0;

    public:
        void unloadInactiveScripts(LuaView& lua);

        void onLoad(ScriptsContainer& container);

        std::size_t size() const { return mLoadedScripts.size(); }
    };
}
