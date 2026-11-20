#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <components/rtx/common/index.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace Rtx::Testing
{
    /// Lets go of a row nothing else holds, as its last holder would: a row a test added arrives
    /// with no holds, and the drop after which nothing holds it is the one way it goes.
    inline void letGoMesh(SceneDesc& scene, MeshIndex mesh)
    {
        scene.drop(scene.holdMesh(mesh, HoldTier::Shown));
    }

    inline void letGoMaterial(SceneDesc& scene, MaterialIndex material)
    {
        scene.drop(scene.holdMaterial(material, HoldTier::Shown));
    }

    /// Holds a test takes on its scene's rows, given back when the test ends however it ends. An
    /// `ASSERT` that returns early would otherwise leave a hold to die undropped, which asserts; a
    /// holder the test declares after its scene goes before the scene does.
    class SceneHolds
    {
    public:
        explicit SceneHolds(SceneDesc& scene)
            : mScene(scene)
        {
        }

        ~SceneHolds()
        {
            for (MeshHold& hold : mMeshes)
                mScene.drop(std::move(hold));
            for (MaterialHold& hold : mMaterials)
                mScene.drop(std::move(hold));
            for (TextureHold& hold : mTextures)
                mScene.drop(std::move(hold));
        }

        SceneHolds(const SceneHolds&) = delete;
        SceneHolds& operator=(const SceneHolds&) = delete;

        /// One hold more on each, answering the row, as a walk's identity takes one.
        MeshIndex mesh(MeshIndex mesh) { return take(mMeshes, mScene.holdMesh(mesh, HoldTier::Shown)); }
        MaterialIndex material(MaterialIndex material)
        {
            return take(mMaterials, mScene.holdMaterial(material, HoldTier::Shown));
        }
        TextureIndex texture(TextureIndex texture)
        {
            return take(mTextures, mScene.holdTexture(texture, HoldTier::Shown));
        }

        /// Gives back the hold this took on a row, as its holder letting go would.
        void dropMesh(MeshIndex mesh) { give(mMeshes, mesh); }
        void dropMaterial(MaterialIndex material) { give(mMaterials, material); }
        void dropTexture(TextureIndex texture) { give(mTextures, texture); }

    private:
        template <class Row, class I>
        static I take(std::vector<Hold<Row, I>>& holds, Hold<Row, I>&& hold)
        {
            const I row = hold.get();
            holds.push_back(std::move(hold));
            return row;
        }

        template <class Row, class I>
        void give(std::vector<Hold<Row, I>>& holds, I row)
        {
            const auto held = std::find_if(
                holds.begin(), holds.end(), [row](const Hold<Row, I>& hold) { return hold.get() == row; });
            if (held != holds.end())
                mScene.drop(std::move(*held));
        }

        SceneDesc& mScene;
        std::vector<MeshHold> mMeshes;
        std::vector<MaterialHold> mMaterials;
        std::vector<TextureHold> mTextures;
    };
}
