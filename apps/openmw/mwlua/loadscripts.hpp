#pragma once

#include <components/lua/luastate.hpp>
#include <components/lua/scriptscontainer.hpp>

namespace MWLua
{

    class LoadScripts : public LuaUtil::ScriptsContainer
    {
    public:
        explicit LoadScripts(LuaUtil::LuaState* lua)
            : LuaUtil::ScriptsContainer(lua, "Load")
        {
            registerEngineHandlers({
                &mContentFilesLoadedHandlers,
            });
        }

        void contentFilesLoaded() { callEngineHandlers(mContentFilesLoadedHandlers); }

    private:
        EngineHandlerList mContentFilesLoadedHandlers{ "onContentFilesLoaded" };
    };

}
