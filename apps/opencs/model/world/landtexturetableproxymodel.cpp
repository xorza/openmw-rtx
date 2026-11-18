#include "landtexturetableproxymodel.hpp"

#include <cstdint>

#include "idtableproxymodel.hpp"

namespace CSMWorld
{
    LandTextureTableProxyModel::LandTextureTableProxyModel(QObject* parent)
        : IdTableProxyModel(parent)
    {
    }

    bool LandTextureTableProxyModel::filterAcceptsRow(std::int32_t sourceRow, const QModelIndex& sourceParent) const
    {
        return IdTableProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }
}
