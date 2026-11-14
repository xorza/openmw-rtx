#include "stopwriter.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Image>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/characterpreview.hpp>
#include <apps/openmw/mwrender/offscreenview.hpp>
#include <apps/openmw/mwrender/renderer.hpp>
#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>
#include <apps/openmw/mwrender/rtx/tracedview.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/rtxtool/instruments/contactsheet.hpp>
#include <apps/rtxtool/instruments/png.hpp>
#include <apps/rtxtool/instruments/scenereport.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/pictureshelf.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/scenetextures.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/placementtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/scene/texturetable.hpp>

namespace RtxTool
{
    namespace
    {
        /// Draws every picture asked for since the frame, and waits for it: a stop stands after the frame and a
        /// picture is drawn inside the next, so this is a drain a stop may pay and a frame may not.
        void drawPicturesNow(const MWRender::FrameContext& context)
        {
            context.mRenderer.drawViews();
            context.mBackend.finishGuiTraces();
        }

        template <class Sample>
        bool writeAndKeep(RunRecord& record, PictureShelf* const shelf, const std::filesystem::path& file,
            const std::uint32_t width, const std::uint32_t height, const std::span<const Sample> samples)
        {
            if (const Result<void, std::string> written = writePng(file, width, height, samples); !written.isOk())
            {
                record.note(written.error() + "\n");
                record.fail();
                return false;
            }

            if (shelf != nullptr)
                shelf->keep(file, width, height, samples);
            return true;
        }
    }

    bool StopWriter::writePicture(const Writing& into, const std::filesystem::path& file, const std::uint32_t width,
        const std::uint32_t height, const std::span<const std::uint8_t> pixels)
    {
        return writeAndKeep(into.mRecord, into.mShelf, file, width, height, pixels);
    }

    bool StopWriter::writePicture(const Writing& into, const std::filesystem::path& file, const std::uint32_t width,
        const std::uint32_t height, const std::span<const std::uint16_t> samples)
    {
        return writeAndKeep(into.mRecord, into.mShelf, file, width, height, samples);
    }

    void StopWriter::write(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
        const Actions& actions, const StopFacts& facts, RunRecord& record, PictureShelf* const shelf)
    {
        const Writing into{ context, report, record, shelf };

        if (actions.mCapture.has_value())
            writeCapture(into, *actions.mCapture);

        if (actions.mMean.has_value())
            addToMean(into, *actions.mMean);

        if (!actions.mFrameTimes.empty())
            writeFrameTimes(into, actions.mFrameTimes, facts.mSamples);

        if (actions.mDigest)
            into.mRecord.note(describeScene(context.mMirror.getScene(), report.mWalked));

        if (!actions.mSheet.empty())
            writeSheet(into, actions.mSheet);

        if (!actions.mMapTile.empty())
            writeMapTile(into, actions.mMapTile);

        if (actions.mDoll.has_value())
            writeDoll(into, actions.mDoll->mWho, actions.mDoll->mFile);

        if (!actions.mFind.empty())
            reportFound(into, actions.mFind);

        if (!actions.mChecks.empty())
            askChecks(context, report, actions.mChecks, facts, record);
    }

    void StopWriter::addToMean(const Writing& into, const Actions::Mean& mean)
    {
        assert(mean.mOf > 0 && "a mean of no frames");
        if (mean.mFile != mMeanFile)
        {
            assert(mMean.getCount() == 0 && "a mean left behind before it was written");
            mMean.clear();
            mMeanFile = mean.mFile;
        }

        Rtx::Renderer& renderer = into.mContext.mBackend;
        const Rtx::FrameExtents extents = renderer.getExtents();
        renderer.readPixels(mPixels);
        mMean.add(mPixels, extents.mOutputWidth, extents.mOutputHeight);
        if (mMean.getCount() < mean.mOf)
            return;

        mMean.mean(mSamples);
        mMean.clear();
        mMeanFile.clear();
        if (!writePicture(into, mean.mFile, extents.mOutputWidth, extents.mOutputHeight, mSamples))
            return;

        into.mRecord.note(std::format("wrote {} {}x{}, the mean of {} frames\n", Files::pathToUnicodeString(mean.mFile),
            extents.mOutputWidth, extents.mOutputHeight, mean.mOf));
    }

