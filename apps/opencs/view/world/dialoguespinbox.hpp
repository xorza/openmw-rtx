#pragma once

#include <QDoubleSpinBox>
#include <QSpinBox>

namespace CSVWorld
{
    class DialogueSpinBox : public QSpinBox
    {
        Q_OBJECT

    public:
        explicit DialogueSpinBox(QWidget* parent = nullptr);

    protected:
        void focusInEvent(QFocusEvent* event) override;
        void focusOutEvent(QFocusEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;
    };

    class DialogueDoubleSpinBox : public QDoubleSpinBox
    {
        Q_OBJECT

    public:
        explicit DialogueDoubleSpinBox(QWidget* parent = nullptr);

    protected:
        void focusInEvent(QFocusEvent* event) override;
        void focusOutEvent(QFocusEvent* event) override;
        void wheelEvent(QWheelEvent* event) override;
    };
}
