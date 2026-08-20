#ifndef OPENMW_COMPONENTS_ESMLOADER_ESMDATA_H
#define OPENMW_COMPONENTS_ESMLOADER_ESMDATA_H

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>
#include <components/misc/tuplemeta.hpp>
#include <components/vfs/pathutil.hpp>

// The record types of `ModelRecords`, whose definitions the list below needs and whose headers are
// therefore part of it. Forward declarations would do for the tuple itself and not for the folds
// over it, which read `sRecordId`, `mId` and `mModel` off each type.
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm3/loadweap.hpp>

#include <bitset>
#include <string_view>
#include <tuple>
#include <vector>

namespace ESM
{
    struct Cell;
    struct GameSetting;
    struct Land;
    class Variant;
}

namespace EsmLoader
{
    struct RefIdWithType
    {
        ESM::RefId mId;
        ESM::RecNameInts mType;
    };

    /// Every record type that names a model, in one list.
    ///
    /// They are read, merged and looked up identically, so this list is the only thing that
    /// distinguishes them: the reader, the store, the query and the model lookup are all generated
    /// from it, and adding a type is this line alone. Cells, lands and game settings are absent
    /// because each keys on something other than a record id and has its own path.
    ///
    /// `ESM::NPC` is absent deliberately. An NPC names no model of its own — it is assembled out of
    /// `ESM::BodyPart` records by race and by what it is wearing, which is a different problem.
    using ModelRecords = std::tuple<ESM::Activator, ESM::Apparatus, ESM::Armor, ESM::Book, ESM::Clothing,
        ESM::Container, ESM::Creature, ESM::Door, ESM::Ingredient, ESM::Light, ESM::Lockpick, ESM::Miscellaneous,
        ESM::Potion, ESM::Probe, ESM::Repair, ESM::Static, ESM::Weapon>;

    /// Which of `ModelRecords` a caller wants, one bit each.
    ///
    /// Selective rather than all-or-nothing because the answer decides what the caller's world is
    /// made of: a navmesh built with the lights and the books in it is a different navmesh.
    using ModelRecordMask = std::bitset<std::tuple_size_v<ModelRecords>>;

    namespace Details
    {
        template <class T>
        struct AsVectors;

        template <class... Ts>
        struct AsVectors<std::tuple<Ts...>>
        {
            using Type = std::tuple<std::vector<Ts>...>;
        };
    }

    /// One vector per type of `ModelRecords`, each sorted by record id.
    using ModelStore = Details::AsVectors<ModelRecords>::Type;

    /// The mask naming exactly `Ts`, which the compiler checks against the list.
    template <class... Ts>
    ModelRecordMask modelRecords()
    {
        ModelRecordMask mask;
        (mask.set(Misc::TupleTypeIndex<Ts, ModelRecords>::value), ...);
        return mask;
    }

    /// Every model-bearing type there is, for a caller that places whatever a cell holds.
    inline ModelRecordMask allModelRecords()
    {
        return ModelRecordMask().set();
    }

    struct EsmData
    {
        std::vector<ESM::Cell> mCells;
        std::vector<ESM::GameSetting> mGameSettings;
        std::vector<ESM::Land> mLands;
        std::vector<RefIdWithType> mRefIdTypes;

        /// Read through `get`; it is public because the loader fills it in by folding over the list.
        ModelStore mModels;

        EsmData() = default;
        EsmData(const EsmData&) = delete;
        EsmData(EsmData&&) = default;

        ~EsmData();

        /// Every record of one model-bearing type, sorted by id.
        template <class T>
        const std::vector<T>& get() const
        {
            return std::get<std::vector<T>>(mModels);
        }

        template <class T>
        std::vector<T>& get()
        {
            return std::get<std::vector<T>>(mModels);
        }
    };

    VFS::Path::NormalizedView getModel(const EsmData& content, const ESM::RefId& refId, ESM::RecNameInts type);

    ESM::Variant getGameSetting(const std::vector<ESM::GameSetting>& records, std::string_view id);
}

#endif
