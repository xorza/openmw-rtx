#include "cellwater.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/PositionAttitudeTransform>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Texture>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/collection.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/esm3/loadland.hpp>
#include <components/fallback/fallback.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/waterutil.hpp>

#include "mask.hpp"

namespace CSVRender
{
    const std::int32_t CellWater::CellSize = ESM::Land::REAL_SIZE;

    CellWater::CellWater(
        CSMWorld::Data& data, osg::Group* cellNode, std::string id, const CSMWorld::CellCoordinates& cellCoords)
        : mData(data)
        , mId(std::move(id))
        , mParentNode(cellNode)
        , mWaterTransform(nullptr)
        , mWaterGroup(nullptr)
        , mWaterGeometry(nullptr)
        , mDeleted(false)
        , mExterior(false)
        , mHasWater(false)
    {
        mWaterTransform = new osg::PositionAttitudeTransform();
        mWaterTransform->setPosition(osg::Vec3f(static_cast<float>(cellCoords.getX() * CellSize) + CellSize / 2.f,
            static_cast<float>(cellCoords.getY() * CellSize) + CellSize / 2.f, 0));

        mWaterTransform->setNodeMask(Mask_Water);
        mParentNode->addChild(mWaterTransform);

        mWaterGroup = new osg::Group();
        mWaterTransform->addChild(mWaterGroup);

        const std::int32_t cellIndex = mData.getCells().searchId(ESM::RefId::stringRefId(mId));
        if (cellIndex > -1)
        {
            updateCellData(mData.getCells().getRecord(cellIndex));
        }

        // Keep water existence/height up to date
        QAbstractItemModel* cells = mData.getTableModel(CSMWorld::UniversalId::Type_Cells);
        connect(cells, &QAbstractItemModel::dataChanged, this, &CellWater::cellDataChanged);
    }

    CellWater::~CellWater()
    {
        mParentNode->removeChild(mWaterTransform);
    }

    void CellWater::updateCellData(const CSMWorld::Record<CSMWorld::Cell>& cellRecord)
    {
        mDeleted = cellRecord.isDeleted();
        if (!mDeleted)
        {
            const CSMWorld::Cell& cell = cellRecord.get();

            if (mExterior != cell.isExterior() || mHasWater != cell.hasWater())
            {
                mExterior = cellRecord.get().isExterior();
                mHasWater = cellRecord.get().hasWater();

                recreate();
            }

            float waterHeight = -1;
            if (!mExterior)
            {
                waterHeight = cellRecord.get().mWater;
            }

            osg::Vec3d pos = mWaterTransform->getPosition();
            pos.z() = waterHeight;
            mWaterTransform->setPosition(pos);
        }
        else
        {
            recreate();
        }
    }

    void CellWater::reloadAssets()
    {
        recreate();
    }

    void CellWater::cellDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
    {
        const CSMWorld::Collection<CSMWorld::Cell>& cells = mData.getCells();

        std::int32_t rowStart = -1;
        std::int32_t rowEnd = -1;

        if (topLeft.parent().isValid())
        {
            rowStart = topLeft.parent().row();
            rowEnd = bottomRight.parent().row();
        }
        else
        {
            rowStart = topLeft.row();
            rowEnd = bottomRight.row();
        }

        for (std::int32_t row = rowStart; row <= rowEnd; ++row)
        {
            const CSMWorld::Record<CSMWorld::Cell>& cellRecord = cells.getRecord(row);

            if (cellRecord.get().mId == ESM::RefId::stringRefId(mId))
                updateCellData(cellRecord);
        }
    }

    void CellWater::recreate()
    {
        const std::int32_t interiorScalar = 20;
        const std::int32_t segmentsPerCell = 1;
        const std::int32_t textureRepeatsPerCell = 6;

        const float alpha = 0.5f;

        const std::int32_t renderBin = osg::StateSet::TRANSPARENT_BIN - 1;

        if (mWaterGeometry)
        {
            mWaterGroup->removeChild(mWaterGeometry);
            mWaterGeometry = nullptr;
        }

        if (mDeleted || !mHasWater)
            return;

        float size = NAN;
        std::int32_t segments = 0;
        float textureRepeats = NAN;

        if (mExterior)
        {
            size = CellSize;
            segments = segmentsPerCell;
            textureRepeats = textureRepeatsPerCell;
        }
        else
        {
            size = CellSize * interiorScalar;
            segments = segmentsPerCell * interiorScalar;
            textureRepeats = textureRepeatsPerCell * interiorScalar;
        }

        mWaterGeometry = SceneUtil::createWaterGeometry(size, segments, textureRepeats);
        mWaterGeometry->setStateSet(SceneUtil::createSimpleWaterStateSet(alpha, renderBin));

        // Add water texture
        constexpr VFS::Path::NormalizedView prefix("textures/water");
        VFS::Path::Normalized texturePath(prefix);
        texturePath /= std::string(Fallback::Map::getString("Water_SurfaceTexture")) + "00.dds";

        Resource::ImageManager* imageManager = mData.getResourceSystem()->getImageManager();

        osg::ref_ptr<osg::Texture2D> waterTexture = new osg::Texture2D();
        waterTexture->setImage(imageManager->getImage(texturePath));
        waterTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        waterTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);

        mWaterGeometry->getStateSet()->setTextureAttribute(0, waterTexture, osg::StateAttribute::ON);

        mData.getResourceSystem()->getSceneManager()->recreateShaders(mWaterGeometry);

        mWaterGroup->addChild(mWaterGeometry);
    }
}
