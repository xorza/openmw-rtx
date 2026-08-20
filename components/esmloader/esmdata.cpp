#include "esmdata.hpp"

#include "lessbyid.hpp"

#include <components/esm/defs.hpp>
// The three types `esmdata.hpp` only forward-declares. `~EsmData` is here, so it destroys their
// vectors here and needs them complete.
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadland.hpp>
#include <components/esm3/variant.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace EsmLoader
{
    namespace
    {
        /// Whether `records` is the table `type` names, filling in `model` when it also holds `refId`.
        ///
        /// Answering "wrong table" and "right table, no such record" differently is what lets the
        /// fold below stop at the first table that matches instead of searching all of them.
        template <class T>
        bool findModel(const std::vector<T>& records, const ESM::RefId& refId, ESM::RecNameInts type,
            VFS::Path::NormalizedView& model)
        {
            if (T::sRecordId != type)
                return false;

            const auto it = std::lower_bound(records.begin(), records.end(), refId, LessById{});
            if (it != records.end() && it->mId == refId)
                model = it->mModel.getNormalized();

            return true;
        }
    }

    EsmData::~EsmData() {}

    VFS::Path::NormalizedView getModel(const EsmData& content, const ESM::RefId& refId, ESM::RecNameInts type)
    {
        // The view points into the record that was found, which outlives this call because it is
        // owned by `content`. Copying the path into a local here would return a dangling view.
        VFS::Path::NormalizedView model;
        std::apply([&](const auto&... records) { (findModel(records, refId, type, model) || ...); }, content.mModels);
        return model;
    }

    ESM::Variant getGameSetting(const std::vector<ESM::GameSetting>& records, std::string_view id)
    {
        auto it = std::lower_bound(records.begin(), records.end(), id, LessById{});
        if (it == records.end() || it->mId != id)
            throw std::runtime_error("Game settings \"" + std::string(id) + "\" is not found");
        return it->mValue;
    }
}
