#pragma once

#include <cstdint>
#include <string>

#include <QKeySequence>
#include <QObject>
#include <QString>

class QAction;
class QWidget;

namespace CSMPrefs
{
    /// A class similar in purpose to QShortcut, but with the ability to use mouse buttons
    class Shortcut : public QObject
    {
        Q_OBJECT

    public:
        enum ActivationStatus
        {
            AS_Regular,
            AS_Secondary,
            AS_Inactive
        };

        enum SecondaryMode
        {
            SM_Replace, ///< The secondary signal replaces the regular signal when the modifier is active
            SM_Detach, ///< The secondary signal is emitted independent of the regular signal, even if not active
            SM_Ignore ///< The secondary signal will not ever be emitted
        };

        Shortcut(const std::string& name, QWidget* parent);
        Shortcut(const std::string& name, const std::string& modName, QWidget* parent);
        Shortcut(const std::string& name, const std::string& modName, SecondaryMode secMode, QWidget* parent);

        ~Shortcut() override;

        bool isEnabled() const;

        const std::string& getName() const;
        const std::string& getModifierName() const;

        SecondaryMode getSecondaryMode() const;

        const QKeySequence& getSequence() const;
        std::int32_t getModifier() const;

        /// The position in the sequence
        std::int32_t getPosition() const;
        /// The position in the sequence
        std::int32_t getLastPosition() const;

        ActivationStatus getActivationStatus() const;
        bool getModifierStatus() const;

        void enable(bool state);

        void setSequence(const QKeySequence& sequence);
        void setModifier(std::int32_t modifier);

        /// The position in the sequence
        void setPosition(std::int32_t pos);

        void setActivationStatus(ActivationStatus status);
        void setModifierStatus(bool status);

        /// Appends the sequence to the QAction text, also keeps it up to date
        void associateAction(QAction* action);

        // Workaround for Qt4 signals being "protected"
        void signalActivated(bool state);
        void signalActivated();

        void signalSecondary(bool state);
        void signalSecondary();

        QString toString() const;

    private:
        bool mEnabled;

        std::string mName;
        std::string mModName;
        SecondaryMode mSecondaryMode;
        QKeySequence mSequence;
        std::int32_t mModifier;

        std::int32_t mCurrentPos;
        std::int32_t mLastPos;

        ActivationStatus mActivationStatus;
        bool mModifierStatus;

        QAction* mAction;
        QString mActionText;

    private slots:

        void actionDeleted();

    signals:

        /// Triggered when the shortcut is activated or deactivated; can be determined from \p state
        void activated(bool state);

        /// Convenience signal.
        void activated();

        /// Triggered depending on SecondaryMode
        void secondary(bool state);

        /// Convenience signal.
        void secondary();
    };
}
