#include "shortcuteventhandler.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWidget>

#include "shortcut.hpp"

namespace CSMPrefs
{
    ShortcutEventHandler::ShortcutEventHandler(QObject* parent)
        : QObject(parent)
    {
    }

    void ShortcutEventHandler::addShortcut(Shortcut* shortcut)
    {
        // Enforced by shortcut class
        auto* widget = static_cast<QWidget*>(shortcut->parent());

        // Check if widget setup is needed
        auto shortcutListIt = mWidgetShortcuts.find(widget);
        if (shortcutListIt == mWidgetShortcuts.end())
        {
            // Create list
            shortcutListIt = mWidgetShortcuts.insert(std::make_pair(widget, ShortcutList())).first;

            // Check if widget has a parent with shortcuts, unfortunately it is not typically set yet
            updateParent(widget);

            // Intercept widget events
            widget->installEventFilter(this);
            connect(widget, &QWidget::destroyed, this, &ShortcutEventHandler::widgetDestroyed);
        }

        // Add to list
        shortcutListIt->second.push_back(shortcut);
    }

    void ShortcutEventHandler::removeShortcut(Shortcut* shortcut)
    {
        // Enforced by shortcut class
        auto* widget = static_cast<QWidget*>(shortcut->parent());

        auto shortcutListIt = mWidgetShortcuts.find(widget);
        if (shortcutListIt != mWidgetShortcuts.end())
        {
            shortcutListIt->second.erase(
                std::remove(shortcutListIt->second.begin(), shortcutListIt->second.end(), shortcut),
                shortcutListIt->second.end());
        }
    }

    bool ShortcutEventHandler::eventFilter(QObject* watched, QEvent* event)
    {
        // Process event
        if (event->type() == QEvent::KeyPress)
        {
            auto* widget = static_cast<QWidget*>(watched);
            auto* keyEvent = static_cast<QKeyEvent*>(event);

            if (!keyEvent->isAutoRepeat())
                return activate(widget, keyEvent->keyCombination());
        }
        else if (event->type() == QEvent::KeyRelease)
        {
            auto* widget = static_cast<QWidget*>(watched);
            auto* keyEvent = static_cast<QKeyEvent*>(event);

            if (!keyEvent->isAutoRepeat())
                return deactivate(widget, keyEvent->keyCombination());
        }
        else if (event->type() == QEvent::MouseButtonPress)
        {
            auto* widget = static_cast<QWidget*>(watched);
            auto* mouseEvent = static_cast<QMouseEvent*>(event);

            return activate(widget, QKeyCombination(mouseEvent->modifiers(), Qt::Key(mouseEvent->button())));
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto* widget = static_cast<QWidget*>(watched);
            auto* mouseEvent = static_cast<QMouseEvent*>(event);

            return deactivate(widget, QKeyCombination(mouseEvent->modifiers(), Qt::Key(mouseEvent->button())));
        }
        else if (event->type() == QEvent::FocusOut)
        {
            auto* widget = static_cast<QWidget*>(watched);
            auto shortcutListIt = mWidgetShortcuts.find(widget);
            assert(shortcutListIt != mWidgetShortcuts.end());

            // Deactivate in case events are missed
            for (auto shortcut : shortcutListIt->second)
            {
                shortcut->setPosition(0);
                shortcut->setModifierStatus(false);

                if (shortcut->getActivationStatus() == Shortcut::AS_Regular)
                {
                    shortcut->setActivationStatus(Shortcut::AS_Inactive);
                    shortcut->signalActivated(false);
                }
                else if (shortcut->getActivationStatus() == Shortcut::AS_Secondary)
                {
                    shortcut->setActivationStatus(Shortcut::AS_Inactive);
                    shortcut->signalSecondary(false);
                }
            }
        }
        else if (event->type() == QEvent::FocusIn)
        {
            auto* widget = static_cast<QWidget*>(watched);
            updateParent(widget);
        }

        return false;
    }

    void ShortcutEventHandler::updateParent(QWidget* widget)
    {
        QWidget* parent = widget->parentWidget();
        while (parent)
        {
            auto parentIt = mWidgetShortcuts.find(parent);
            if (parentIt != mWidgetShortcuts.end())
            {
                mChildParentRelations.insert(std::make_pair(widget, parent));
                updateParent(parent);
                break;
            }

            // Check next
            parent = parent->parentWidget();
        }
    }

