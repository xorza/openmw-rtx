#pragma once

#include <cstdint>

#include <QWidget>

class QVBoxLayout;

namespace CSVWidget
{
    class SceneTool;

    class SceneToolbar : public QWidget
    {
        Q_OBJECT

        QVBoxLayout* mLayout;
        std::int32_t mButtonSize;
        std::int32_t mIconSize;

    protected:
        void focusInEvent(QFocusEvent* event) override;

    public:
        explicit SceneToolbar(std::int32_t buttonSize, QWidget* parent = nullptr);

        /// If insertPoint==0, insert \a tool at the end of the scrollbar. Otherwise
        /// insert tool after \a insertPoint.
        void addTool(SceneTool* tool, SceneTool* insertPoint = nullptr);

        void removeTool(SceneTool* tool);

        std::int32_t getButtonSize() const;

        std::int32_t getIconSize() const;

    signals:

        void focusSceneRequest();
    };
}
