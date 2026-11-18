#pragma once

#include <cstdint>

#include "setting.hpp"

class QSpinBox;
class QMutex;
class QObject;
class QWidget;

namespace CSMPrefs
{
    class Category;

    class IntSetting final : public TypedSetting<std::int32_t>
    {
        Q_OBJECT

        std::int32_t mMin;
        std::int32_t mMax;
        std::string mTooltip;
        QSpinBox* mWidget;

    public:
        explicit IntSetting(
            Category* parent, QMutex* mutex, std::string_view key, const QString& label, Settings::Index& index);

        // defaults to [0, std::numeric_limits<int>::max()]
        IntSetting& setRange(std::int32_t min, std::int32_t max);

        IntSetting& setMin(std::int32_t min);

        IntSetting& setMax(std::int32_t max);

        IntSetting& setTooltip(const std::string& tooltip);

        /// Return label, input widget.
        SettingWidgets makeWidgets(QWidget* parent) override;

        void updateWidget() override;

    private slots:

        void valueChanged(std::int32_t value);
    };
}