    void StopWriter::writeCapture(const Writing& into, const Actions::Capture& capture)
    {
        const std::filesystem::path& file = capture.mFile;
        Rtx::Renderer& renderer = into.mContext.mBackend;
        const Rtx::FrameExtents extents = renderer.getExtents();

        const auto write = [&] {
            if (capture.mDepth == Actions::Capture::Depth::Shown)
            {
                renderer.readPixels(mPixels);
                return writePicture(into, file, extents.mOutputWidth, extents.mOutputHeight, mPixels);
            }
            renderer.readDeepPixels(mSamples);
            return writePicture(into, file, extents.mOutputWidth, extents.mOutputHeight, mSamples);
        };
        if (!write())
            return;

        into.mRecord.note(std::format(
            "wrote {} {}x{}", Files::pathToUnicodeString(file), extents.mOutputWidth, extents.mOutputHeight));

        if (extents.mRenderWidth != extents.mOutputWidth || extents.mRenderHeight != extents.mOutputHeight)
            into.mRecord.note(std::format(", traced at {}x{}", extents.mRenderWidth, extents.mRenderHeight));

        into.mRecord.note("\n");
    }

    void StopWriter::writeFrameTimes(
        const Writing& into, const std::filesystem::path& file, const FrameSamples& samples)
    {
        const Result<void, std::string> written = RtxTool::writeFrameTimes(file, samples);
        if (!written.isOk())
        {
            into.mRecord.note(written.error() + "\n");
            into.mRecord.fail();
            return;
        }

        into.mRecord.note(std::format("wrote {} frames to {}\n", samples.size(), Files::pathToUnicodeString(file)));
    }

    void StopWriter::writeSheet(const Writing& into, const std::filesystem::path& sheet)
    {
        const Rtx::SceneDesc& scene = into.mContext.mMirror.getScene();

        Rtx::SceneTextures described;
        described.describeAll(scene, nullptr, false);

        const ContactSheet drawn = drawContactSheet(described.getDescriptions());
        if (drawn.mCount == 0)
        {
            into.mRecord.note("the world uses no textures\n");
            into.mRecord.fail();
            return;
        }

        if (!writePicture(into, sheet, drawn.mWidth, drawn.mHeight, drawn.mPixels))
            return;

        // Left to right, top to bottom, the way it was drawn.
        std::vector<std::string> names;
        listSheetNames(described.getDescriptions(), scene.textures().getRows(), names);
        for (std::size_t at = 0; at < names.size(); ++at)
            into.mRecord.note(std::format("  {}  {}\n", at, names[at]));

        into.mRecord.note(std::format("wrote {}, {} textures\n", Files::pathToUnicodeString(sheet), drawn.mCount));
    }

    void StopWriter::writeView(const Writing& into, MWRender::OffscreenView& view, const std::filesystem::path& file)
    {
        view.keepCopy();
        view.redraw();
        drawPicturesNow(into.mContext);

        const osg::Image* drawn = view.getCopy();
        if (drawn == nullptr)
        {
            into.mRecord.note("the picture was not drawn\n");
            into.mRecord.fail();
            return;
        }

        writeImage(into, *drawn, file);
    }

    void StopWriter::writeImage(const Writing& into, const osg::Image& drawn, const std::filesystem::path& file)
    {
        const int width = drawn.s();
        const int height = drawn.t();
        const auto stride = static_cast<std::size_t>(width) * 4;
        mPixels.resize(stride * static_cast<std::size_t>(height));

        for (int row = 0; row < height; ++row)
            std::memcpy(
                mPixels.data() + stride * static_cast<std::size_t>(row), drawn.data(0, height - 1 - row), stride);

        if (!writePicture(into, file, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), mPixels))
            return;

