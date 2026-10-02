#include "debugbindings.hpp"

#include "context.hpp"
#include "luamanagerimp.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwinput/actions.hpp"

#include "../mwrender/renderer.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/rendersupport.hpp"

#include <components/debug/debuglog.hpp>
#include <components/lua/luastate.hpp>

namespace MWLua
{
    sol::table initDebugPackage(const Context& context)
    {
        auto view = context.sol();
        sol::table api(view, sol::create);

        api["RENDER_MODE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, MWRender::RenderMode>(view,
                {
                    { "CollisionDebug", MWRender::Render_CollisionDebug },
                    { "Wireframe", MWRender::Render_Wireframe },
                    { "Pathgrid", MWRender::Render_Pathgrid },
                    { "Water", MWRender::Render_Water },
                    { "Scene", MWRender::Render_Scene },
                    { "NavMesh", MWRender::Render_NavMesh },
                    { "ActorsPaths", MWRender::Render_ActorsPaths },
                    { "RecastMesh", MWRender::Render_RecastMesh },
                }));

        api["toggleRenderMode"] = [context](MWRender::RenderMode value) {
            context.mLuaManager->addAction([value] {
                MWBase::World& world = *MWBase::Environment::get().getWorld();
                const std::string_view declined
                    = world.getRenderingManager()->getRenderer().support().declinedMode(value);
                if (!declined.empty())
                    Log(Debug::Warning) << MWRender::notAvailable("debug.toggleRenderMode", declined);
                else
                    world.toggleRenderMode(value);
            });
        };

        api["toggleGodMode"] = []() { MWBase::Environment::get().getWorld()->toggleGodMode(); };
        api["isGodMode"] = []() { return MWBase::Environment::get().getWorld()->getGodModeState(); };

        api["toggleAI"] = []() { MWBase::Environment::get().getMechanicsManager()->toggleAI(); };
        api["isAIEnabled"] = []() { return MWBase::Environment::get().getMechanicsManager()->isAIActive(); };

        api["toggleCollision"] = []() { MWBase::Environment::get().getWorld()->toggleCollisionMode(); };
        api["isCollisionEnabled"] = []() {
            auto world = MWBase::Environment::get().getWorld();
            return world->isActorCollisionEnabled(world->getPlayerPtr());
        };

        api["toggleMWScript"] = []() { MWBase::Environment::get().getWorld()->toggleScripts(); };
        api["isMWScriptEnabled"] = []() { return MWBase::Environment::get().getWorld()->getScriptsEnabled(); };

        api["reloadLua"] = []() { MWBase::Environment::get().getLuaManager()->reloadAllScripts(); };

        // Same code path as the screenshot key. Deferred, because the screenshot is captured
        // on the next frame and the input manager is not safe to touch from a Lua thread.
        api["takeScreenshot"] = [context]() {
            context.mLuaManager->addAction(
                [] { MWBase::Environment::get().getInputManager()->executeAction(MWInput::A_Screenshot); });
        };

        api["NAV_MESH_RENDER_MODE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, Settings::NavMeshRenderMode>(view,
                {
                    { "AreaType", Settings::NavMeshRenderMode::AreaType },
                    { "UpdateFrequency", Settings::NavMeshRenderMode::UpdateFrequency },
                }));

        api["setNavMeshRenderMode"] = [context](Settings::NavMeshRenderMode value) {
            context.mLuaManager->addAction(
                [value] { MWBase::Environment::get().getWorld()->getRenderingManager()->setNavMeshMode(value); });
        };

        api["triggerShaderReload"] = [context]() {
            context.mLuaManager->addAction([] {
                MWRender::Renderer& renderer
                    = MWBase::Environment::get().getWorld()->getRenderingManager()->getRenderer();
                const std::string_view declined
                    = renderer.support().declinedRequest(MWRender::ScriptRequest::ShaderReload);
                if (!declined.empty())
                    Log(Debug::Warning) << MWRender::notAvailable("debug.triggerShaderReload", declined);
                else
                    renderer.reloadShaders();
            });
        };

        api["setShaderHotReloadEnabled"] = [context](bool value) {
            context.mLuaManager->addAction([value] {
                MWRender::Renderer& renderer
                    = MWBase::Environment::get().getWorld()->getRenderingManager()->getRenderer();
                const std::string_view declined
                    = renderer.support().declinedRequest(MWRender::ScriptRequest::LiveShaderReload);
                if (!declined.empty())
                    Log(Debug::Warning) << MWRender::notAvailable("debug.setShaderHotReloadEnabled", declined);
                else
                    renderer.setLiveShaderReload(value);
            });
        };

        return LuaUtil::makeReadOnly(api);
    }
}
