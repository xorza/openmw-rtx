#include "globalmap.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Texture>
#include <osg/Vec4>
#include <osg/ref_ptr>
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/store.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/globalmap.hpp>
#include <components/esm3/loadland.hpp>
#include <components/files/memorystream.hpp>
#include <components/misc/constants.hpp>
#include <components/myguiplatform/guirendermanager.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/imageregion.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>

#include "localmap.hpp"
#include "mapoverlay.hpp"
#include "renderer.hpp"

namespace
{
    std::vector<char> writePng(const osg::Image& overlayImage)
    {
        std::ostringstream ostream;
        osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (!readerwriter)
        {
            Log(Debug::Error) << "Error: Can't write map overlay: no png readerwriter found";
            return std::vector<char>();
        }

        osgDB::ReaderWriter::WriteResult result = readerwriter->writeImage(overlayImage, ostream);
        if (!result.success())
        {
            Log(Debug::Warning) << "Error: Can't write map overlay: " << result.message() << " code "
                                << result.status();
            return std::vector<char>();
        }

        std::string data = ostream.str();
        return std::vector<char>(data.begin(), data.end());
    }
}

namespace MWRender
{

    class CreateMapWorkItem : public SceneUtil::WorkItem
    {
    public:
        CreateMapWorkItem(std::int32_t width, std::int32_t height, std::int32_t minX, std::int32_t minY,
            std::int32_t maxX, std::int32_t maxY, std::int32_t cellSize, const MWWorld::Store<ESM::Land>& landStore,
            osg::ref_ptr<osg::Image> colorLut)
            : mWidth(width)
            , mHeight(height)
            , mMinX(minX)
            , mMinY(minY)
            , mMaxX(maxX)
            , mMaxY(maxY)
            , mCellSize(cellSize)
            , mLandStore(landStore)
            , mColorLut(std::move(std::move(colorLut)))
        {
        }

        void doWork() override
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(mWidth, mHeight, 1, GL_RGB, GL_UNSIGNED_BYTE);

            osg::ref_ptr<osg::Image> alphaImage = new osg::Image;
            alphaImage->allocateImage(mWidth, mHeight, 1, GL_ALPHA, GL_UNSIGNED_BYTE);

            for (std::int32_t x = mMinX; x <= mMaxX; ++x)
            {
                for (std::int32_t y = mMinY; y <= mMaxY; ++y)
                {
                    const ESM::Land* land = mLandStore.search(x, y);

                    for (std::int32_t cellY = 0; cellY < mCellSize; ++cellY)
                    {
                        for (std::int32_t cellX = 0; cellX < mCellSize; ++cellX)
                        {
                            std::int32_t vertexX = (cellX * 9) / mCellSize; // 0..8
                            std::int32_t vertexY = (cellY * 9) / mCellSize; // 0..8

                            std::int32_t texelX = (x - mMinX) * mCellSize + cellX;
                            std::int32_t texelY = (y - mMinY) * mCellSize + cellY;

                            std::int32_t lutIndex = 0;
                            // Converting [-128; 127] WNAM range to [0; 255] index
                            if (land != nullptr && (land->mDataTypes & ESM::Land::DATA_WNAM))
                                lutIndex = static_cast<std::int32_t>(land->mWnam[static_cast<std::size_t>(vertexY) * 9
                                               + static_cast<std::size_t>(vertexX)])
                                    + 128;

                            // Use getColor to handle all pixel format conversions automatically
                            osg::Vec4 color = mColorLut->getColor(static_cast<std::uint32_t>(lutIndex), 0);

                            // Use setColor to write to output images
                            image->setColor(
                                color, static_cast<std::uint32_t>(texelX), static_cast<std::uint32_t>(texelY));

                            // Set alpha based on lutIndex threshold
                            osg::Vec4 alpha(0.0f, 0.0f, 0.0f, lutIndex < 128 ? 0.0f : 1.0f);
                            alphaImage->setColor(
                                alpha, static_cast<std::uint32_t>(texelX), static_cast<std::uint32_t>(texelY));
                        }
                    }
                }
            }

            mBaseTexture = new osg::Texture2D;
            mBaseTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mBaseTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mBaseTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mBaseTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mBaseTexture->setImage(image);
            mBaseTexture->setResizeNonPowerOfTwoHint(false);

            mAlphaImage = alphaImage;
        }

        std::int32_t mWidth, mHeight;
        std::int32_t mMinX, mMinY, mMaxX, mMaxY;
        std::int32_t mCellSize;
        const MWWorld::Store<ESM::Land>& mLandStore;
        osg::ref_ptr<osg::Image> mColorLut;

