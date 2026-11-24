#include "cell.hpp"

#include <cstdint>
#include <ranges>
#include <set>
#include <utility>

#include <osg/Callback>
#include <osg/Group>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osg/Referenced>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/cell.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/ref.hpp>
#include <apps/opencs/model/world/refcollection.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/model/world/variantconversion.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/terrain/terraingrid.hpp>

#include "cellarrow.hpp"
#include "cellborder.hpp"
#include "cellmarker.hpp"
#include "cellwater.hpp"
#include "instancedragmodes.hpp"
#include "mask.hpp"
#include "objectmarker.hpp"
#include "pathgrid.hpp"
#include "tagbase.hpp"
#include "terrainstorage.hpp"
#include "worldspacewidget.hpp"

namespace CSVRender
{
    namespace
    {
        class CellNodeContainer : public osg::Referenced
        {
        public:
            explicit CellNodeContainer(Cell* cell)
                : mCell(cell)
            {
            }

            Cell* getCell() { return mCell; }

        private:
            Cell* mCell;
        };
    }

    class CellNodeCallback : public osg::NodeCallback
    {
    public:
        void operator()(osg::Node* node, osg::NodeVisitor* nv) override
        {
            traverse(node, nv);
            auto* container = static_cast<CellNodeContainer*>(node->getUserData());
            container->getCell()->updateLand();
        }
    };
}

bool CSVRender::Cell::removeObject(const std::string& id)
{
    auto iter = mObjects.find(Misc::StringUtils::lowerCase(id));

    if (iter == mObjects.end())
        return false;

    removeObject(iter);
    return true;
}

std::map<std::string, CSVRender::Object*>::iterator CSVRender::Cell::removeObject(
    std::map<std::string, Object*>::iterator iter)
{
    delete iter->second;
    mObjects.erase(iter++);
    return iter;
}

bool CSVRender::Cell::addObjects(std::int32_t start, std::int32_t end)
{
    bool modified = false;

    const CSMWorld::RefCollection& collection = mData.getReferences();

    for (std::int32_t i = start; i <= end; ++i)
    {
        const auto& cellId = ESM::RefId::stringRefId(collection.getRecord(i).get().mCell.toString());

        CSMWorld::RecordBase::State state = collection.getRecord(i).mState;

        if (cellId == mId && state != CSMWorld::RecordBase::State_Deleted)
        {
            const std::string& id = collection.getRecord(i).get().mId.getRefIdString();

            auto object = std::make_unique<Object>(mData, mCellNode, id, false);

            mObjects.insert(std::make_pair(id, object.release()));
            modified = true;
        }
    }

    return modified;
}

void CSVRender::Cell::updateLand()
{
    if (!mUpdateLand || mLandDeleted)
        return;

    mUpdateLand = false;

    // Cell is deleted
    if (mDeleted)
    {
        unloadLand();
        return;
    }

    const CSMWorld::IdCollection<CSMWorld::Land>& land = mData.getLand();

    if (land.getRecord(mId).isDeleted())
        return;

    const ESM::Land& esmLand = land.getRecord(mId).get();

    if (mTerrain)
    {
        mTerrain->unloadCell(mCoordinates.getX(), mCoordinates.getY());
        mTerrain->clearAssociatedCaches();
    }
    else
    {
        constexpr double expiryDelay = 0;
        mTerrain = std::make_unique<Terrain::TerrainGrid>(mCellNode, mCellNode, mData.getResourceSystem().get(),
            mTerrainStorage, Mask_Terrain, ESM::Cell::sDefaultWorldspaceId, expiryDelay);
    }

    mTerrain->loadCell(esmLand.mX, esmLand.mY);

    if (!mCellBorder)
        mCellBorder = std::make_unique<CellBorder>(mCellNode, mCoordinates);

    mCellBorder->buildShape(esmLand);
}

void CSVRender::Cell::unloadLand()
{
    if (mTerrain)
        mTerrain->unloadCell(mCoordinates.getX(), mCoordinates.getY());

    if (mCellBorder)
        mCellBorder.reset();
}

