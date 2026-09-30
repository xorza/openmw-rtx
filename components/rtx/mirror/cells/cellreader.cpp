#include "cellreader.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <osg/Matrix>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Quat>
#include <osg/Transform>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/crashcatcher/crashnote.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/rtx/common/contract.hpp>
#include <components/rtx/common/result.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/scene/lightbuilder.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/sceneutil/visitor.hpp>

#include "cellworld.hpp"
#include "prepared.hpp"

namespace Rtx
{
    namespace
    {
        osg::Matrixf transformOf(const Terrain::PagedCellRef& ref)
        {
            const osg::Quat attitude = osg::Quat(ref.mRotation.z(), osg::Vec3f(0.0f, 0.0f, -1.0f))
                * osg::Quat(ref.mRotation.y(), osg::Vec3f(0.0f, -1.0f, 0.0f))
                * osg::Quat(ref.mRotation.x(), osg::Vec3f(-1.0f, 0.0f, 0.0f));

            osg::Matrixf transform;
            transform.preMultTranslate(ref.mPosition);
            transform.preMultRotate(attitude);
            transform.preMultScale(osg::Vec3f(ref.mScale, ref.mScale, ref.mScale));

            return transform;
        }

        /// Where `SceneUtil::addLight` attaches a lamp's light in the model at `root`: the first
        /// node named `AttachLight` its visitor meets, or the root.
        osg::Vec3f anchorIn(const osg::Node& root)
        {
            // OSG's visitor API is non-const throughout, and neither walk writes anything.
            osg::Node& walked = const_cast<osg::Node&>(root);
            SceneUtil::FindByNameVisitor find("AttachLight");
            walked.accept(find);
            if (find.mFoundNode == nullptr)
                return osg::Vec3f();

            // A template's nodes have no parents outside it: the game's clone copies every node.
            const osg::NodePathList paths = find.mFoundNode->getParentalNodePaths(&walked);
            assert(!paths.empty() && "a node the walk found that no path leads to");
            return osg::Vec3f() * osg::computeLocalToWorld(paths.front());
        }
    }

    CellReader::CellReader(const Terrain::ObjectStorage& storage, Terrain::Storage& ground, ContentSource& content,
        const ESM::RefId worldspace, const osg::Node::NodeMask mask)
        : mStorage(storage)
        , mContent(content)
        , mWorldspace(worldspace)
        , mMask(mask)
        , mGround(ground, worldspace)
        , mCollector(storage.makeCollector())
    {
    }

    PreparedTexture& CellReader::readTexture(const VFS::Path::Normalized& path)
    {
        if (const auto known = mTexturesByPath.find(path.value()); known != mTexturesByPath.end())
        {
            mTextures.lend(**known);
            return **known;
        }

        // A file that does not read keeps its place and its path, for the texture table to stand
        // in and refuse: left out, the ground under it would show another layer with nothing said.
        PreparedTexture& texture = mTextures.take([&](PreparedTexture& into) {
            const Result<osg::ref_ptr<const osg::Image>, std::string> image = mContent.getImage(path);
            into.mImage = image.isOk() ? image.value() : nullptr;
            into.mPath = path;
        });

        mTextures.lend(texture);
        [[maybe_unused]] const bool fresh = mTexturesByPath.insert(&texture).second;
        assert(fresh && "a texture filed twice");

        return texture;
    }

