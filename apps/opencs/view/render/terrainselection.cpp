#include "terrainselection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <osg/GL>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Vec3f>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/cellcoordinates.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <components/esm3/loadland.hpp>

#include "cell.hpp"
#include "worldspacewidget.hpp"

namespace CSMWorld
{
    struct Cell;
}

CSVRender::TerrainSelection::TerrainSelection(
    osg::Group* parentNode, WorldspaceWidget* worldspaceWidget, TerrainSelectionType type)
    : mParentNode(parentNode)
    , mWorldspaceWidget(worldspaceWidget)
    , mSelectionType(type)
{
    mGeometry = new osg::Geometry();

    mSelectionNode = new osg::Group();
    mSelectionNode->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    mSelectionNode->getOrCreateStateSet()->setRenderBinDetails(11, "RenderBin");
    mSelectionNode->addChild(mGeometry);

    activate();
}

CSVRender::TerrainSelection::~TerrainSelection()
{
    deactivate();
}

std::vector<std::pair<std::int32_t, std::int32_t>> CSVRender::TerrainSelection::getTerrainSelection() const
{
    return mSelection;
}

void CSVRender::TerrainSelection::onlySelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions)
{
    mSelection = localPositions;

    update();
}

void CSVRender::TerrainSelection::addSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::AddSelect);
}

void CSVRender::TerrainSelection::removeSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::RemoveSelect);
}

void CSVRender::TerrainSelection::toggleSelect(const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions)
{
    handleSelection(localPositions, SelectionMethod::ToggleSelect);
}

void CSVRender::TerrainSelection::clearTemporarySelection()
{
    mTemporarySelection.clear();
}

void CSVRender::TerrainSelection::activate()
{
    if (!mParentNode->containsNode(mSelectionNode))
        mParentNode->addChild(mSelectionNode);
}

void CSVRender::TerrainSelection::deactivate()
{
    mParentNode->removeChild(mSelectionNode);
}

void CSVRender::TerrainSelection::update()
{
    mSelectionNode->removeChild(mGeometry);
    mGeometry = new osg::Geometry();

    const osg::ref_ptr<osg::Vec3Array> vertices(new osg::Vec3Array);

    switch (mSelectionType)
    {
        case TerrainSelectionType::Texture:
            drawTextureSelection(vertices);
            break;
        case TerrainSelectionType::Shape:
            drawShapeSelection(vertices);
            break;
    }

    mGeometry->setVertexArray(vertices);
    osg::ref_ptr<osg::DrawArrays> drawArrays = new osg::DrawArrays(osg::PrimitiveSet::LINES);
    drawArrays->setCount(static_cast<GLsizei>(vertices->size()));
    if (!vertices->empty())
        mGeometry->addPrimitiveSet(drawArrays);
    mSelectionNode->addChild(mGeometry);
}

void CSVRender::TerrainSelection::drawShapeSelection(const osg::ref_ptr<osg::Vec3Array>& vertices)
{
    if (!mSelection.empty())
    {
        for (std::pair<std::int32_t, std::int32_t>& localPos : mSelection)
        {
            std::int32_t x(localPos.first);
            std::int32_t y(localPos.second);

            float xWorldCoord(CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(x));
            float yWorldCoord(CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(y));

            osg::Vec3f pointXY(xWorldCoord, yWorldCoord, static_cast<float>(calculateLandHeight(x, y)));

            vertices->push_back(pointXY);
            vertices->push_back(osg::Vec3f(xWorldCoord, CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(y - 1),
                static_cast<float>(calculateLandHeight(x, y - 1))));
            vertices->push_back(pointXY);
            vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(x - 1), yWorldCoord,
                static_cast<float>(calculateLandHeight(x - 1, y))));
            vertices->push_back(pointXY);
            vertices->push_back(osg::Vec3f(xWorldCoord, CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(y + 1),
                static_cast<float>(calculateLandHeight(x, y + 1))));
            vertices->push_back(pointXY);
            vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(x + 1), yWorldCoord,
                static_cast<float>(calculateLandHeight(x + 1, y))));
        }
    }
}