        into.mRecord.note(std::format("wrote {} {}x{}\n", Files::pathToUnicodeString(file), width, height));
    }

    void StopWriter::writeMapTile(const Writing& into, const std::filesystem::path& file)
    {
        // The game's own tile, and not a picture framed here to look like one: the local map drew the cell the
        // player stands in when they entered it, at the resolution and over the depth range the settings gave it, and
        // what a stop writes is that picture, found by the renderer that drew it as the one taken straight down over
        // where the player stands.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        const osg::Vec3f standing = player.getRefData().getPosition().asVec3();
        MWRender::TracedView* tile
            = into.mContext.mRenderer.getViews().findWorldView(osg::Vec2f(standing.x(), standing.y()));

        const osg::Image* drawn = nullptr;
        if (tile != nullptr)
        {
            // The first ask starts the copy; asked again after the drain, it is there.
            tile->keepCopy();
            drawn = tile->getCopy();
            if (drawn == nullptr)
            {
                drawPicturesNow(into.mContext);
                drawn = tile->getCopy();
            }
        }

        if (drawn == nullptr)
        {
            into.mRecord.note("no map tile is drawn for the cell the stop stands in\n");
            into.mRecord.fail();
            return;
        }

        writeImage(into, *drawn, file);
    }

    void StopWriter::writeDoll(const Writing& into, const std::string& who, const std::filesystem::path& file)
    {
        MWBase::World& world = *MWBase::Environment::get().getWorld();
        const ESM::RefId id = ESM::RefId::stringRefId(who);

        // Stood in the world and not assembled beside it: `MWRender::NpcAnimation` dresses a body out of the parts a
        // race calls for, equips what the record carries and finds the bone a weapon hangs on, and needs a live
        // reference to do any of it. Looked up first, as a weapon is (`Staging::arm`): `ManualRef` throws for an id
        // with no record, and an id of a creature or an item throws from the animation, both out of the frame and
        // past the run's report.
        if (MWBase::Environment::get().getESMStore()->get<ESM::NPC>().search(id) == nullptr)
        {
            into.mRecord.note(std::format("no NPC record is called \"{}\"\n", who));
            into.mRecord.fail();
            return;
        }

        const MWWorld::Ptr player = world.getPlayerPtr();
        MWWorld::ManualRef ref(*MWBase::Environment::get().getESMStore(), id, 1);
        const MWWorld::Ptr subject
            = world.placeObject(ref.getPtr(), player.getCell(), player.getRefData().getPosition());

        {
            MWRender::InventoryPreview preview(into.mContext.mRenderer, &into.mContext.mResources, subject);
            preview.rebuild();

            // Through the view and not through the texture the GUI draws from, which is the one route that carries
            // the row order: `OffscreenView::getTexture` is Y-up and a PNG is not.
            writeView(into, preview.getView(), file);
        }

        // The subject was a prop for one picture; the stops after this one stand in a cell without it.
        world.deleteObject(subject);
    }

    void StopWriter::reportFound(const Writing& into, const std::string& needle)
    {
        const Rtx::SceneDesc& scene = into.mContext.mMirror.getScene();
        const std::span<const Rtx::TextureRow> rows = scene.textures().getRows();

        // **Found by texture and reported by placement**, because a mesh carries no name of its own
        // once it is a run of triangles: what a walk keeps is the material it arrived wearing, and a
        // material names the file it samples.
        std::uint32_t met = 0;
        scene.placements().forEachPlaced([&](Rtx::Index, const Rtx::PlacementRow& row) {
            const Rtx::MeshInstance& instance = row.mInstance;
            if (instance.mMaterial == Rtx::sNoIndex)
                return;

            const Rtx::Material& material = scene.materials().getRows()[instance.mMaterial];
            if (material.mDiffuse == Rtx::sNoIndex)
                return;

            const std::string_view path = rows[material.mDiffuse].mPath.value();
            if (path.find(needle) == std::string_view::npos)
                return;

            const osg::Vec3f at = instance.mTransform.getTrans();
            into.mRecord.note(std::format("  {:.0f}, {:.0f}, {:.0f}   {}\n", at.x(), at.y(), at.z(), path));
            ++met;
        });

        into.mRecord.note(std::format("{} placements wear a texture matching \"{}\"\n", met, needle));
    }
}