    PreparedModel* CellReader::readModel(const VFS::Path::NormalizedView path)
    {
        if (const auto known = mModelsByPath.find(path.value()); known != mModelsByPath.end())
            return *known;

        // The loader's parse and the walk after it, both over content a mod may have written wrong.
        const Crash::NoteScope noted("reading the model \"{}\"", path.value());
        const osg::ref_ptr<const osg::Node> node = mContent.getTemplate(path);
        if (node == nullptr)
            return nullptr;

        PreparedModel& model = mModels.take([&](PreparedModel& into) {
            into.mPath.assign(path.value());
            into.mTemplate = node;

            // The template's own bound, as `createChunk` measured a reference by it. Computed at
            // load for every template the game hands out, so this is a read.
            into.mRadius = node->getBound().radius();

            // What the walk had read before it met what it cannot take goes, and the reason stays.
            // The loader answers a file it cannot read with the error marker, which reads.
            const Result<void, std::string> walked = mWalk.read(*node, mMask, into);
            if (!walked.isOk())
            {
                into.reuse();
                into.mPath.assign(path.value());
                into.mRefused.assign(walked.error());
            }
        });

        [[maybe_unused]] const bool fresh = mModelsByPath.insert(&model).second;
        assert(fresh && "a model filed twice");

        return &model;
    }

    const VFS::Path::Normalized& CellReader::modelPathOf(const ESM::RefId& record)
    {
        auto named = mModelPaths.find(record);
        if (named == mModelPaths.end())
        {
            VFS::Path::Normalized model = mStorage.getModel(record);
            if (!model.empty())
                model = Misc::ResourceHelpers::correctMeshPath(model);
            named = mModelPaths.emplace(record, std::move(model)).first;
        }

        return named->second;
    }

    osg::Vec3f CellReader::anchorOf(const ESM::RefId& lamp)
    {
        auto anchored = mAnchors.find(lamp);
        if (anchored == mAnchors.end())
        {
            const VFS::Path::Normalized& model = modelPathOf(lamp);
            const osg::ref_ptr<const osg::Node> node = model.empty() ? nullptr : mContent.getTemplate(model);
            anchored = mAnchors.emplace(lamp, node != nullptr ? anchorIn(*node) : osg::Vec3f()).first;
        }

        return anchored->second;
    }

    PreparedCell& CellReader::read(const osg::Vec2i& cell, const bool statics)
    {
        PreparedCell& prepared = mCells.take([&](PreparedCell& into) {
            fill(into, cell, statics);
            into.mPreprocessed = mWalk.takeStats();
        });
        mCells.lend(prepared);
        return prepared;
    }

    void CellReader::fill(PreparedCell& prepared, const osg::Vec2i& cell, const bool statics)
    {
        prepared.mCell = cell;
        prepared.mStatics = statics;

        mGround.read(cell, prepared.mGround);

        // Each layer's texture counted once for the cell, so the reading stands until the frame
        // gives the cell's hold on it back.
        const std::span<const GroundReader::LayerFiles> files = mGround.getLayerFiles();
        assert(files.size() == prepared.mGround.mLayers.size() && "a layer the ground reader found no files for");
        for (std::size_t at = 0; at < files.size(); ++at)
        {
            PreparedLayer& layer = prepared.mGround.mLayers[at];
            layer.mTexture = &readTexture(files[at].mPath);
            if (files[at].mNormalPath.empty())
                continue;

            PreparedTexture& normal = readTexture(files[at].mNormalPath);
            layer.mNormalTexture = &normal;
            layer.mParallax = files[at].mParallax && normal.mImage != nullptr && carriesHeight(*normal.mImage);
        }

        // One cell at a time, which is the paging's near answer: containers page here as they do
        // in the active grid's own chunks, and the size rule is what thins them with distance. One
        // walk of the cell's records answers the lamps too, which the paging never stands, and a
        // reference is a lamp where its record is a `LIGH`: its light is carried whether or not
        // the statics are read, and its model stands as any static's does.
        mStorage.collect(1.0f, cell, mWorldspace, Terrain::RefKinds::Both, *mCollector, mRefScratch);

        for (const Terrain::PagedCellRef& ref : mRefScratch)
        {
            const bool givesLight = readLamp(ref, prepared);
            if (statics)
                readStatic(ref, givesLight, prepared);
        }
    }

