#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include <components/esm/luascripts.hpp>
#include <components/esm3/refnum.hpp>
#include <components/vfs/pathutil.hpp>

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace LuaUtil
{
    using ScriptIdsWithInitializationData = std::map<std::int32_t, std::string_view>;

    class ScriptsConfiguration
    {
    public:
        void init(ESM::LuaScriptsCfg, bool);

        std::size_t size() const { return mScripts.size(); }
        const ESM::LuaScriptCfg& operator[](std::size_t id) const { return mScripts[id]; }

        std::optional<std::int32_t> findId(VFS::Path::NormalizedView path) const;
        std::optional<std::int32_t> mapId(std::int32_t savedId) const;

        bool isCustomScript(std::int32_t id) const
        {
            return mScripts[static_cast<std::size_t>(id)].mFlags & ESM::LuaScriptCfg::sCustom;
        }

        ScriptIdsWithInitializationData getMenuConf() const { return getConfByFlag(ESM::LuaScriptCfg::sMenu); }
        ScriptIdsWithInitializationData getGlobalConf() const { return getConfByFlag(ESM::LuaScriptCfg::sGlobal); }
        ScriptIdsWithInitializationData getPlayerConf() const { return getConfByFlag(ESM::LuaScriptCfg::sPlayer); }
        ScriptIdsWithInitializationData getLoadConf() const { return getConfByFlag(ESM::LuaScriptCfg::sLoad); }
        ScriptIdsWithInitializationData getLocalConf(
            std::uint32_t type, const ESM::RefId& recordId, ESM::RefNum refnum) const;

        void read(ESM::ESMReader&);
        void write(ESM::ESMWriter&) const;

    private:
        ScriptIdsWithInitializationData getConfByFlag(ESM::LuaScriptCfg::Flags flag) const;

        std::vector<ESM::LuaScriptCfg> mScripts;
        std::map<VFS::Path::Normalized, std::int32_t, std::less<>> mPathToIndex;

        struct DetailedConf
        {
            std::int32_t mScriptId;
            bool mAttach;
            std::string_view mInitializationData;
        };
        std::map<std::uint32_t, std::vector<std::int32_t>> mScriptsPerType;
        std::map<ESM::RefId, std::vector<DetailedConf>, std::less<>> mScriptsPerRecordId;
        std::map<ESM::RefNum, std::vector<DetailedConf>> mScriptsPerRefNum;
        std::map<std::int32_t, std::int32_t> mScriptIdMapping;
    };

    // Parse ESM::LuaScriptsCfg from text and add to `cfg`.
    void parseOMWScripts(ESM::LuaScriptsCfg& cfg, std::string_view data);

    std::string scriptCfgToString(const ESM::LuaScriptCfg& script);

}