    bool ShortcutEventHandler::activate(QWidget* widget, QKeyCombination keyCombination)
    {
        std::vector<std::pair<MatchResult, Shortcut*>> potentials;
        bool used = false;

        while (widget)
        {
            auto shortcutListIt = mWidgetShortcuts.find(widget);
            assert(shortcutListIt != mWidgetShortcuts.end());

            // Find potential activations
<<<<<<< apps/opencs/model/prefs/shortcuteventhandler.cpp (ours)
            for (auto shortcut : shortcutListIt->second)
=======
            for (auto it = shortcutListIt->second.begin(); it != shortcutListIt->second.end(); ++it)
>>>>>>> apps/opencs/model/prefs/shortcuteventhandler.cpp (theirs)
            {
                if (!shortcut->isEnabled())
                    continue;

                if (checkModifier(keyCombination, shortcut, true))
                    used = true;

                if (shortcut->getActivationStatus() != Shortcut::AS_Inactive)
                    continue;

                std::int32_t pos = shortcut->getPosition();
                std::int32_t lastPos = shortcut->getLastPosition();
                MatchResult result = match(keyCombination.keyboardModifiers(), keyCombination.key(),
                    static_cast<std::uint32_t>(shortcut->getSequence()[static_cast<uint>(pos)].toCombined()));

                if (result == Matches_WithMod || result == Matches_NoMod)
                {
                    if (pos < lastPos && (result == Matches_WithMod || pos > 0))
                    {
                        shortcut->setPosition(pos + 1);
                    }
                    else if (pos == lastPos)
                    {
                        potentials.emplace_back(result, shortcut);
                    }
                }
            }

            // Move on to parent
            auto widgetIt = mChildParentRelations.find(widget);
            widget = (widgetIt != mChildParentRelations.end()) ? widgetIt->second : nullptr;
        }

        // Only activate the best match; in exact conflicts, this will favor the first shortcut added.
        if (!potentials.empty())
        {
            std::stable_sort(potentials.begin(), potentials.end(), ShortcutEventHandler::sort);
            Shortcut* shortcut = potentials.front().second;

            if (shortcut->getModifierStatus() && shortcut->getSecondaryMode() == Shortcut::SM_Replace)
            {
                shortcut->setActivationStatus(Shortcut::AS_Secondary);
                shortcut->signalSecondary(true);
                shortcut->signalSecondary();
            }
            else
            {
                shortcut->setActivationStatus(Shortcut::AS_Regular);
                shortcut->signalActivated(true);
                shortcut->signalActivated();
            }

            used = true;
        }

        return used;
    }

    bool ShortcutEventHandler::deactivate(QWidget* widget, QKeyCombination keyCombination)
    {
        bool used = false;

        while (widget)
        {
            auto shortcutListIt = mWidgetShortcuts.find(widget);
            assert(shortcutListIt != mWidgetShortcuts.end());

<<<<<<< apps/opencs/model/prefs/shortcuteventhandler.cpp (ours)
            for (auto shortcut : shortcutListIt->second)
=======
            for (auto it = shortcutListIt->second.begin(); it != shortcutListIt->second.end(); ++it)
>>>>>>> apps/opencs/model/prefs/shortcuteventhandler.cpp (theirs)
            {
                if (checkModifier(keyCombination, shortcut, false))
                    used = true;

                std::int32_t pos = shortcut->getPosition();
                MatchResult result
                    = match(0, keyCombination.key(), shortcut->getSequence()[static_cast<uint>(pos)].key());

                if (result != Matches_Not)
                {
                    shortcut->setPosition(0);

                    if (shortcut->getActivationStatus() == Shortcut::AS_Regular)
                    {
                        shortcut->setActivationStatus(Shortcut::AS_Inactive);
                        shortcut->signalActivated(false);
                        used = true;
                    }
                    else if (shortcut->getActivationStatus() == Shortcut::AS_Secondary)
                    {
                        shortcut->setActivationStatus(Shortcut::AS_Inactive);
                        shortcut->signalSecondary(false);
                        used = true;
                    }
                }
            }

            // Move on to parent
            auto widgetIt = mChildParentRelations.find(widget);
            widget = (widgetIt != mChildParentRelations.end()) ? widgetIt->second : nullptr;
        }

        return used;
    }

    bool ShortcutEventHandler::checkModifier(QKeyCombination keyCombination, Shortcut* shortcut, bool activate)
    {
        if (!shortcut->isEnabled() || !shortcut->getModifier() || shortcut->getSecondaryMode() == Shortcut::SM_Ignore
            || shortcut->getModifierStatus() == activate)
            return false;

        MatchResult result = match(keyCombination.keyboardModifiers(), keyCombination.key(),
            static_cast<std::uint32_t>(shortcut->getModifier()));
        bool used = false;

        if (result != Matches_Not)
        {
            shortcut->setModifierStatus(activate);

            if (shortcut->getSecondaryMode() == Shortcut::SM_Detach)
            {
                if (activate)
                {
                    shortcut->signalSecondary(true);
                    shortcut->signalSecondary();
                }
                else
                {
                    shortcut->signalSecondary(false);
                }
            }
            else if (!activate && shortcut->getActivationStatus() == Shortcut::AS_Secondary)
            {
                shortcut->setActivationStatus(Shortcut::AS_Inactive);
                shortcut->setPosition(0);
                shortcut->signalSecondary(false);
                used = true;
            }
        }

        return used;
    }

    ShortcutEventHandler::MatchResult ShortcutEventHandler::match(
        std::uint32_t mod, std::uint32_t button, std::uint32_t value)
    {
        if ((mod | button) == value)
        {
            return Matches_WithMod;
        }
        else if (button == value)
        {
            return Matches_NoMod;
        }
        else
        {
            return Matches_Not;
        }
    }

    bool ShortcutEventHandler::sort(
        const std::pair<MatchResult, Shortcut*>& left, const std::pair<MatchResult, Shortcut*>& right)
    {
        if (left.first == Matches_WithMod && right.first == Matches_NoMod)
            return true;
        else
            return left.second->getPosition() > right.second->getPosition();
    }

    void ShortcutEventHandler::widgetDestroyed()
    {
        auto* widget = static_cast<QWidget*>(sender());

        mWidgetShortcuts.erase(widget);
        mChildParentRelations.erase(widget);
    }
}