    bool CellReader::readLamp(const Terrain::PagedCellRef& ref, PreparedCell& prepared)
    {
        const std::optional<SceneUtil::LightCommon> record = mStorage.getLight(ref.mRefId);
        if (!record.has_value())
            return false;

        // Made once here to be judged, and again every walk to be stood, because a flame is a
        // function of the hour and whether a lamp is refused is not. A record off by default
        // casts nothing wherever it is placed, so it is not carried.
        const osg::Vec3f position = anchorOf(ref.mRefId) * transformOf(ref);
        const Result<std::optional<Light>, std::string_view> made
            = makeLight(*record, position, 0.0, static_cast<int>(ref.mRefNum.mIndex));
        if (!made.isOk())
        {
            prepared.mRefusals.push_back(Refusal{
                .mKind = Refused::Lamp, .mName = ref.mRefId.toDebugString(), .mWhy = std::string(made.error()) });
            return false;
        }
        if (!made.value().has_value())
            return false;

        prepared.mLights.push_back(PreparedLight{
            .mPosition = position,
            .mRefNum = ref.mRefNum,
            .mGate = ref.mGate,
            .mRecord = *record,
        });
        return true;
    }

    void CellReader::readStatic(const Terrain::PagedCellRef& ref, const bool givesLight, PreparedCell& prepared)
    {
        // A reference naming no record is the content's to answer for, and the game draws
        // nothing for one either.
        if (Misc::ResourceHelpers::isHiddenMarker(ref.mRefId))
            return;

        const VFS::Path::Normalized& model = modelPathOf(ref.mRefId);
        if (model.empty())
            return;

        // A model this cannot read is a reference left out and refused, and never a cell
        // left out: a settled walk waits for every cell of the ring, and one that never came
        // would hold it for ever.
        PreparedModel* read = readModel(model);
        if (read == nullptr)
            return;

        if (!read->mRefused.empty())
        {
            prepared.mRefusals.push_back(
                Refusal{ .mKind = Refused::Model, .mName = read->mPath, .mWhy = read->mRefused });
            return;
        }

        if (read->mParts.empty())
            return;

        std::uint32_t index = 0;
        for (; index < prepared.mModels.size(); ++index)
            if (prepared.mModels[index] == read)
                break;

        // One hold for the cell, however many of its references stand the model.
        if (index == prepared.mModels.size())
        {
            prepared.mModels.push_back(read);
            mModels.lend(*read);
        }

        prepared.mRefs.push_back(PreparedRef{
            .mModel = index,
            .mRefNum = ref.mRefNum,
            .mTransform = transformOf(ref),
            .mRadius = givesLight || read->mEmits ? std::numeric_limits<float>::infinity() : read->mRadius * ref.mScale,
            .mGate = ref.mGate,
        });
    }

    void CellReader::giveBack(PreparedCell& cell)
    {
        if (!mCells.release(cell))
            return;

        cell.reuse();
        mCells.give(cell);
    }

    void CellReader::giveBack(PreparedTexture& texture)
    {
        if (!mTextures.release(texture))
            return;

        // Erased under the path it is still filed under, before `reuse` clears it.
        const auto filed = mTexturesByPath.find(texture.mPath.value());
        contract(filed != mTexturesByPath.end(), "a texture given back that was never filed");
        mTexturesByPath.erase(filed);

        texture.reuse();
        mTextures.give(texture);
    }

    ReaderMemory CellReader::measure() const
    {
        ReaderMemory measured;
        mModels.forEach([&](const PreparedModel& model, const bool spare) {
            const bool lent = model.mLent > 0;
            (spare ? measured.mSpareModels : lent ? measured.mLentModels : measured.mFiledModels) += 1;
            (spare ? measured.mSpareBytes : lent ? measured.mLentBytes : measured.mFiledBytes) += model.getRoomBytes();
        });
        return measured;
    }

    void CellReader::giveBack(PreparedModel& model)
    {
        if (!mModels.release(model))
            return;

        // Erased under the path it is still filed under, before `reuse` clears it.
        const auto filed = mModelsByPath.find(std::string_view(model.mPath));
        contract(filed != mModelsByPath.end(), "a model given back that was never filed");
        mModelsByPath.erase(filed);

        model.reuse();
        mModels.give(model);
    }
}
