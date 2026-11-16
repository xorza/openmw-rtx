#pragma once

#include <QColor>
#include <string>
#include <string_view>
#include <utility>

#include "setting.hpp"

class QMutex;
class QObject;
class QWidget;

namespace CSVWidget
{
    class ColorEditor;
}

namespace CSMPrefs
{
    class Category;

    class ColourSetting final : public TypedSetting<std::string>
    {
        Q_OBJECT

        std::string mTooltip;
        CSVWidget::ColorEditor* mWidget;

    public:
        explicit ColourSetting(
            Category* parent, QMutex* mutex, std::string_view key, const QString& label, Settings::Index& index);

        ColourSetting& setTooltip(const std::string& tooltip);

        /// Return label, input widget.
        SettingWidgets makeWidgets(QWidget* parent) override;

        void updateWidget() override;

    private slots:

        void valueChanged();
    };
}
