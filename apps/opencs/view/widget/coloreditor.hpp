#pragma once

#include <cstdint>

#include <QPushButton>

class QColor;
class QPoint;
class QSize;

namespace CSVWidget
{
    class ColorPickerPopup;

    class ColorEditor : public QPushButton
    {
        Q_OBJECT

        QColor mColor;
        ColorPickerPopup* mColorPicker;
        bool mPopupOnStart;

        QPoint calculatePopupPosition();

    public:
        explicit ColorEditor(const QColor& color, QWidget* parent = nullptr, const bool popupOnStart = false);
        explicit ColorEditor(const std::int32_t colorInt, QWidget* parent = nullptr, const bool popupOnStart = false);

        QColor color() const;

        /// \return Color RGB value encoded in an int.
        std::int32_t colorInt() const;

        void setColor(const QColor& color);

        /// \brief Set color using given int value.
        /// \param colorInt RGB color value encoded as an integer.
        void setColor(const std::int32_t colorInt);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void showEvent(QShowEvent* event) override;

    private:
        explicit ColorEditor(QWidget* parent = nullptr, const bool popupOnStart = false);

    private slots:
        void showPicker();
        void pickerColorChanged(const QColor& color);

    signals:
        void pickingFinished();
    };
}