CSVRender::Cell::Cell(CSMDoc::Document& document, ObjectMarker* selectionMarker, osg::Group* rootNode,
    const std::string& id, bool deleted, bool isExterior)
    : mSelectionMarker(selectionMarker)
    , mData(document.getData())
    , mId(ESM::RefId::stringRefId(id))
    , mDeleted(deleted)
    , mSubMode(0)
    , mSubModeElementMask(0)
    , mUpdateLand(isExterior)
    , mLandDeleted(false)
{
    std::pair<CSMWorld::CellCoordinates, bool> result = CSMWorld::CellCoordinates::fromId(id);

    mTerrainStorage = new TerrainStorage(mData);

    if (result.second)
        mCoordinates = result.first;

    mCellNode = new osg::Group;
    mCellNode->setUserData(new CellNodeContainer(this));
    mCellNode->setUpdateCallback(new CellNodeCallback);
    rootNode->addChild(mCellNode);

    setCellMarker();

    if (!mDeleted)
    {
        CSMWorld::IdTable& references = mData.getTableModel<CSMWorld::IdTable>(CSMWorld::UniversalId::Type_References);

        std::int32_t rows = references.rowCount();

        addObjects(0, rows - 1);

        if (mUpdateLand)
        {
            std::int32_t landIndex = document.getData().getLand().searchId(mId);
            if (landIndex == -1)
            {
                CSMWorld::IdTable& landTable = mData.getTableModel<CSMWorld::IdTable>(CSMWorld::UniversalId::Type_Land);
                document.getUndoStack().push(new CSMWorld::CreateCommand(landTable, mId.getRefIdString()));
            }
            updateLand();
        }

        mPathgrid = std::make_unique<Pathgrid>(mData, mCellNode, mId.getRefIdString(), mCoordinates);
        mCellWater = std::make_unique<CellWater>(mData, mCellNode, mId.getRefIdString(), mCoordinates);
    }
}

CSVRender::Cell::~Cell()
{
    for (auto& object : mObjects)
        delete object.second;

    mCellNode->getParent(0)->removeChild(mCellNode);
}

CSVRender::Pathgrid* CSVRender::Cell::getPathgrid() const
{
    return mPathgrid.get();
}

bool CSVRender::Cell::referenceableDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    bool modified = false;

    for (auto& object : mObjects)
        if (object.second->referenceableDataChanged(topLeft, bottomRight))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceableAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    if (parent.isValid())
        return false;

    bool modified = false;

    for (auto& object : mObjects)
        if (object.second->referenceableAboutToBeRemoved(parent, start, end))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    if (mDeleted)
        return false;

    CSMWorld::IdTable& references = mData.getTableModel<CSMWorld::IdTable>(CSMWorld::UniversalId::Type_References);

    std::int32_t idColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Id);
    std::int32_t cellColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Cell);
    std::int32_t stateColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Modification);

    // list IDs in cell
    std::map<std::string, bool> ids; // id, deleted state

    for (std::int32_t i = topLeft.row(); i <= bottomRight.row(); ++i)
    {
        auto cell = CSMWorld::getRefId(references.getData(i, cellColumn));

        if (cell == mId)
        {
            std::string id = Misc::StringUtils::lowerCase(CSMWorld::getString(references.getData(i, idColumn)));

            std::int32_t state = references.getData(i, stateColumn).toInt();

            ids.insert(std::make_pair(id, state == CSMWorld::RecordBase::State_Deleted));
        }
    }

    // perform update and remove where needed
    bool modified = false;

    auto iter = mObjects.begin();
    while (iter != mObjects.end())
    {
        if (iter->second->referenceDataChanged(topLeft, bottomRight))
            modified = true;

        auto iter2 = ids.find(iter->first);

        if (iter2 != ids.end())
        {
            bool deleted = iter2->second;
            ids.erase(iter2);

            if (deleted)
            {
                iter = removeObject(iter);
                modified = true;
                continue;
            }
        }

        ++iter;
    }

    // add new objects
    for (auto& id : ids)
    {
        if (!id.second)
        {
            mObjects.insert(std::make_pair(id.first, new Object(mData, mCellNode, id.first, false)));

            modified = true;
        }
    }

    return modified;
}

bool CSVRender::Cell::referenceAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    if (parent.isValid())
        return false;

    if (mDeleted)
        return false;

    CSMWorld::IdTable& references = mData.getTableModel<CSMWorld::IdTable>(CSMWorld::UniversalId::Type_References);

    std::int32_t idColumn = references.findColumnIndex(CSMWorld::Columns::ColumnId_Id);

    bool modified = false;

    for (std::int32_t row = start; row <= end; ++row)
        if (removeObject(CSMWorld::getString(references.getData(row, idColumn))))
            modified = true;

    return modified;
}

bool CSVRender::Cell::referenceAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    if (parent.isValid())
        return false;

    if (mDeleted)
        return false;

    return addObjects(start, end);
}

void CSVRender::Cell::setAlteredHeight(std::int32_t inCellX, std::int32_t inCellY, float height)
{
    mTerrainStorage->setAlteredHeight(inCellX, inCellY, height);
    mUpdateLand = true;
}

