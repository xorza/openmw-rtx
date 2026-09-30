#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace Rtx::Testing
{
    /// Lets go of a row nothing else holds, as its last holder would: a row a test added arrives
    /// with no holds, and the drop after which nothing holds it is the one way it goes.
    inline void letGoMesh(SceneDesc& scene, Index mesh)
    {
        scene.drop(scene.holdMesh(mesh));
    }

    inline void letGoMaterial(SceneDesc& scene, Index material)
    {
        scene.drop(scene.holdMaterial(material));
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
            mScene.drop(mTextures);
        }

        SceneHolds(const SceneHolds&) = delete;
        SceneHolds& operator=(const SceneHolds&) = delete;

        /// One hold more on each, answering the row, as a walk's identity takes one.
        Index mesh(Index mesh) { return take(mMeshes, mScene.holdMesh(mesh)); }
        Index material(Index material) { return take(mMaterials, mScene.holdMaterial(material)); }
        Index texture(Index texture) { return take(mTextures, mScene.holdTexture(texture)); }

        /// Gives back the hold this took on a row, as its holder letting go would.
        void dropMesh(Index mesh) { give(mMeshes, mesh); }
        void dropMaterial(Index material) { give(mMaterials, material); }
        void dropTexture(Index texture) { give(mTextures, texture); }

    private:
        template <class Row>
        static Index take(std::vector<Hold<Row>>& holds, Hold<Row>&& hold)
        {
            const Index row = hold.get();
            holds.push_back(std::move(hold));
            return row;
        }

        template <class Row>
        void give(std::vector<Hold<Row>>& holds, Index row)
        {
            const auto held
                = std::find_if(holds.begin(), holds.end(), [row](const Hold<Row>& hold) { return hold.get() == row; });
            if (held != holds.end())
                mScene.drop(std::move(*held));
        }

        SceneDesc& mScene;
        std::vector<MeshHold> mMeshes;
        std::vector<MaterialHold> mMaterials;
        std::vector<TextureHold> mTextures;
    };
}
