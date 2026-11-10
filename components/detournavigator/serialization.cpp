#include "serialization.hpp"

#include "preparednavmeshdata.hpp"
#include "recast.hpp"

#include <components/serialization/binaryreader.hpp>
#include <components/serialization/binarywriter.hpp>
#include <components/serialization/format.hpp>
#include <components/serialization/sizeaccumulator.hpp>

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace DetourNavigator
{
    namespace
    {
        template <Serialization::Mode mode>
        struct Format : Serialization::Format<mode, Format<mode>>
        {
            using Serialization::Format<mode, Format<mode>>::operator();

            template <class Visitor, class T>
            auto operator()(Visitor&& visitor, T& value) const
                -> std::enable_if_t<std::is_same_v<std::decay_t<T>, rcPolyMesh>>
            {
                visitor(*this, value.nverts);
                visitor(*this, value.npolys);
                visitor(*this, value.maxpolys);
                visitor(*this, value.nvp);
                visitor(*this, value.bmin);
                visitor(*this, value.bmax);
                visitor(*this, value.cs);
                visitor(*this, value.ch);
                visitor(*this, value.borderSize);
                visitor(*this, value.maxEdgeError);
                if constexpr (mode == Serialization::Mode::Read)
                {
                    if (value.verts == nullptr)
                        permRecastAlloc(value.verts, getVertsLength(value));
                    if (value.polys == nullptr)
                        permRecastAlloc(value.polys, getPolysLength(value));
                    if (value.regs == nullptr)
                        permRecastAlloc(value.regs, getRegsLength(value));
                    if (value.flags == nullptr)
                        permRecastAlloc(value.flags, getFlagsLength(value));
                    if (value.areas == nullptr)
                        permRecastAlloc(value.areas, getAreasLength(value));
                }
                visitor(*this, value.verts, getVertsLength(value));
                visitor(*this, value.polys, getPolysLength(value));
                visitor(*this, value.regs, getRegsLength(value));
                visitor(*this, value.flags, getFlagsLength(value));
                visitor(*this, value.areas, getAreasLength(value));
            }

            template <class Visitor, class T>
            auto operator()(Visitor&& visitor, T& value) const
                -> std::enable_if_t<std::is_same_v<std::decay_t<T>, rcPolyMeshDetail>>
            {
                visitor(*this, value.nmeshes);
                if constexpr (mode == Serialization::Mode::Read)
                    if (value.meshes == nullptr)
                        permRecastAlloc(value.meshes, getMeshesLength(value));
                visitor(*this, value.meshes, getMeshesLength(value));
                visitor(*this, value.nverts);
                if constexpr (mode == Serialization::Mode::Read)
                    if (value.verts == nullptr)
                        permRecastAlloc(value.verts, getVertsLength(value));
                visitor(*this, value.verts, getVertsLength(value));
                visitor(*this, value.ntris);
                if constexpr (mode == Serialization::Mode::Read)
                    if (value.tris == nullptr)
                        permRecastAlloc(value.tris, getTrisLength(value));
                visitor(*this, value.tris, getTrisLength(value));
            }

            template <class Visitor, class T>
            auto operator()(Visitor&& visitor, T& value) const
                -> std::enable_if_t<std::is_same_v<std::decay_t<T>, PreparedNavMeshData>>
            {
                if constexpr (mode == Serialization::Mode::Write)
                {
                    visitor(*this, DetourNavigator::preparedNavMeshDataMagic);
                    visitor(*this, DetourNavigator::preparedNavMeshDataVersion);
                }
                else
                {
                    static_assert(mode == Serialization::Mode::Read);
                    char magic[std::size(DetourNavigator::preparedNavMeshDataMagic)];
                    visitor(*this, magic);
                    if (std::memcmp(magic, DetourNavigator::preparedNavMeshDataMagic, sizeof(magic)) != 0)
                        throw std::runtime_error("Bad PreparedNavMeshData magic");
                    std::uint32_t version = 0;
                    visitor(*this, version);
                    if (version != DetourNavigator::preparedNavMeshDataVersion)
                        throw std::runtime_error("Bad PreparedNavMeshData version");
                }
                visitor(*this, value.mUserId);
                visitor(*this, value.mCellSize);
                visitor(*this, value.mCellHeight);
                visitor(*this, value.mPolyMesh);
                visitor(*this, value.mPolyMeshDetail);
            }
        };
    }
} // namespace DetourNavigator

namespace DetourNavigator
{
    std::vector<std::byte> serialize(const PreparedNavMeshData& value)
    {
        constexpr Format<Serialization::Mode::Write> format;
        Serialization::SizeAccumulator sizeAccumulator;
        format(sizeAccumulator, value);
        std::vector<std::byte> result(sizeAccumulator.value());
        format(Serialization::BinaryWriter(result.data(), result.data() + result.size()), value);
        return result;
    }

    bool deserialize(const std::vector<std::byte>& data, PreparedNavMeshData& value)
    {
        try
        {
            constexpr Format<Serialization::Mode::Read> format;
            format(Serialization::BinaryReader(data.data(), data.data() + data.size()), value);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}
