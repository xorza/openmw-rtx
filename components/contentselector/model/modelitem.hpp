#pragma once

#include <cstdint>

#include <QList>
#include <QMimeData>

namespace ContentSelectorModel
{
    class ModelItem : public QMimeData
    {
        Q_OBJECT

    public:
        explicit ModelItem(ModelItem* parent = nullptr);
        // ModelItem(const ModelItem *parent = 0);

        ~ModelItem() override;

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        ModelItem* parent() const;
        std::int32_t row() const;

        std::int32_t childCount() const;
        std::int32_t childRow(ModelItem* child) const;
        ModelItem* child(std::int32_t row);

        void appendChild(ModelItem* item);
        void removeChild(std::int32_t row);

        bool hasFormat(const QString& mimetype) const override;

        // virtual bool acceptChild(ModelItem *child);

    protected:
        ModelItem* mParentItem;
        QList<ModelItem*> mChildItems;
    };
}
