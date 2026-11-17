#pragma once

#include <cstdint>

#include <QApplication>
#include <QEvent>

namespace Platform
{
    /// What a program looks like: the system's, which on Windows follows its dark mode, or the fork's
    /// own (`Graphite::apply`), the same on every system.
    enum class Look
    {
        System,
        Graphite,
    };

    class Application : public QApplication
    {
        Q_OBJECT

    public:
        Application(std::int32_t& argc, char* argv[], Look look);

    signals:
        void darkModeChanged(bool);

    private slots:
        /// The look for the system's dark mode, `isDark` or not: Windows' own style or its dark sheet; nothing where
        /// the system's style follows its mode itself.
        void updateStyle(bool isDark);

    private:
        /// The system's look as it starts: on Windows its dark mode followed (`applicationwin32.cpp`); nothing on
        /// Linux, whose style follows the system itself (`applicationposix.cpp`).
        void followSystemLook();

        /// Whether the system's dark mode turned since it was last read, which it then holds; never on Linux.
        bool darkModeTurned();

        bool notify(QObject* receiver, QEvent* event) override;

        std::string mCurrentStyle{};
        std::string mInitialStyle{};
        bool mDarkMode{ false };
    };
}