        osg::ref_ptr<osg::Texture2D> mBaseTexture;
        osg::ref_ptr<osg::Image> mAlphaImage;
    };

    struct GlobalMap::WritePng final : public SceneUtil::WorkItem
    {
        osg::ref_ptr<const osg::Image> mOverlayImage;
        std::vector<char> mImageData;

        explicit WritePng(osg::ref_ptr<const osg::Image> overlayImage)
            : mOverlayImage(std::move(overlayImage))
        {
        }

        void doWork() override { mImageData = writePng(*mOverlayImage); }
    };

    GlobalMap::GlobalMap(Renderer& renderer, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue)
        : mRenderer(renderer)
        , mWorkQueue(workQueue)
        , mWidth(0)
        , mHeight(0)
        , mMinX(0)
        , mMaxX(0)
        , mMinY(0)
        , mMaxY(0)
    {
    }

    GlobalMap::~GlobalMap()
    {
        if (mWorkItem)
            mWorkItem->waitTillDone();
    }

    void GlobalMap::render()
    {
        const MWWorld::ESMStore& esmStore = *MWBase::Environment::get().getESMStore();

        // get the size of the world
        MWWorld::Store<ESM::Cell>::iterator it = esmStore.get<ESM::Cell>().extBegin();
        for (; it != esmStore.get<ESM::Cell>().extEnd(); ++it)
        {
            mMinX = std::min(it->getGridX(), mMinX);
            mMaxX = std::max(it->getGridX(), mMaxX);
            mMinY = std::min(it->getGridY(), mMinY);
            mMaxY = std::max(it->getGridY(), mMaxY);
        }

        const std::int32_t cellSize = Settings::map().mGlobalMapCellSize;

        mWidth = cellSize * (mMaxX - mMinX + 1);
        mHeight = cellSize * (mMaxY - mMinY + 1);

        // Load color LUT texture
        constexpr VFS::Path::NormalizedView colorLutPath("textures/omw_map_color_palette.dds");
        auto resourceSystem = MWBase::Environment::get().getResourceSystem();
        osg::ref_ptr<osg::Image> colorLut = resourceSystem->getImageManager()->getImage(colorLutPath);

        // Validate LUT dimensions
        if (!colorLut || colorLut->s() != 256 || colorLut->t() != 1)
        {
            throw std::runtime_error("Global map color LUT must be 256x1 pixels, got "
                + std::to_string(colorLut ? colorLut->s() : 0) + "x" + std::to_string(colorLut ? colorLut->t() : 0));
        }

        mWorkItem = std::make_shared<CreateMapWorkItem>(
            mWidth, mHeight, mMinX, mMinY, mMaxX, mMaxY, cellSize, esmStore.get<ESM::Land>(), colorLut);
        mWorkQueue->addWorkItem(mWorkItem);
    }

    void GlobalMap::worldPosToImageSpace(float x, float z, float& imageX, float& imageY)
    {
        imageX = ((x / static_cast<float>(Constants::CellSizeInUnits) - static_cast<float>(mMinX))
                     / static_cast<float>((mMaxX - mMinX + 1)))
            * static_cast<float>(getWidth());

        imageY = (1.f
                     - (z / static_cast<float>(Constants::CellSizeInUnits) - static_cast<float>(mMinY))
                         / static_cast<float>((mMaxY - mMinY + 1)))
            * static_cast<float>(getHeight());
    }

    void GlobalMap::exploreCell(std::int32_t cellX, std::int32_t cellY, LocalMap& localMap)
    {
        ensureLoaded();

        std::shared_ptr<OffscreenView> tile = localMap.getMapView(cellX, cellY);
        if (!tile)
            return;

        const std::int32_t cellSize = Settings::map().mGlobalMapCellSize;
        const std::int32_t originX = (cellX - mMinX) * cellSize;
        const std::int32_t originY = (cellY - mMinY) * cellSize;

        if (cellX > mMaxX || cellX < mMinX || cellY > mMaxY || cellY < mMinY)
            return;

        mOverlay->paintTile(SceneUtil::ImageRegion{ originX, originY, cellSize, cellSize }, std::move(tile));
    }

    void GlobalMap::clear()
    {
        ensureLoaded();

        mOverlay->clear();
    }

    void GlobalMap::write(ESM::GlobalMap& map)
    {
        ensureLoaded();

        map.mBounds.mMinX = mMinX;
        map.mBounds.mMaxX = mMaxX;
        map.mBounds.mMinY = mMinY;
        map.mBounds.mMaxY = mMaxY;

        if (mWritePng != nullptr)
        {
            mWritePng->waitTillDone();
            map.mImageData = std::move(mWritePng->mImageData);
            mWritePng = nullptr;
            return;
        }

        map.mImageData = writePng(mOverlay->getImage());
    }

    namespace
    {
        struct Box
        {
            std::int32_t mLeft, mTop, mRight, mBottom;

            Box(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
                : mLeft(left)
                , mTop(top)
                , mRight(right)
                , mBottom(bottom)
            {
            }
            bool operator==(const Box& other) const
            {
                return mLeft == other.mLeft && mTop == other.mTop && mRight == other.mRight && mBottom == other.mBottom;
            }
        };
    }

    void GlobalMap::read(ESM::GlobalMap& map)
    {
        ensureLoaded();

        const ESM::GlobalMap::Bounds& bounds = map.mBounds;

        if (bounds.mMaxX - bounds.mMinX < 0)
            return;
        if (bounds.mMaxY - bounds.mMinY < 0)
            return;

        if (bounds.mMinX > bounds.mMaxX || bounds.mMinY > bounds.mMaxY)
            throw std::runtime_error("invalid map bounds");

        if (map.mImageData.empty())
            return;

        Files::IMemStream istream(map.mImageData.data(), map.mImageData.size());

        osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension("png");
        if (!readerwriter)
        {
            Log(Debug::Error) << "Error: Can't read map overlay: no png readerwriter found";
            return;
        }

        osgDB::ReaderWriter::ReadResult result = readerwriter->readImage(istream);
        if (!result.success())
        {
            Log(Debug::Error) << "Error: Can't read map overlay: " << result.message() << " code " << result.status();
            return;
        }

        osg::ref_ptr<osg::Image> image = result.getImage();
        std::int32_t imageWidth = image->s();
        std::int32_t imageHeight = image->t();

        std::int32_t xLength = (bounds.mMaxX - bounds.mMinX + 1);
        std::int32_t yLength = (bounds.mMaxY - bounds.mMinY + 1);

        // Size of one cell in image space
        std::int32_t cellImageSizeSrc = imageWidth / xLength;
        if ((imageHeight / yLength) != cellImageSizeSrc)
            throw std::runtime_error("cell size must be quadratic");

        // If cell bounds of the currently loaded content and the loaded savegame do not match,
        // we need to resize source/dest boxes to accommodate
        // This means nonexisting cells will be dropped silently
        const std::int32_t cellImageSizeDst = Settings::map().mGlobalMapCellSize;

        // Completely off-screen? -> no need to blit anything
        if (bounds.mMaxX < mMinX || bounds.mMaxY < mMinY || bounds.mMinX > mMaxX || bounds.mMinY > mMaxY)
            return;

        std::int32_t leftDiff = (mMinX - bounds.mMinX);
        std::int32_t topDiff = (bounds.mMaxY - mMaxY);
        std::int32_t rightDiff = (bounds.mMaxX - mMaxX);
        std::int32_t bottomDiff = (mMinY - bounds.mMinY);

        Box srcBox(std::max(0, leftDiff * cellImageSizeSrc), std::max(0, topDiff * cellImageSizeSrc),
            std::min(imageWidth, imageWidth - rightDiff * cellImageSizeSrc),
            std::min(imageHeight, imageHeight - bottomDiff * cellImageSizeSrc));

        Box destBox(std::max(0, -leftDiff * cellImageSizeDst), std::max(0, -topDiff * cellImageSizeDst),
            std::min(mWidth, mWidth + rightDiff * cellImageSizeDst),
            std::min(mHeight, mHeight + bottomDiff * cellImageSizeDst));

        if (srcBox == destBox && imageWidth == mWidth && imageHeight == mHeight)
        {
            mOverlay->replace(std::move(image));
        }
        else
        {
            // Dimensions don't match. This could mean a changed map region, or a changed map resolution.
            // In the latter case, we'll want filtering.
            // The boxes above count rows from the top; the overlay's rectangles count them from the bottom.
            const std::int32_t srcHeight = srcBox.mBottom - srcBox.mTop;
            const std::int32_t destHeight = destBox.mBottom - destBox.mTop;
            mOverlay->paintImage(SceneUtil::ImageRegion{ destBox.mLeft, mHeight - destBox.mBottom,
                                     destBox.mRight - destBox.mLeft, destHeight },
                std::move(image),
                SceneUtil::ImageRegion{
                    srcBox.mLeft, imageHeight - srcBox.mBottom, srcBox.mRight - srcBox.mLeft, srcHeight });
        }
    }

    std::unique_ptr<MyGUI::ITexture> GlobalMap::makeBaseGuiTexture()
    {
        ensureLoaded();
        return MyGUIPlatform::shareTexture(*mBaseTexture);
    }

    MyGUI::ITexture& GlobalMap::getOverlayTexture()
    {
        ensureLoaded();
        return mOverlay->getTexture();
    }

    void GlobalMap::ensureLoaded()
    {
        if (mWorkItem)
        {
            mWorkItem->waitTillDone();

            mBaseTexture = mWorkItem->mBaseTexture;
            mOverlay = mRenderer.createMapOverlay(
                MapOverlaySpec{ .mWidth = mWidth, .mHeight = mHeight, .mLandAlpha = mWorkItem->mAlphaImage });

            mWorkItem = nullptr;
        }
    }

    void GlobalMap::asyncWritePng()
    {
        if (mOverlay == nullptr)
            return;
        // Use deep copy to avoid any sychronization
        mWritePng = std::make_shared<WritePng>(new osg::Image(mOverlay->getImage(), osg::CopyOp::DEEP_COPY_ALL));
        mWorkQueue->addWorkItem(mWritePng, /*front=*/true);
    }
}
