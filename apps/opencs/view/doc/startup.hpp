#pragma once

#include <cstdint>

#include <QWidget>

class QGridLayout;
class QString;
class QPushButton;
class QWidget;
class QIcon;

namespace CSVDoc
{
    class StartupDialogue : public QWidget
    {
        Q_OBJECT

    private:
        std::int32_t mWidth;
        std::int32_t mColumn;
        QGridLayout* mLayout{};

        QPushButton* addButton(const QString& label, const QString& icon);

        QWidget* createButtons();

        QWidget* createTools();

    public:
        StartupDialogue();

    signals:

        void createGame();

        void createAddon();

        void loadDocument();

        void editConfig();
    };
}