void CSVRender::TerrainSelection::drawTextureSelection(const osg::ref_ptr<osg::Vec3Array>& vertices)
{
    if (!mSelection.empty())
    {
        const std::int32_t landHeightsNudge = (ESM::Land::REAL_SIZE / ESM::Land::LAND_SIZE)
            / (ESM::Land::LAND_SIZE - 1); // Does this work with all land size configurations?

        const std::int32_t textureSizeToLandSizeModifier = (ESM::Land::LAND_SIZE - 1) / ESM::Land::LAND_TEXTURE_SIZE;

        for (std::pair<std::int32_t, std::int32_t>& localPos : mSelection)
        {
            std::int32_t x(localPos.first);
            std::int32_t y(localPos.second);

            // convert texture selection to global vertex coordinates at selection box corners
            std::int32_t x1 = x * textureSizeToLandSizeModifier + landHeightsNudge;
            std::int32_t x2 = x * textureSizeToLandSizeModifier + textureSizeToLandSizeModifier + landHeightsNudge;
            std::int32_t y1 = y * textureSizeToLandSizeModifier - landHeightsNudge;
            std::int32_t y2 = y * textureSizeToLandSizeModifier + textureSizeToLandSizeModifier - landHeightsNudge;

            // Draw edges (check all sides, draw lines between vertices, +1 height to keep lines above ground)
            // Check adjancent selections, draw lines only to edges of the selection
            const auto north = std::find(mSelection.begin(), mSelection.end(), std::make_pair(x, y + 1));
            if (north == mSelection.end())
            {
                for (std::int32_t i = 1; i < (textureSizeToLandSizeModifier + 1); i++)
                {
                    float drawPreviousX = CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>((i - 1) * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    float drawCurrentX = CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>(i * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    vertices->push_back(
                        osg::Vec3f(drawPreviousX, CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y + 1),
                            static_cast<float>(calculateLandHeight(x1 + (i - 1), y2))));
                    vertices->push_back(
                        osg::Vec3f(drawCurrentX, CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y + 1),
                            static_cast<float>(calculateLandHeight(x1 + i, y2))));
                }
            }

            const auto south = std::find(mSelection.begin(), mSelection.end(), std::make_pair(x, y - 1));
            if (south == mSelection.end())
            {
                for (std::int32_t i = 1; i < (textureSizeToLandSizeModifier + 1); i++)
                {
                    float drawPreviousX = CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>((i - 1) * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    float drawCurrentX = CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>(i * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    vertices->push_back(
                        osg::Vec3f(drawPreviousX, CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y),
                            static_cast<float>(calculateLandHeight(x1 + (i - 1), y1))));
                    vertices->push_back(
                        osg::Vec3f(drawCurrentX, CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y),
                            static_cast<float>(calculateLandHeight(x1 + i, y1))));
                }
            }

            const auto east = std::find(mSelection.begin(), mSelection.end(), std::make_pair(x + 1, y));
            if (east == mSelection.end())
            {
                for (std::int32_t i = 1; i < (textureSizeToLandSizeModifier + 1); i++)
                {
                    float drawPreviousY = CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>((i - 1) * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    float drawCurrentY = CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>(i * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x + 1),
                        drawPreviousY, static_cast<float>(calculateLandHeight(x2, y1 + (i - 1)))));
                    vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x + 1),
                        drawCurrentY, static_cast<float>(calculateLandHeight(x2, y1 + i))));
                }
            }

            const auto west = std::find(mSelection.begin(), mSelection.end(), std::make_pair(x - 1, y));
            if (west == mSelection.end())
            {
                for (std::int32_t i = 1; i < (textureSizeToLandSizeModifier + 1); i++)
                {
                    float drawPreviousY = CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>((i - 1) * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    float drawCurrentY = CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(y)
                        // A whole-number division on purpose.
                        // NOLINTNEXTLINE(bugprone-integer-division)
                        + static_cast<float>(i * (ESM::Land::REAL_SIZE / (ESM::Land::LAND_SIZE - 1)));
                    vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x),
                        drawPreviousY, static_cast<float>(calculateLandHeight(x1, y1 + (i - 1)))));
                    vertices->push_back(osg::Vec3f(CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(x),
                        drawCurrentY, static_cast<float>(calculateLandHeight(x1, y1 + i))));
                }
            }
        }
    }
}

