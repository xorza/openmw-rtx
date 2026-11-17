#pragma once

#include <cstdint>

#include "idtableproxymodel.hpp"

class QModelIndex;
class QObject;

namespace CSMWorld
{
    /// \brief Removes base records from filtered results.
    class LandTextureTableProxyModel : public IdTableProxyModel
    {
        Q_OBJECT
    public:
        explicit LandTextureTableProxyModel(QObject* parent = nullptr);

    protected:
        bool filterAcceptsRow(std::int32_t sourceRow, const QModelIndex& sourceParent) const override;
    };
}
