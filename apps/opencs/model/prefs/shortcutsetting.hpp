#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <QKeySequence>

#include "setting.hpp"

class QEvent;
class QMutex;
class QObject;
class QPushButton;
class QWidget;

namespace CSMPrefs
{
    class Category;

    class ShortcutSetting final : public TypedSetting<std::string>
    {
        Q_OBJECT

    public:
        explicit ShortcutSetting(
            Category* parent, QMutex* mutex, std::string_view key, const QString& label, Settings::Index& index);

        SettingWidgets makeWidgets(QWidget* parent) override;

        void updateWidget() override;

    protected:
        bool eventFilter(QObject* target, QEvent* event) override;

    private:
        bool handleEvent(QObject* target, std::int32_t mod, std::int32_t value, bool active);

        void storeValue(const QKeySequence& sequence);
        void resetState();

        static constexpr std::int32_t MaxKeys = 4;

        QPushButton* mButton;

        bool mEditorActive;
        std::int32_t mEditorPos;
        std::int32_t mEditorKeys[MaxKeys]{};

    private slots:

        void buttonToggled(bool checked);
    };
}
