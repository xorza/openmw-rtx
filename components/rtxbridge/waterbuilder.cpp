#include "waterbuilder.hpp"

#include <array>

#include <osg/BoundingBox>
#include <osg/Matrixf>

#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/rtx/scenedesc.hpp>

namespace RtxBridge
{
    namespace
    {
        /// A unit quad in the xy plane facing up, which is every water surface in the game.
        ///
        /// A unit one rather than a placed one, unlike terrain: water is flat, so every cell's
        /// surface is the *same* geometry moved and scaled. Two triangles is all of it — subdividing
        /// would only matter if the surface displaced, and the waves to come move the normal and not
        /// the point.
        ///
        /// **Appended per call rather than shared**, so two water cells would be two identical
        /// bottom-level structures. Nothing loads two yet. Sharing needs somewhere to keep the index
        /// between calls, which is what `SceneExtractor` is for its meshes and what this would have
        /// to become — worth doing when a second cell arrives and not before.
        Rtx::Index addQuad(Rtx::SceneDesc& scene)
        {
            const std::array positions{
                osg::Vec3f(-0.5f, -0.5f, 0.0f),
                osg::Vec3f(0.5f, -0.5f, 0.0f),
                osg::Vec3f(0.5f, 0.5f, 0.0f),
                osg::Vec3f(-0.5f, 0.5f, 0.0f),
            };
            const std::array normals{
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
                osg::Vec3f(0.0f, 0.0f, 1.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            // No texture coordinates, and none coming: water has no texture, and the waves key off
            // world position, which a hit already knows. Interpolated coordinates could not serve
            // that anyway — the instance scale multiplies the quad and not its coordinates, so the
            // same range would cover a cell and a puddle alike.
            return scene.addMesh(positions, normals, {}, indices);
        }
    }

    void addWater(Rtx::SceneDesc& scene, const ESM::Cell& cell)
    {
        if (!cell.hasWater())
            return;

        osg::Vec3f scale;
        osg::Vec3f centre;

        if (cell.isExterior())
        {
            const auto side = static_cast<float>(Constants::CellSizeInUnits);
            scale = osg::Vec3f(side, side, 1.0f);
            centre = osg::Vec3f((static_cast<float>(cell.getGridX()) + 0.5f) * side,
                (static_cast<float>(cell.getGridY()) + 0.5f) * side, 0.0f);
        }
        else
        {
            const osg::BoundingBoxf bounds = scene.getBounds();
            if (!bounds.valid())
                return;

            scale = osg::Vec3f(bounds.xMax() - bounds.xMin(), bounds.yMax() - bounds.yMin(), 1.0f);
            centre = osg::Vec3f(
                (bounds.xMin() + bounds.xMax()) * 0.5f, (bounds.yMin() + bounds.yMax()) * 0.5f, cell.mWater);
        }

        Rtx::Material material;
        material.mKind = Rtx::MaterialKind::Water;

        scene.addInstance(Rtx::MeshInstance{
            .mTransform = osg::Matrixf::scale(scale) * osg::Matrixf::translate(centre),
            .mMesh = addQuad(scene),
            .mMaterial = scene.addMaterial(material),
        });
    }
}
