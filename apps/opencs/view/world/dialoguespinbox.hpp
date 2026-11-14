#ifndef CSV_WORLD_DIALOGUESPINBOX_H
#define CSV_WORLD_DIALOGUESPINBOX_H

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

#endif // CSV_WORLD_DIALOGUESPINBOX_H