float CSVRender::Cell::getSumOfAlteredAndTrueHeight(
    std::int32_t cellX, std::int32_t cellY, std::int32_t inCellX, std::int32_t inCellY)
{
    return mTerrainStorage->getSumOfAlteredAndTrueHeight(cellX, cellY, inCellX, inCellY);
}

float* CSVRender::Cell::getAlteredHeight(std::int32_t inCellX, std::int32_t inCellY)
{
    return mTerrainStorage->getAlteredHeight(inCellX, inCellY);
}

void CSVRender::Cell::resetAlteredHeights()
{
    mTerrainStorage->resetHeights();
    mUpdateLand = true;
}

void CSVRender::Cell::pathgridModified()
{
    if (mPathgrid)
        mPathgrid->recreateGeometry();
}

void CSVRender::Cell::pathgridRemoved()
{
    if (mPathgrid)
        mPathgrid->removeGeometry();
}

void CSVRender::Cell::landDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    mLandDeleted = true;
    unloadLand();
}

void CSVRender::Cell::landAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    mUpdateLand = true;
    mLandDeleted = false;
}

void CSVRender::Cell::landTextureChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landTextureAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    mUpdateLand = true;
}

void CSVRender::Cell::landTextureAdded(const QModelIndex& parent, std::int32_t start, std::int32_t end)
{
    mUpdateLand = true;
}

void CSVRender::Cell::reloadAssets()
{
    for (Object* object : mObjects | std::views::values)
    {
        object->reloadAssets();
    }

    if (mTerrain)
    {
        mTerrain->unloadCell(mCoordinates.getX(), mCoordinates.getY());
        mTerrain->loadCell(mCoordinates.getX(), mCoordinates.getY());
    }

    if (mCellWater)
        mCellWater->reloadAssets();
}

void CSVRender::Cell::setSelection(std::int32_t elementMask, Selection mode)
{
    if (static_cast<std::uint32_t>(elementMask) & Mask_Reference)
    {
        for (Object* object : mObjects | std::views::values)
        {
            bool selected = false;

            switch (mode)
            {
                case Selection_Clear:
                    selected = false;
                    break;
                case Selection_All:
                    selected = true;
                    break;
                case Selection_Invert:
                    selected = !object->getSelected();
                    break;
            }

            object->setSelected(selected);
            if (selected)
                mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
        }
        mSelectionMarker->updateSelectionMarker();
    }
    if (mPathgrid && static_cast<std::uint32_t>(elementMask) & Mask_Pathgrid)
    {
        // Only one pathgrid may be selected, so some operations will only have an effect
        // if the pathgrid is already focused
        switch (mode)
        {
            case Selection_Clear:
                mPathgrid->clearSelected();
                break;

            case Selection_All:
                if (mPathgrid->isSelected())
                    mPathgrid->selectAll();
                break;

            case Selection_Invert:
                if (mPathgrid->isSelected())
                    mPathgrid->invertSelected();
                break;
        }
    }
}

