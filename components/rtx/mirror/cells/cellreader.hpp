#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <osg/Node>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/esm/refid.hpp>
#include <components/rtx/common/scratch.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/terrain/objectstorage.hpp>
#include <components/terrain/pagedcellref.hpp>
#include <components/terrain/storage.hpp>
#include <components/vfs/pathutil.hpp>

#include "groundreader.hpp"
#include "prepared.hpp"
#include "readermemory.hpp"
#include "templatewalk.hpp"

namespace Rtx
{
    class ContentSource;

    /// Reads one cell — its ground and its paged references — into a `PreparedCell`, on whichever
    /// thread owns this: what `Terrain::ObjectPaging::createChunk` reads through
    /// `Terrain::ObjectStorage`, and none of what it merges. A model is read once and lent to
    /// every cell that names it, and so is an image; a lend is a cell's and not the frame's,
    /// because the frame cannot know what this has lent since it last looked, and `CellHolds`
    /// counts what the frame holds beside it. Not thread-safe, and one instance a thread.
    class CellReader
    {
    public:
        /// @param mask which nodes the walk of a template may descend into — the frame walk's own
        ///        traversal mask, so the two reach the same drawables.
        CellReader(const Terrain::ObjectStorage& storage, Terrain::Storage& ground, ContentSource& content,
            ESM::RefId worldspace, osg::Node::NodeMask mask);

        /// Reads the cell at `cell`: its ground, the lights of its `LIGH` references, and — where
        /// `statics` — every reference that names a model with something to trace, a lamp's
        /// included, as one `PreparedRef` each. The cell is lent, and `giveBack` is where it returns; every model and
        /// every ground texture it names is lent to it as well, one hold each, and those come back
        /// on their own.
        PreparedCell& read(const osg::Vec2i& cell, bool statics);

        /// Takes a cell back, once the frame has copied what it wanted of it.
        void giveBack(PreparedCell& cell);

        /// Takes back one cell's hold on a model. The model goes where it was the last, and its
        /// images with it where nothing else names them.
        void giveBack(PreparedModel& model);

        /// Takes back one cell's hold on an image its ground named. The image goes where it was the
        /// last.
        void giveBack(PreparedTexture& texture);

        /// What the models this made keep, lent and spare. Walks every one, so it is asked once a
        /// cell read, on the reader's own thread.
        ReaderMemory measure() const;

    private:
        /// Reads the cell into `prepared`, which `read` has taken and lends after: the whole of a
        /// read but the pool's part.
        void fill(PreparedCell& prepared, const osg::Vec2i& cell, bool statics);

        /// Carries the light of `ref` where its record is a lamp's that casts, and says whether it
        /// did: a lamp is its own light's brightest surface, and a point of light long after it is
        /// a pixel.
        bool readLamp(const Terrain::PagedCellRef& ref, PreparedCell& prepared);

        /// Appends `ref` as a `PreparedRef` where it names a model with something to trace, out of
        /// the size rule's reach where `givesLight` or the model emits.
        void readStatic(const Terrain::PagedCellRef& ref, bool givesLight, PreparedCell& prepared);

        /// The model path `record` names, as `readModel` files it — empty where it names none.
        const VFS::Path::Normalized& modelPathOf(const ESM::RefId& record);

        /// Where the light of a lamp record stands in its reference's own space — `anchorIn`.
        osg::Vec3f anchorOf(const ESM::RefId& lamp);

        /// The model at `path`, read whole where this holds none under that path. Null where
        /// nothing stands for the path. Refused, with no part and `PreparedModel::mRefused` saying
        /// why, where the template describes a mesh this renderer cannot take.
        PreparedModel* readModel(VFS::Path::NormalizedView path);

        /// The reading of a layer's texture at `path`, made where this holds none under that path,
        /// and one more holder counted on it: its diffuse, and its normal map where it has one. The
        /// file is opened here and only on a miss, so a layer every cell of a band wears is opened
        /// once for as long as any of them holds it.
        PreparedTexture& readTexture(const VFS::Path::Normalized& path);

        const Terrain::ObjectStorage& mStorage;
        ContentSource& mContent;
        ESM::RefId mWorldspace;
        osg::Node::NodeMask mMask;

        GroundReader mGround;
        TemplateWalk mWalk;

        /// What the storage reads this reader's cells with, kept from one cell to the next.
        std::unique_ptr<Terrain::RefCollector> mCollector;

        // Refilled per cell, per model and per image.
        std::vector<Terrain::PagedCellRef> mRefScratch;

        Spares<PreparedCell> mCells;
        Spares<PreparedModel> mModels;
        Spares<PreparedTexture> mTextures;

        /// What the two tables are ordered by: the path each is filed under.
        struct PathOf
        {
            std::string_view operator()(const PreparedModel* held) const { return held->mPath; }
            std::string_view operator()(const PreparedTexture* held) const { return held->mPath.value(); }
        };

        /// Every model and every ground texture lent, sorted by path — searched rather than keyed,
        /// because a lookup then costs no node and no string. By path and not by image, because a
        /// texture that does not read has no image and still stands, as the stand-in.
        boost::container::flat_set<PreparedModel*, KeyedLess<std::string_view, PathOf>, std::vector<PreparedModel*>>
            mModelsByPath;
        boost::container::flat_set<PreparedTexture*, KeyedLess<std::string_view, PathOf>, std::vector<PreparedTexture*>>
            mTexturesByPath;

        /// The model path each record names, as `readModel` files it — empty where the record
        /// names none. Built the first time a reference to the record is met and kept for the
        /// reader's life, because a record's model does not change and building the path is two
        /// strings, which every static reference of every cell read would otherwise pay.
        boost::container::flat_map<ESM::RefId, VFS::Path::Normalized> mModelPaths;

        /// Where each lamp record's light stands, found the first time a reference to it is met
        /// and kept for the same reason: apart from the model's reading, because the light is
        /// carried whether or not the statics are read.
        boost::container::flat_map<ESM::RefId, osg::Vec3f> mAnchors;
    };
}
