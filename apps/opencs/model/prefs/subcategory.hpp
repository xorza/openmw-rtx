#pragma once

#include <string>
#include <utility>

#include "setting.hpp"

namespace CSMPrefs
{
    class Category;

    class Subcategory final : public Setting
    {
        Q_OBJECT

    public:
        explicit Subcategory(Category* parent, QMutex* mutex, const QString& label, Settings::Index& index);

        SettingWidgets makeWidgets(QWidget* parent) override;

        void updateWidget() override {}

        void reset() override {}
    };
}
