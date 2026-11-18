#include "instanceselectionmode.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <QAction>
#include <QMenu>
#include <QPoint>
#include <osg/Array>
#include <osg/GL>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Math>
#include <osg/PositionAttitudeTransform>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/StateSet>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include "instancedragmodes.hpp"
#include "mask.hpp"
#include "object.hpp"
#include "selectionmode.hpp"
#include "worldspacewidget.hpp"

namespace CSVWidget
{
    class SceneToolbar;
}

namespace CSVRender
{
    class TagBase;

    InstanceSelectionMode::InstanceSelectionMode(
        CSVWidget::SceneToolbar* parent, WorldspaceWidget& worldspaceWidget, osg::Group* cellNode)
        : SelectionMode(parent, worldspaceWidget, Mask_Reference)
        , mParentNode(cellNode)
    {
        mSelectSame = new QAction("Extend selection to instances with same object ID", this);
        mDeleteSelection = new QAction("Delete selected instances", this);

        connect(mSelectSame, &QAction::triggered, this, &InstanceSelectionMode::selectSame);
        connect(mDeleteSelection, &QAction::triggered, this, &InstanceSelectionMode::deleteSelection);
    }

    InstanceSelectionMode::~InstanceSelectionMode()
    {
        if (mBaseNode)
            mParentNode->removeChild(mBaseNode);
    }

    void InstanceSelectionMode::setDragStart(const osg::Vec3d& dragStart)
    {
        mDragStart = dragStart;
    }

    const osg::Vec3d& InstanceSelectionMode::getDragStart()
    {
        return mDragStart;
    }

    void InstanceSelectionMode::dragEnded(const osg::Vec3d& dragEndPoint, DragMode dragMode)
    {
        float dragDistance = static_cast<float>((mDragStart - dragEndPoint).length());
        if (mBaseNode)
            mParentNode->removeChild(mBaseNode);
        if (getCurrentId() == "cube-centre")
        {
            const osg::Vec3d reach(dragDistance, dragDistance, dragDistance);
            osg::Vec3d pointA(mDragStart - reach);
            osg::Vec3d pointB(mDragStart + reach);
            getWorldspaceWidget().selectInsideCube(pointA, pointB, dragMode);
        }
        else if (getCurrentId() == "cube-corner")
        {
            getWorldspaceWidget().selectInsideCube(mDragStart, dragEndPoint, dragMode);
        }
        else if (getCurrentId() == "sphere")
        {
            getWorldspaceWidget().selectWithinDistance(mDragStart, dragDistance, dragMode);
        }
    }

    void InstanceSelectionMode::drawSelectionCubeCentre(const osg::Vec3f& mousePlanePoint)
    {
        float dragDistance = static_cast<float>((mDragStart - mousePlanePoint).length());
        drawSelectionCube(mDragStart, dragDistance);
    }

    void InstanceSelectionMode::drawSelectionCubeCorner(const osg::Vec3f& mousePlanePoint)
    {
        drawSelectionBox(mDragStart, mousePlanePoint);
    }

    void InstanceSelectionMode::drawSelectionBox(const osg::Vec3d& pointA, const osg::Vec3d& pointB)
    {
        if (mBaseNode)
            mParentNode->removeChild(mBaseNode);
        mBaseNode = new osg::PositionAttitudeTransform;
        mBaseNode->setPosition(pointA);

        osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);

        auto* vertices = new osg::Vec3Array;
        vertices->push_back(osg::Vec3f(0.0f, 0.0f, 0.0f));
        vertices->push_back(osg::Vec3f(0.0f, 0.0f, static_cast<float>(pointB[2] - pointA[2])));
        vertices->push_back(osg::Vec3f(0.0f, static_cast<float>(pointB[1] - pointA[1]), 0.0f));
        vertices->push_back(
            osg::Vec3f(0.0f, static_cast<float>(pointB[1] - pointA[1]), static_cast<float>(pointB[2] - pointA[2])));

        vertices->push_back(osg::Vec3f(static_cast<float>(pointB[0] - pointA[0]), 0.0f, 0.0f));
        vertices->push_back(
            osg::Vec3f(static_cast<float>(pointB[0] - pointA[0]), 0.0f, static_cast<float>(pointB[2] - pointA[2])));
        vertices->push_back(
            osg::Vec3f(static_cast<float>(pointB[0] - pointA[0]), static_cast<float>(pointB[1] - pointA[1]), 0.0f));
        vertices->push_back(osg::Vec3f(static_cast<float>(pointB[0] - pointA[0]),
            static_cast<float>(pointB[1] - pointA[1]), static_cast<float>(pointB[2] - pointA[2])));

        geometry->setVertexArray(vertices);

