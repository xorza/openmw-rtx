#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "setting.hpp"

class QMutex;
class QObject;
class QWidget;
class QEvent;
class QPushButton;

namespace CSMPrefs
{
    class Category;

    class ModifierSetting final : public TypedSetting<std::string>
    {
        Q_OBJECT

    public:
        explicit ModifierSetting(
            Category* parent, QMutex* mutex, std::string_view key, const QString& label, Settings::Index& index);

        SettingWidgets makeWidgets(QWidget* parent) override;

        void updateWidget() override;

    protected:
        bool eventFilter(QObject* target, QEvent* event) override;

    private:
        bool handleEvent(QObject* target, std::int32_t mod, std::int32_t value);

        void storeValue(std::int32_t modifier);
        void resetState();

        QPushButton* mButton;
        bool mEditorActive;

    private slots:

        void buttonToggled(bool checked);
    };
}
