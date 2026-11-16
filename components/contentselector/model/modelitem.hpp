#pragma once

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
        int row() const;

        int childCount() const;
        int childRow(ModelItem* child) const;
        ModelItem* child(int row);

        void appendChild(ModelItem* item);
        void removeChild(int row);

        bool hasFormat(const QString& mimetype) const override;

        // virtual bool acceptChild(ModelItem *child);

    protected:
        ModelItem* mParentItem;
        QList<ModelItem*> mChildItems;
    };
}
