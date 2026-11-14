#pragma once

#include <QMainWindow>

class QSplitter;
class QStackedWidget;
class QListWidgetItem;

namespace CSVPrefs
{
    class PageBase;

    class Dialogue : public QMainWindow
    {
        Q_OBJECT

        QStackedWidget* mContent;

    private:
        void buildCategorySelector(QSplitter* main);

        void buildContentArea(QSplitter* main);

        PageBase* makePage(const std::string& key);

    public:
        Dialogue();

        ~Dialogue() override;

    protected:
        void closeEvent(QCloseEvent* event) override;

    public slots:

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void show();

    private slots:

        void selectionChanged(QListWidgetItem* current, QListWidgetItem* previous);
    };
}