        auto* primitives = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLES, 0);

        // top
        primitives->push_back(2);
        primitives->push_back(1);
        primitives->push_back(0);

        primitives->push_back(3);
        primitives->push_back(1);
        primitives->push_back(2);

        // bottom
        primitives->push_back(4);
        primitives->push_back(5);
        primitives->push_back(6);

        primitives->push_back(6);
        primitives->push_back(5);
        primitives->push_back(7);

        // sides
        primitives->push_back(1);
        primitives->push_back(4);
        primitives->push_back(0);

        primitives->push_back(4);
        primitives->push_back(1);
        primitives->push_back(5);

        primitives->push_back(4);
        primitives->push_back(2);
        primitives->push_back(0);

        primitives->push_back(6);
        primitives->push_back(2);
        primitives->push_back(4);

        primitives->push_back(6);
        primitives->push_back(3);
        primitives->push_back(2);

        primitives->push_back(7);
        primitives->push_back(3);
        primitives->push_back(6);

        primitives->push_back(1);
        primitives->push_back(3);
        primitives->push_back(5);

        primitives->push_back(5);
        primitives->push_back(3);
        primitives->push_back(7);

        geometry->addPrimitiveSet(primitives);

        auto* colours = new osg::Vec4Array;

        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.5f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));

        geometry->setColorArray(colours, osg::Array::BIND_PER_VERTEX);

        geometry->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
        geometry->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        geometry->getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        mBaseNode->addChild(geometry);
        mParentNode->addChild(mBaseNode);
    }

    void InstanceSelectionMode::drawSelectionCube(const osg::Vec3d& point, float radius)
    {
        if (mBaseNode)
            mParentNode->removeChild(mBaseNode);
        mBaseNode = new osg::PositionAttitudeTransform;
        mBaseNode->setPosition(point);

        osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);

        auto* vertices = new osg::Vec3Array;
        for (std::int32_t i = 0; i < 2; ++i)
        {
            float height = i ? -radius : radius;
            vertices->push_back(osg::Vec3f(height, -radius, -radius));
            vertices->push_back(osg::Vec3f(height, -radius, radius));
            vertices->push_back(osg::Vec3f(height, radius, -radius));
            vertices->push_back(osg::Vec3f(height, radius, radius));
        }

        geometry->setVertexArray(vertices);

        auto* primitives = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLES, 0);

        // top
        primitives->push_back(2);
        primitives->push_back(1);
        primitives->push_back(0);

        primitives->push_back(3);
        primitives->push_back(1);
        primitives->push_back(2);

        // bottom
        primitives->push_back(4);
        primitives->push_back(5);
        primitives->push_back(6);

        primitives->push_back(6);
        primitives->push_back(5);
        primitives->push_back(7);

        // sides
        primitives->push_back(1);
        primitives->push_back(4);
        primitives->push_back(0);

        primitives->push_back(4);
        primitives->push_back(1);
        primitives->push_back(5);

        primitives->push_back(4);
        primitives->push_back(2);
        primitives->push_back(0);

        primitives->push_back(6);
        primitives->push_back(2);
        primitives->push_back(4);

        primitives->push_back(6);
        primitives->push_back(3);
        primitives->push_back(2);

        primitives->push_back(7);
        primitives->push_back(3);
        primitives->push_back(6);

        primitives->push_back(1);
        primitives->push_back(3);
        primitives->push_back(5);

        primitives->push_back(5);
        primitives->push_back(3);
        primitives->push_back(7);

        geometry->addPrimitiveSet(primitives);

        auto* colours = new osg::Vec4Array;

        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.5f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));
        colours->push_back(osg::Vec4f(0.3f, 0.3f, 0.4f, 0.2f));
        colours->push_back(osg::Vec4f(0.9f, 0.9f, 1.0f, 0.2f));

        geometry->setColorArray(colours, osg::Array::BIND_PER_VERTEX);

        geometry->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
        geometry->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        geometry->getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        mBaseNode->addChild(geometry);
        mParentNode->addChild(mBaseNode);
    }

    void InstanceSelectionMode::drawSelectionSphere(const osg::Vec3f& mousePlanePoint)
    {
        float dragDistance = static_cast<float>((mDragStart - mousePlanePoint).length());
        drawSelectionSphere(mDragStart, dragDistance);
    }

    void InstanceSelectionMode::drawSelectionSphere(const osg::Vec3d& point, float radius)
    {
        if (mBaseNode)
            mParentNode->removeChild(mBaseNode);
        mBaseNode = new osg::PositionAttitudeTransform;
        mBaseNode->setPosition(point);

        osg::ref_ptr<osg::Geometry> geometry(new osg::Geometry);

        auto* vertices = new osg::Vec3Array;
        constexpr std::int32_t resolution = 32;
        float radiusPerResolution = radius / resolution;
        float reciprocalResolution = 1.0f / resolution;
        float doubleReciprocalRes = reciprocalResolution * 2;

        auto* colours = new osg::Vec4Array;

        for (std::int32_t i = 0; i <= resolution; i += 2)
        {
            float iShifted = (static_cast<float>(i) - resolution / 2.0f); // i - 16 = -16 ... 16
            float xPercentile = iShifted * doubleReciprocalRes;
            float x = xPercentile * radius;
            float thisRadius = sqrt(radius * radius - x * x);

            // the next row
            float iShifted2 = (static_cast<float>(i + 1) - resolution / 2.0f);
            float xPercentile2 = iShifted2 * doubleReciprocalRes;
            float x2 = xPercentile2 * radius;
            float thisRadius2 = sqrt(radius * radius - x2 * x2);

            for (std::int32_t j = 0; j < resolution; ++j)
            {
                auto vertexX = static_cast<float>(static_cast<double>(thisRadius)
                    * sin(static_cast<double>(static_cast<float>(j) * reciprocalResolution) * osg::PI * 2));
                float vertexY = static_cast<float>(i) * radiusPerResolution * 2 - radius;
                auto vertexZ = static_cast<float>(static_cast<double>(thisRadius)
                    * cos(static_cast<double>(static_cast<float>(j) * reciprocalResolution) * osg::PI * 2));
                float heightPercentage = (vertexZ + radius) / (radius * 2);
                vertices->push_back(osg::Vec3f(vertexX, vertexY, vertexZ));
                colours->push_back(osg::Vec4f(heightPercentage, heightPercentage, heightPercentage, 0.3f));

                auto vertexNextRowX = static_cast<float>(static_cast<double>(thisRadius2)
                    * sin(static_cast<double>(static_cast<float>(j) * reciprocalResolution) * osg::PI * 2));
                float vertexNextRowY = static_cast<float>((i + 1)) * radiusPerResolution * 2 - radius;
                auto vertexNextRowZ = static_cast<float>(static_cast<double>(thisRadius2)
                    * cos(static_cast<double>(static_cast<float>(j) * reciprocalResolution) * osg::PI * 2));
                float heightPercentageNextRow = (vertexZ + radius) / (radius * 2);
                vertices->push_back(osg::Vec3f(vertexNextRowX, vertexNextRowY, vertexNextRowZ));
                colours->push_back(
                    osg::Vec4f(heightPercentageNextRow, heightPercentageNextRow, heightPercentageNextRow, 0.3f));
            }
        }

        geometry->setVertexArray(vertices);

        auto* primitives = new osg::DrawElementsUShort(osg::PrimitiveSet::TRIANGLE_STRIP, 0);

        for (std::int32_t i = 0; i < resolution; ++i)
        {
            // Even
            for (std::int32_t j = 0; j < resolution * 2; ++j)
            {
                if (i * resolution * 2 + j > static_cast<std::int32_t>(vertices->size()) - 1)
                    continue;
                primitives->push_back(static_cast<std::uint16_t>(i * resolution * 2 + j));
            }
            if (i * resolution * 2 > static_cast<std::int32_t>(vertices->size()) - 1)
                continue;
            primitives->push_back(static_cast<std::uint16_t>(i * resolution * 2));
            primitives->push_back(static_cast<std::uint16_t>(i * resolution * 2 + 1));

            // Odd
            for (std::int32_t j = 1; j < resolution * 2 - 2; j += 2)
            {
                if ((i + 1) * resolution * 2 + j - 1 > static_cast<std::int32_t>(vertices->size()) - 1)
                    continue;
                primitives->push_back(static_cast<std::uint16_t>((i + 1) * resolution * 2 + j - 1));
                primitives->push_back(static_cast<std::uint16_t>(i * resolution * 2 + j + 2));
            }
            if ((i + 2) * resolution * 2 - 2 > static_cast<std::int32_t>(vertices->size()) - 1)
                continue;
            primitives->push_back(static_cast<std::uint16_t>((i + 2) * resolution * 2 - 2));
            primitives->push_back(static_cast<std::uint16_t>(i * resolution * 2 + 1));
            primitives->push_back(static_cast<std::uint16_t>((i + 1) * resolution * 2));
        }

        geometry->addPrimitiveSet(primitives);

        geometry->setColorArray(colours, osg::Array::BIND_PER_VERTEX);

        geometry->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::ON);
        geometry->getOrCreateStateSet()->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
        geometry->getOrCreateStateSet()->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

        mBaseNode->addChild(geometry);
        mParentNode->addChild(mBaseNode);
    }

    bool InstanceSelectionMode::createContextMenu(QMenu* menu)
    {
        if (menu)
        {
            SelectionMode::createContextMenu(menu);

            menu->addAction(mSelectSame);
            menu->addAction(mDeleteSelection);
        }

        return true;
    }

    void InstanceSelectionMode::selectSame()
    {
        getWorldspaceWidget().selectAllWithSameParentId(Mask_Reference);
    }

    void InstanceSelectionMode::deleteSelection()
    {
        std::vector<osg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Reference);

        CSMWorld::IdTable& referencesTable = dynamic_cast<CSMWorld::IdTable&>(
            *getWorldspaceWidget().getDocument().getData().getTableModel(CSMWorld::UniversalId::Type_References));

        for (auto& entry : selection)
        {
            auto* command = new CSMWorld::DeleteCommand(
                referencesTable, static_cast<ObjectTag*>(entry.get())->mObject->getReferenceId());

            getWorldspaceWidget().getDocument().getUndoStack().push(command);
        }
    }
}
