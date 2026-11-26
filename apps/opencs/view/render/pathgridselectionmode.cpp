#include "pathgridselectionmode.hpp"

#include <vector>

#include <QAction>
#include <QMenu>
#include <osg/ref_ptr>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/world/commandmacro.hpp>

#include "mask.hpp"
#include "pathgrid.hpp"
#include "selectionmode.hpp"
#include "tagbase.hpp"
#include "worldspacewidget.hpp"

namespace CSVWidget
{
    class SceneToolbar;
}

namespace CSVRender
{
    PathgridSelectionMode::PathgridSelectionMode(CSVWidget::SceneToolbar* parent, WorldspaceWidget& worldspaceWidget)
        : SelectionMode(parent, worldspaceWidget, Mask_Pathgrid)
    {
        mRemoveSelectedNodes = new QAction("Remove selected nodes", this);
        mRemoveSelectedEdges = new QAction("Remove edges between selected nodes", this);

        connect(mRemoveSelectedNodes, &QAction::triggered, this, &PathgridSelectionMode::removeSelectedNodes);
        connect(mRemoveSelectedEdges, &QAction::triggered, this, &PathgridSelectionMode::removeSelectedEdges);
    }

    bool PathgridSelectionMode::createContextMenu(QMenu* menu)
    {
        if (menu)
        {
            SelectionMode::createContextMenu(menu);

            menu->addAction(mRemoveSelectedNodes);
            menu->addAction(mRemoveSelectedEdges);
        }

        return true;
    }

    void PathgridSelectionMode::removeSelectedNodes()
    {
        std::vector<osg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Pathgrid);

<<<<<<< apps/opencs/view/render/pathgridselectionmode.cpp (ours)
        for (auto& entry : selection)
        {
            if (auto* tag = dynamic_cast<PathgridTag*>(entry.get()))
=======
        for (auto& selectedTag : selection)
        {
            if (PathgridTag* tag = dynamic_cast<PathgridTag*>(selectedTag.get()))
>>>>>>> apps/opencs/view/render/pathgridselectionmode.cpp (theirs)
            {
                QUndoStack& undoStack = getWorldspaceWidget().getDocument().getUndoStack();
                QString description = "Remove selected nodes";

                CSMWorld::CommandMacro macro(undoStack, description);
                tag->getPathgrid()->applyRemoveNodes(macro);
            }
        }
    }

    void PathgridSelectionMode::removeSelectedEdges()
    {
        std::vector<osg::ref_ptr<TagBase>> selection = getWorldspaceWidget().getSelection(Mask_Pathgrid);

<<<<<<< apps/opencs/view/render/pathgridselectionmode.cpp (ours)
        for (auto& entry : selection)
        {
            if (auto* tag = dynamic_cast<PathgridTag*>(entry.get()))
=======
        for (auto& selectedTag : selection)
        {
            if (PathgridTag* tag = dynamic_cast<PathgridTag*>(selectedTag.get()))
>>>>>>> apps/opencs/view/render/pathgridselectionmode.cpp (theirs)
            {
                QUndoStack& undoStack = getWorldspaceWidget().getDocument().getUndoStack();
                QString description = "Remove edges between selected nodes";

                CSMWorld::CommandMacro macro(undoStack, description);
                tag->getPathgrid()->applyRemoveEdges(macro);
            }
        }
    }
}
