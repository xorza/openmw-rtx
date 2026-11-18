#pragma once

#include <cstdint>

#include <QListView>

namespace CSVWidget
{
    class CompleterPopup : public QListView
    {
    public:
        explicit CompleterPopup(QWidget* parent = nullptr);

        std::int32_t sizeHintForRow(std::int32_t row) const override;
    };
}
