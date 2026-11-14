#include "modifiersetting.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>

#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>

#include "shortcutmanager.hpp"
#include "state.hpp"

class QObject;
class QWidget;

namespace CSMPrefs
{
    ModifierSetting::ModifierSetting(
        Category* parent, QMutex* mutex, std::string_view key, const QString& label, Settings::Index& index)
        : TypedSetting(parent, mutex, key, label, index)
        , mButton(nullptr)
        , mEditorActive(false)
    {
    }

    SettingWidgets ModifierSetting::makeWidgets(QWidget* parent)
    {
        int modifier = 0;
        State::get().getShortcutManager().getModifier(getKey(), modifier);

        QString text = QString::fromUtf8(State::get().getShortcutManager().convertToString(modifier).c_str());

        auto* label = new QLabel(getLabel(), parent);
        auto* widget = new QPushButton(text, parent);

        widget->setCheckable(true);
        widget->installEventFilter(this);

        // right clicking on button sets shortcut to RMB, so context menu should not appear
        widget->setContextMenuPolicy(Qt::PreventContextMenu);

        mButton = widget;

        connect(widget, &QPushButton::toggled, this, &ModifierSetting::buttonToggled);

        return SettingWidgets{ .mLabel = label, .mInput = widget };
    }

    void ModifierSetting::updateWidget()
    {
        if (mButton)
        {
            const std::string& shortcut = getValue();

            int modifier = 0;
            State::get().getShortcutManager().convertFromString(shortcut, modifier);
            State::get().getShortcutManager().setModifier(getKey(), modifier);
            resetState();
        }
    }

    bool ModifierSetting::eventFilter(QObject* target, QEvent* event)
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->isAutoRepeat())
                return true;

            int mod = static_cast<int>(keyEvent->modifiers());
            int key = keyEvent->key();

            return handleEvent(target, mod, key);
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            int mod = static_cast<int>(mouseEvent->modifiers());
            int button = mouseEvent->button();

            return handleEvent(target, mod, button);
        }
        else if (event->type() == QEvent::FocusOut)
        {
            resetState();
        }

        return false;
    }

    bool ModifierSetting::handleEvent(QObject* target, int mod, int value)
    {
        // For potential future exceptions
        const int blacklist[] = { 0 };

        if (!mEditorActive)
        {
            if (value == Qt::RightButton)
            {
                // Clear modifier
                int modifier = 0;
                storeValue(modifier);

                resetState();
            }

            return false;
        }

        // Handle blacklist
        for (int i : blacklist)
        {
            if (value == i)
                return true;
        }

        // Update modifier
        int modifier = value;
        storeValue(modifier);

        resetState();

        return true;
    }

    void ModifierSetting::storeValue(int modifier)
    {
        State::get().getShortcutManager().setModifier(getKey(), modifier);
        setValue(State::get().getShortcutManager().convertToString(modifier));
        getParent()->getState()->update(*this);
    }

    void ModifierSetting::resetState()
    {
        mButton->setChecked(false);
        mEditorActive = false;

        // Button text
        int modifier = 0;
        State::get().getShortcutManager().getModifier(getKey(), modifier);

        QString text = QString::fromUtf8(State::get().getShortcutManager().convertToString(modifier).c_str());
        mButton->setText(text);
    }

    void ModifierSetting::buttonToggled(bool checked)
    {
        if (checked)
            mButton->setText("Press keys or click here...");

        mEditorActive = checked;
    }
}
