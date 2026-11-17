#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <utility>

#include <QMetaType>

namespace osg
{
    class Vec3d;
}

namespace ESM
{
    class RefId;
}

namespace CSMWorld
{
    class CellCoordinates
    {
        std::int32_t mX;
        std::int32_t mY;

    public:
        CellCoordinates();

        CellCoordinates(std::int32_t x, std::int32_t y);

        explicit CellCoordinates(const std::pair<std::int32_t, std::int32_t>& coordinates);

        std::int32_t getX() const;

        std::int32_t getY() const;

        CellCoordinates move(std::int32_t x, std::int32_t y) const;
        ///< Return a copy of *this, moved by the given offset.

        /// Generate cell id string from x and y coordinates
        static std::string generateId(std::int32_t x, std::int32_t y);

        std::string getId(const std::string& worldspace) const;
        ///< Return the ID for the cell at these coordinates.

        static bool isExteriorCell(const std::string& id);

        static bool isExteriorCell(const ESM::RefId& id);

        /// \return first: CellCoordinates (or 0, 0 if cell does not have coordinates),
        /// second: is cell paged?
        ///
        /// \note The worldspace part of \a id is ignored
        static std::pair<CellCoordinates, bool> fromId(const std::string& id);

        /// \return cell coordinates such that given world coordinates are in it.
        static std::pair<std::int32_t, std::int32_t> coordinatesToCellIndex(float x, float y);

        /// Converts worldspace coordinates to global texture selection, taking in account the texture offset.
        static std::pair<std::int32_t, std::int32_t> toTextureCoords(const osg::Vec3d& worldPos);

        /// Converts worldspace coordinates to global vertex selection.
        static std::pair<std::int32_t, std::int32_t> toVertexCoords(const osg::Vec3d& worldPos);

        /// Converts global texture coordinate X to worldspace coordinate, offset by 0.25f.
        static float textureGlobalXToWorldCoords(std::int32_t textureGlobal);

        /// Converts global texture coordinate Y to worldspace coordinate, offset by 0.25f.
        static float textureGlobalYToWorldCoords(std::int32_t textureGlobal);

        /// Converts global vertex coordinate to worldspace coordinate
        static float vertexGlobalToWorldCoords(std::int32_t vertexGlobal);

        /// Converts global vertex coordinate to local cell's heightmap coordinates
        static std::int32_t vertexGlobalToInCellCoords(std::int32_t vertexGlobal);

        /// Converts global texture coordinates to cell id
        static std::string textureGlobalToCellId(const std::pair<std::int32_t, std::int32_t>& textureGlobal);

        /// Converts global vertex coordinates to cell id
        static std::string vertexGlobalToCellId(const std::pair<std::int32_t, std::int32_t>& vertexGlobal);
    };

    bool operator==(const CellCoordinates& left, const CellCoordinates& right);
    bool operator!=(const CellCoordinates& left, const CellCoordinates& right);
    bool operator<(const CellCoordinates& left, const CellCoordinates& right);

    std::ostream& operator<<(std::ostream& stream, const CellCoordinates& coordiantes);
}

Q_DECLARE_METATYPE(CSMWorld::CellCoordinates)
