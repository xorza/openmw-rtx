#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/GL>
#include <osg/Image>
#include <osg/Vec2f>
#include <osg/Vec2i>
#include <osg/Vec3f>
#include <osg/Vec4ub>
#include <osg/ref_ptr>

#include <components/esm/exteriorcelllocation.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/result.hpp>
#include <components/terrain/defs.hpp>
#include <components/terrain/storage.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx::Testing
{
    /// A land of a few cells with records and a default plane everywhere else, filled the way
    /// `ESMTerrain::Storage` fills a chunk one cell wide at full detail.
    ///
    /// **Heights a test can compute by hand**: a plane of eight units a column and sixteen a row over
    /// every cell with a record. Every such cell is grass on its western half and rock on its eastern,
    /// on the 34 × 34 blend maps `getBlendmaps` builds for Morrowind. The rock is a mod's: a
    /// `_diffusespec` with an `_nh` beside it, as `getLayerInfo` finds them under `auto use terrain
    /// normal maps` and `auto use terrain specular maps`.
    class FakeLand : public Terrain::Storage
    {
    public:
        static constexpr std::int32_t sVerts = ESM::Land::LAND_SIZE;
        static constexpr std::int32_t sTiles = ESM::Land::LAND_TEXTURE_SIZE;
        static constexpr std::int32_t sMaskSide = 2 * (sTiles + 1);
        static constexpr float sCellSize = static_cast<float>(Constants::CellSizeInUnits);

        /// The cells with a land record.
        std::vector<osg::Vec2i> mWithData;

        static float heightAt(const std::int32_t column, const std::int32_t row)
        {
            return 8.0f * static_cast<float>(column) + 16.0f * static_cast<float>(row);
        }

        /// Where the storage puts vertex (`column`, `row`) of a cell, relative to its centre.
        static osg::Vec3f positionAt(const std::int32_t column, const std::int32_t row)
        {
            const auto side = static_cast<float>(sVerts - 1);
            return osg::Vec3f((static_cast<float>(column) / side - 0.5f) * sCellSize,
                (static_cast<float>(row) / side - 0.5f) * sCellSize, heightAt(column, row));
        }

        /// The colour the storage puts on vertex (`column`, `row`), display-encoded as `VCLR` is.
        ///
        /// **Not white, because a white tint is the answer a renderer that dropped it gives.** The
        /// three channels differ from each other so that a test cannot pass on a decode that
        /// swapped them.
        /// Vertex (`column`, `row`)'s colour in `cell`: the blue tells the cells of a sixteen-cell square apart, so
        /// every cell's ground is a mesh of its own, as a world's is, where one geometry is one mesh
        /// (`SceneDesc::addMesh`); the origin's is a full blue.
        static osg::Vec4ub colourAt(
            const std::int32_t column, const std::int32_t row, const osg::Vec2i& cell = osg::Vec2i())
        {
            const auto red = static_cast<std::uint8_t>(64 + column % 8);
            const auto green = static_cast<std::uint8_t>(128 + row % 8);
            const auto blue = static_cast<std::uint8_t>(255 - (((cell.x() & 15) << 4) | (cell.y() & 15)));
            return osg::Vec4ub(red, green, blue, 255);
        }

        bool hasData(const ESM::ExteriorCellLocation cell) override
        {
            return std::find(mWithData.begin(), mWithData.end(), osg::Vec2i(cell.mX, cell.mY)) != mWithData.end();
        }

        void getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId) override
        {
            minX = -100.0f;
            maxX = 100.0f;
            minY = -100.0f;
            maxY = 100.0f;
        }

        bool getMinMaxHeights(
            float, const osg::Vec2f& centre, const ESM::RefId worldspace, float& min, float& max) override
        {
            min = heightAt(0, 0);
            max = heightAt(sVerts - 1, sVerts - 1);
            return hasData(cellOf(centre, worldspace));
        }

        void fillVertexBuffers(const std::int32_t lod, const float size, const osg::Vec2f& centre,
            const ESM::RefId worldspace, osg::Vec3Array& positions, osg::Vec3Array& normals,
            osg::Vec4ubArray& colours) override
        {
            EXPECT_EQ(lod, 0) << "the ring reads every cell at full detail";
            EXPECT_EQ(size, 1.0f) << "and one cell at a time";
            EXPECT_TRUE(hasData(cellOf(centre, worldspace))) << "a cell with no record is not filled";

            positions.resize(static_cast<std::size_t>(sVerts) * sVerts);
            normals.resize(positions.size());
            colours.resize(positions.size());

            const ESM::ExteriorCellLocation location = cellOf(centre, worldspace);
            const osg::Vec2i cell(location.mX, location.mY);
            for (std::int32_t column = 0; column < sVerts; ++column)
                for (std::int32_t row = 0; row < sVerts; ++row)
                {
                    const std::size_t at = static_cast<std::size_t>(column) * sVerts + static_cast<std::size_t>(row);
                    positions[at] = positionAt(column, row);
                    normals[at] = osg::Vec3f(0.0f, 0.0f, 1.0f);
                    colours[at] = colourAt(column, row, cell);
                }
        }

        void getBlendmaps(float, const osg::Vec2f& centre, ImageVector& blendmaps,
            std::vector<Terrain::LayerInfo>& layers, const ESM::RefId worldspace) override
        {
            if (!hasData(cellOf(centre, worldspace)))
            {
                layers.push_back(layerOf("textures/_land_default.dds", "", false, false));
                return;
            }

            layers.push_back(layerOf("textures/grass.dds", "", false, false));
            layers.push_back(layerOf("textures/rock_diffusespec.dds", "textures/rock_nh.dds", true, true));
            blendmaps.push_back(half(true));
            blendmaps.push_back(half(false));
        }

        float getHeightAt(const osg::Vec3f&, ESM::RefId) override { return ESM::Land::DEFAULT_HEIGHT; }
        float getCellWorldSize(ESM::RefId) override { return sCellSize; }
        std::int32_t getCellVertices(ESM::RefId) override { return sVerts; }
        std::int32_t getTextureTileCount(const float chunkSize, ESM::RefId) override
        {
            return static_cast<std::int32_t>(sTiles * chunkSize);
        }

    private:
        static ESM::ExteriorCellLocation cellOf(const osg::Vec2f& centre, const ESM::RefId worldspace)
        {
            return ESM::ExteriorCellLocation(static_cast<std::int32_t>(std::floor(centre.x())),
                static_cast<std::int32_t>(std::floor(centre.y())), worldspace);
        }

        static Terrain::LayerInfo layerOf(
            const char* path, const char* normal, const bool parallax, const bool specular)
        {
            return Terrain::LayerInfo{ VFS::Path::Normalized(path), VFS::Path::Normalized(normal), parallax, specular };
        }

        /// A blend map covering the western or the eastern half of the cell, as `ESMTerrain`
        /// builds one: one byte a texel, rows of `sMaskSide`.
        static osg::ref_ptr<osg::Image> half(const bool western)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(sMaskSide, sMaskSide, 1, GL_ALPHA, GL_UNSIGNED_BYTE);
            std::memset(image->data(), 0, image->getTotalSizeInBytes());

            for (std::int32_t row = 0; row < sMaskSide; ++row)
            {
                std::uint8_t* along = image->data(0, static_cast<std::uint32_t>(row));
                for (std::int32_t column = 0; column < sMaskSide; ++column)
                    along[column] = (column < sMaskSide / 2) == western ? 255 : 0;
            }

            return image;
        }
    };

    /// Images by path, one object per path however often it is asked for — which is how a loader's
    /// cache answers, and what lets a reading made against one be found by the other. A path
    /// `lose` named reads nothing, as a file the archives do not hold.
    class ImagesByPath
    {
    public:
        Result<osg::ref_ptr<const osg::Image>, std::string> get(const VFS::Path::NormalizedView path)
        {
            ++mOpened;
            if (mLost.contains(path.value()))
                return Err{ "no image reads from the file" };

            const auto found = mImages.find(path.value());
            if (found != mImages.end())
                return found->second;

            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setFileName(std::string(path.value()));
            image->allocateImage(4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            std::fill_n(image->data(), image->getTotalSizeInBytes(), static_cast<std::uint8_t>(128));

            mImages.emplace(std::string(path.value()), image);
            return osg::ref_ptr<const osg::Image>(image);
        }

        void lose(std::string_view path) { mLost.emplace(path); }

        /// Answers `path` with `image` from here on, for a file of a format other than RGBA8.
        void put(std::string_view path, osg::ref_ptr<osg::Image> image)
        {
            image->setFileName(std::string(path));
            mImages.insert_or_assign(std::string(path), std::move(image));
        }

        /// How many times a file was asked for, which a reader that opens each once keeps low.
        std::size_t getOpened() const { return mOpened; }

    private:
        std::map<std::string, osg::ref_ptr<osg::Image>, std::less<>> mImages;
        std::set<std::string, std::less<>> mLost;
        std::size_t mOpened = 0;
    };
}