void CSVRender::TerrainSelection::handleSelection(
    const std::vector<std::pair<std::int32_t, std::int32_t>>& localPositions, SelectionMethod selectionMethod)
{
    for (auto const& localPos : localPositions)
    {
        const auto iter = std::find(mSelection.begin(), mSelection.end(), localPos);

        switch (selectionMethod)
        {
            case SelectionMethod::OnlySelect:
                break;

            case SelectionMethod::AddSelect:
                if (iter == mSelection.end())
                {
                    mSelection.emplace_back(localPos);
                }
                break;

            case SelectionMethod::RemoveSelect:
                if (iter != mSelection.end())
                {
                    mSelection.erase(iter);
                }
                break;

            case SelectionMethod::ToggleSelect:
            {
                const auto iterTemp = std::find(mTemporarySelection.begin(), mTemporarySelection.end(), localPos);
                if (iterTemp == mTemporarySelection.end())
                {
                    if (iter == mSelection.end())
                    {
                        mSelection.emplace_back(localPos);
                    }
                    else
                    {
                        mSelection.erase(iter);
                    }
                }
                mTemporarySelection.emplace_back(localPos);
                break;
            }

            default:
                break;
        }
    }

    update();
}

bool CSVRender::TerrainSelection::noCell(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Cell>& cellCollection = document.getData().getCells();
    return cellCollection.searchId(ESM::RefId::stringRefId(cellId)) == -1;
}

bool CSVRender::TerrainSelection::noLand(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Land>& landCollection = document.getData().getLand();
    return landCollection.searchId(ESM::RefId::stringRefId(cellId)) == -1;
}

bool CSVRender::TerrainSelection::noLandLoaded(const std::string& cellId)
{
    CSMDoc::Document& document = mWorldspaceWidget->getDocument();
    const CSMWorld::IdCollection<CSMWorld::Land>& landCollection = document.getData().getLand();
    return !landCollection.getRecord(ESM::RefId::stringRefId(cellId)).get().isDataLoaded(ESM::Land::DATA_VNML);
}

bool CSVRender::TerrainSelection::isLandLoaded(const std::string& cellId)
{
    if (!noCell(cellId) && !noLand(cellId) && !noLandLoaded(cellId))
        return true;
    return false;
}

std::int32_t CSVRender::TerrainSelection::calculateLandHeight(
    std::int32_t x, std::int32_t y) // global vertex coordinates
{
    auto cellX = static_cast<std::int32_t>(std::floor(static_cast<float>(x) / (ESM::Land::LAND_SIZE - 1)));
    auto cellY = static_cast<std::int32_t>(std::floor(static_cast<float>(y) / (ESM::Land::LAND_SIZE - 1)));
    std::int32_t localX = x - cellX * (ESM::Land::LAND_SIZE - 1);
    std::int32_t localY = y - cellY * (ESM::Land::LAND_SIZE - 1);

    CSMWorld::CellCoordinates coords(cellX, cellY);

    float landHeight = 0.f;
    if (auto* cell = dynamic_cast<CSVRender::Cell*>(mWorldspaceWidget->getCell(coords)))
    {
        landHeight = cell->getSumOfAlteredAndTrueHeight(cellX, cellY, localX, localY);
    }
    else if (isLandLoaded(CSMWorld::CellCoordinates::generateId(cellX, cellY)))
    {
        CSMDoc::Document& document = mWorldspaceWidget->getDocument();
        std::string cellId = CSMWorld::CellCoordinates::generateId(cellX, cellY);
        const ESM::Land::LandData* landData = document.getData()
                                                  .getLand()
                                                  .getRecord(ESM::RefId::stringRefId(cellId))
                                                  .get()
                                                  .getLandData(ESM::Land::DATA_VHGT);
        return static_cast<std::int32_t>(landData
                ->mHeights[static_cast<std::size_t>(localY) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(localX)]);
    }

    return static_cast<std::int32_t>(landHeight);
}
