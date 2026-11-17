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

#if defined(WIN32)
        void init();

    private slots:
        void updateStyle(bool isDark);
#endif

    signals:
        void darkModeChanged(bool);

    private:
        bool notify(QObject* receiver, QEvent* event) override;

        std::string mCurrentStyle{};
        std::string mInitialStyle{};
        bool mDarkMode{ false };
    };
}
