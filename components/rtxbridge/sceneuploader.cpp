#include "sceneuploader.hpp"

#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/wavespectrum.hpp>

#include "texturebuilder.hpp"

namespace RtxBridge
{
    bool SceneUploader::recognises(
        const Rtx::Renderer& renderer, const Rtx::SceneDesc& scene, std::uint32_t textures) const
    {
        return mRenderer == &renderer && mScene == &scene && mUploaded == textures;
    }

    SceneUpload SceneUploader::hand(
        Rtx::Renderer& renderer, Rtx::SceneDesc& scene, Resource::ImageManager& images, const Rtx::SeaState& sea)
    {
        const bool mine = recognises(renderer, scene, renderer.getTextureCount());

        // Geometry the walk has not met before has no bottom-level structure and no uploaded
        // texture. **Which is a cell change and a load, not a frame** — a door opening moves
        // instances the walk already knows.
        const bool arrived = !mine || scene.getStructureRevision() != mBuilt;

        // **Grown, or replaced?** `clear` empties every table and starts the indices again, so
        // everything built from them has to be built again; anything short of that is an append, and
        // appending is what keeps a cell boundary from costing a fifth of a second. A renderer this
        // has not built has nothing to append to, whatever it happens to be holding.
        const bool reset = !mine || scene.getResetRevision() != mReset;

        if (!arrived)
        {
            renderer.placeScene(scene, sea);
            return SceneUpload{};
        }

        // Held only across the call: `TextureData` carries spans into this, and both `extendScene`
        // and `setScene` have finished reading them when they return.
        //
        // **Everything on a reset and the arrivals otherwise.** A reset builds the array from
        // nothing, so what it wants is the table in its own order; a frame that grew wants the slots
        // that were written and no others, wherever in the table they sit.
        const SceneTextures textures
            = reset ? SceneTextures(scene, images) : SceneTextures(scene, images, scene.getArrivedTextures());

        SceneUpload done;
        done.mDescribed = textures.getDescriptions().size();
        done.mUnreadable = textures.getUnreadable();

        if (reset)
        {
            renderer.setScene(scene, textures.getDescriptions(), sea);
            mReset = scene.getResetRevision();
            done.mKind = SceneUpload::Kind::Rebuilt;
        }
        else
        {
            renderer.extendScene(scene, textures.getDescriptions(), sea);
            done.mKind = SceneUpload::Kind::Extended;
        }

        scene.clearArrivals();

        mRenderer = &renderer;
        mScene = &scene;
        mUploaded = renderer.getTextureCount();
        mBuilt = scene.getStructureRevision();
        return done;
    }
}