void CSVRender::Cell::selectAllWithSameParentId(std::int32_t elementMask)
{
    std::set<std::string> ids;

    for (Object* object : mObjects | std::views::values)
    {
        if (object->getSelected())
            ids.insert(object->getReferenceableId());
    }

    for (Object* object : mObjects | std::views::values)
    {
        if (!object->getSelected() && ids.contains(object->getReferenceableId()))
        {
            object->setSelected(true);
            mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
        }
    }
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::handleSelectDrag(Object* object, DragMode dragMode)
{
    if (dragMode == DragMode_Select_Only || dragMode == DragMode_Select_Add)
        object->setSelected(true);

    else if (dragMode == DragMode_Select_Remove)
        object->setSelected(false);

    else if (dragMode == DragMode_Select_Invert)
        object->setSelected(!object->getSelected());

    if (object->getSelected())
        mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
}

void CSVRender::Cell::selectInsideCube(const osg::Vec3d& pointA, const osg::Vec3d& pointB, DragMode dragMode)
{
    for (auto& object : mObjects)
    {
        if (dragMode == DragMode_Select_Only)
            object.second->setSelected(false);

        const osg::Vec3d position(object.second->getPosition().asVec3());
        if ((position[0] > pointA[0] && position[0] < pointB[0])
            || (position[0] > pointB[0] && position[0] < pointA[0]))
        {
            if ((position[1] > pointA[1] && position[1] < pointB[1])
                || (position[1] > pointB[1] && position[1] < pointA[1]))
            {
                if ((position[2] > pointA[2] && position[2] < pointB[2])
                    || (position[2] > pointB[2] && position[2] < pointA[2]))
                    handleSelectDrag(object.second, dragMode);
            }
        }
    }

    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::selectWithinDistance(const osg::Vec3d& point, float distance, DragMode dragMode)
{
    for (auto& object : mObjects)
    {
        if (dragMode == DragMode_Select_Only)
            object.second->setSelected(false);

        float distanceFromObject = static_cast<float>((point - object.second->getPosition().asVec3()).length());
        if (distanceFromObject < distance)
            handleSelectDrag(object.second, dragMode);
    }

    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::setCellArrows(std::int32_t mask)
{
    for (std::int32_t i = 0; i < 4; ++i)
    {
        auto direction = static_cast<CellArrow::Direction>(1 << i);

        bool enable = mask & direction;

        if (enable != (mCellArrows[i].get() != nullptr))
        {
            if (enable)
                mCellArrows[i] = std::make_unique<CellArrow>(mCellNode, direction, mCoordinates);
            else
                mCellArrows[i].reset(nullptr);
        }
    }
}

void CSVRender::Cell::setCellMarker()
{
    bool cellExists = false;
    bool isInteriorCell = false;

    std::int32_t cellIndex = mData.getCells().searchId(mId);
    if (cellIndex > -1)
    {
        const CSMWorld::Record<CSMWorld::Cell>& cellRecord = mData.getCells().getRecord(cellIndex);
        cellExists = !cellRecord.isDeleted();
        isInteriorCell = cellRecord.get().mData.mFlags & ESM::Cell::Interior;
    }

    if (!isInteriorCell)
    {
        mCellMarker = std::make_unique<CellMarker>(mCellNode, mCoordinates, cellExists);
    }
}

CSMWorld::CellCoordinates CSVRender::Cell::getCoordinates() const
{
    return mCoordinates;
}

bool CSVRender::Cell::isDeleted() const
{
    return mDeleted;
}

osg::ref_ptr<CSVRender::TagBase> CSVRender::Cell::getSnapTarget(std::uint32_t elementMask) const
{
    osg::ref_ptr<TagBase> result;

    if (elementMask & Mask_Reference)
        for (auto& obj : mObjects)
            if (obj.second->getSnapTarget())
                return obj.second->getTag();

    return result;
}

void CSVRender::Cell::selectFromGroup(const std::vector<std::string>& group)
{
    for (const auto& [_, object] : mObjects)
    {
        for (const auto& objectName : group)
        {
            if (objectName == object->getReferenceId())
            {
                object->setSelected(true, osg::Vec4f(1, 0, 1, 1));
                mSelectionMarker->addToSelectionHistory(object->getReferenceId(), false);
            }
        }
    }
    mSelectionMarker->updateSelectionMarker();
}

void CSVRender::Cell::unhideAll()
{
    for (const auto& [_, object] : mObjects)
    {
        osg::ref_ptr<osg::Group> rootNode = object->getRootNode();
        if (rootNode->getNodeMask() == Mask_Hidden)
            rootNode->setNodeMask(Mask_Reference);
    }
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::Cell::getSelection(std::uint32_t elementMask) const
{
    std::vector<osg::ref_ptr<TagBase>> result;

    if (elementMask & Mask_Reference)
        for (const auto& object : mObjects)
            if (object.second->getSelected())
                result.push_back(object.second->getTag());
    if (mPathgrid && elementMask & Mask_Pathgrid)
        if (mPathgrid->isSelected())
            result.emplace_back(mPathgrid->getTag());

    return result;
}

std::vector<osg::ref_ptr<CSVRender::TagBase>> CSVRender::Cell::getEdited(std::uint32_t elementMask) const
{
    std::vector<osg::ref_ptr<TagBase>> result;

    if (elementMask & Mask_Reference)
        for (const auto& object : mObjects)
            if (object.second->isEdited())
                result.push_back(object.second->getTag());

    return result;
}

void CSVRender::Cell::setSubMode(std::int32_t subMode, std::uint32_t elementMask)
{
    mSubMode = subMode;
    mSubModeElementMask = elementMask;

    if (elementMask & Mask_Reference)
        mSelectionMarker->setSubMode(subMode);
}

void CSVRender::Cell::reset(std::uint32_t elementMask)
{
    if (elementMask & Mask_Reference)
        for (Object* object : mObjects | std::views::values)
            object->reset();
    if (mPathgrid && elementMask & Mask_Pathgrid)
        mPathgrid->resetIndicators();
}

CSVRender::Object* CSVRender::Cell::getObjectByReferenceId(const std::string& referenceId)
{
    if (auto iter = mObjects.find(Misc::StringUtils::lowerCase(referenceId)); iter != mObjects.end())
        return iter->second;
    else
        return nullptr;
}
