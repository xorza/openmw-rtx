#pragma once

#include <QListView>

namespace CSVWidget
{
    class CompleterPopup : public QListView
    {
    public:
        explicit CompleterPopup(QWidget* parent = nullptr);

        int sizeHintForRow(int row) const override;
    };
}
