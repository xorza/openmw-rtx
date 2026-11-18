#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/vfs/pathutil.hpp>

namespace ESM
{
    struct ESM_Context;
    struct Static;
    struct Cell;
}

namespace Loading
{
    class Listener;
}

namespace Files
{
    class Collections;
}

namespace ToUTF8
{
    class Utf8Encoder;
}

namespace MWWorld
{
    template <class T>
    class Store;

    class GroundcoverStore
    {
    private:
        std::map<ESM::RefId, VFS::Path::Normalized> mMeshCache;
        std::map<std::pair<std::int32_t, std::int32_t>, std::vector<ESM::ESM_Context>> mCellContexts;

    public:
        void init(const Store<ESM::Static>& statics, const Files::Collections& fileCollections,
            const std::vector<std::string>& groundcoverFiles, ToUTF8::Utf8Encoder* encoder,
            Loading::Listener* listener);

        VFS::Path::NormalizedView getGroundcoverModel(ESM::RefId id) const
        {
            auto it = mMeshCache.find(id);
            if (it == mMeshCache.end())
                return {};
            return it->second;
        }

        void initCell(ESM::Cell& cell, std::int32_t cellX, std::int32_t cellY) const;
    };
}
