#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <osg/Vec2i>

#include <components/esm/refid.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/terrain/objectstorage.hpp>
#include <components/terrain/pagedcellref.hpp>
#include <components/vfs/pathutil.hpp>

namespace ESM
{
    struct Cell;
}

namespace MWWorld
{
    class ESMStore;
}

namespace MWRender
{
    /// The game's records a thread other than the main one may ask: the static ones alone, which stand from
    /// the content's load to the session's end. The dynamic records a save makes are cleared on the main thread
    /// at every `World::clear`, so a lookup that reads them first from a reader thread races that clear.
    /// Defined in `objectpaging.cpp`, beside the walk that asks it.
    class StaticRecords
    {
    public:
        explicit StaticRecords(const MWWorld::ESMStore& store)
            : mStore(store)
        {
        }

        /// The record type a base record id names, or 0.
        int findType(const ESM::RefId& id) const;

        const ESM::Cell* searchExterior(int x, int y) const;

        std::optional<SceneUtil::LightCommon> getLight(const ESM::RefId& id) const;

        /// The model a base record of `type` names, empty where it names none.
        VFS::Path::Normalized getModel(int type, const ESM::RefId& id) const;

    private:
        const MWWorld::ESMStore& mStore;
    };

    /// What the running game's content files say stands where: the walk `ObjectPaging` reads its
    /// chunks through, offered to a renderer that stands the distance itself, whose reader threads ask it.
    /// Holds nothing; every answer comes out of the game's `StaticRecords`. Defined in `objectpaging.cpp`,
    /// beside the walk.
    class ObjectStorage final : public Terrain::ObjectStorage
    {
    public:
        /// Whether `collect` hands a cell's reference of record `type` to a chunk as near as one
        /// cell, lamps included: what the distance stands of a cell, and so which references a
        /// script's word on them reaches (`MWScript::VisibilityGates`).
        static bool handsOver(int type);

        void collect(float size, const osg::Vec2i& startCell, ESM::RefId worldspace, Terrain::RefKinds kinds,
            Terrain::RefCollector& collector, std::vector<Terrain::PagedCellRef>& into) const override;

        std::unique_ptr<Terrain::RefCollector> makeCollector() const override;

        std::optional<SceneUtil::LightCommon> getLight(const ESM::RefId& id) const override;

        VFS::Path::Normalized getModel(const ESM::RefId& id) const override;

    private:
        static StaticRecords records();
    };
}
