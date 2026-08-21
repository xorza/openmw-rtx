#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include <osg/Matrixf>

#include <components/esm3/readerscache.hpp>
#include <components/esmloader/esmdata.hpp>
#include <components/files/collections.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include "terrainstorage.hpp"

namespace boost::program_options
{
    class variables_map;
}

namespace Files
{
    struct ConfigurationManager;
}

namespace Resource
{
    class ImageManager;
    class ResourceSystem;
    class SceneManager;
}

namespace Terrain
{
    class TerrainGrid;
}

namespace osg
{
    class Group;
    class Node;
}

namespace ESM
{
    struct Light;
    struct Cell;
}

namespace RtxTool
{
    /// A Morrowind installation, opened with no window and no game running.
    ///
    /// Everything OpenMW builds below its simulation on the way to a frame — the virtual file
    /// system, the content files, and the resource managers that turn a model path into a scene
    /// graph — and nothing above it. No GL context is created or needed: contexts are for drawing,
    /// not for loading.
    class World
    {
    public:
        World(Files::ConfigurationManager& config, const boost::program_options::variables_map& variables,
            const std::filesystem::path& resourcePath);
        ~World();

        World(const World&) = delete;
        World& operator=(const World&) = delete;

        /// Finds a cell the way Morrowind addresses one: a pair of integers is an exterior, anything
        /// else is an interior's name. Null when there is no such cell.
        const ESM::Cell* findCell(std::string_view spec) const;

        /// One object a cell places.
        struct Object
        {
            VFS::Path::Normalized mModel;
            osg::Matrixf mTransform;

            /// The `LIGH` record this reference stands for, or null. A candle is both things at
            /// once: a mesh to place and a light to cast, arriving by the same reference.
            ///
            /// Points into the loaded content, which outlives every call.
            const ESM::Light* mLight = nullptr;
        };

        /// What `forEachObject` met but could not place.
        struct SkippedObjects
        {
            /// References to a record type whose model this tool does not read — lights, creatures,
            /// items on the floor. Their geometry is real; loading it needs more of the content
            /// files than the static world does.
            std::uint32_t mUnknownType = 0;

            /// References whose record has no model at all. Markers, mostly.
            std::uint32_t mNoModel = 0;
        };

        /// Calls `handle` for every object the cell places that has a model to draw.
        SkippedObjects forEachObject(const ESM::Cell& cell, const std::function<void(const Object&)>& handle);

        /// Loads an exterior cell's terrain and returns the graph it went into. Null for an
        /// interior, and for an exterior with no land record.
        ///
        /// The graph is the same one every time and it accumulates: asking for a second cell adds
        /// its chunks beside the first's. Nothing here loads more than one, and a caller that did
        /// would want them together anyway.
        ///
        /// The game gets terrain without asking: by cull time `Terrain::QuadTreeWorld` has already
        /// put chunks in the scene graph, and the mirror picks them up like any other geometry.
        /// Headless there is no such thing, so the harness stands one up — and the renderer still
        /// does not have to know terrain exists, which is the whole argument for mirroring a graph
        /// rather than reading the content files twice.
        ///
        /// The returned node lives as long as this `World` does.
        osg::ref_ptr<osg::Group> buildTerrain(const ESM::Cell& cell);

        /// The node `buildTerrain` accumulates into, or null before the first exterior.
        ///
        /// For a caller that wants to know which chunks are new: the count before it loads and the
        /// count after bound exactly the ones it caused.
        osg::Group* getTerrainRoot() const { return mTerrainParent.get(); }

        Resource::SceneManager& getSceneManager();

        Resource::ImageManager& getImageManager();

    private:
        // Declaration order is destruction order reversed, and the managers hold references to the
        // encoder and the VFS, so those come first and go last.
        ToUTF8::Utf8Encoder mEncoder;
        Files::Collections mFileCollections;
        VFS::Manager mVfs;
        ESM::ReadersCache mReaders;

        // Built in the initialiser list: `EsmData` is move-constructible and not assignable, which is
        // the right shape for something this size and means it cannot be filled in from the body.
        EsmLoader::EsmData mEsmData;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;

        // Built on the first exterior asked for. The grid keeps the chunks it made: its destructor
        // unloads every cell and detaches its root, so it has to outlive whoever is reading them.
        //
        // Declaration order here is load-bearing, because destruction runs backwards through it.
        // The grid deregisters itself from the resource system, so it must go first; the storage
        // holds pointers into `mEsmData`, so it must go before that.
        std::unique_ptr<TerrainStorage> mTerrainStorage;
        osg::ref_ptr<osg::Group> mTerrainParent;
        osg::ref_ptr<osg::Group> mCompileRoot;
        std::unique_ptr<Terrain::TerrainGrid> mTerrain;
    };
}
