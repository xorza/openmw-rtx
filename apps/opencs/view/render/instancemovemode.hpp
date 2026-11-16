#pragma once

#include <apps/opencs/view/widget/modebutton.hpp>

namespace CSVRender
{
    class InstanceMoveMode : public CSVWidget::ModeButton
    {
        Q_OBJECT

    public:
        explicit InstanceMoveMode(QWidget* parent = nullptr);
    };
}
