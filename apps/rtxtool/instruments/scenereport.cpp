#include "scenereport.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/formatcensus.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/placementtable.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>

#include "digest.hpp"
#include "scenedigest.hpp"

namespace RtxTool
{
    std::string describeScene(const Rtx::SceneDesc& scene, const MWRender::WalkReport& walked)
    {
        const Rtx::ExtractionStats& stats = walked.mFound;
        std::string out;

        out
            += (std::format("\nplaced\n"
                            "  instances:            {}\n"
                            "  distant statics:      {}\n"
                            "  ground cells:         {}\n"
                            "  groundcover:          {}\n"
                            "  meshes:               {}\n"
                            "  materials:            {}\n"
                            "  textures:             {}\n"
                            "  triangles:            {}\n"
                            "  vertex+index bytes:   {} KiB, of which vertex colours {} KiB\n"
                            "  handed over:          {}\n"
                            "  laid out as:          {}\n",
                scene.placements().getCounts().mPlaced, stats.mDistantStatics, stats.mGroundCells, stats.mGroundcover,
                scene.meshes().getRows().size(), scene.materials().getRows().size(), scene.textures().getRows().size(),
                scene.meshes().getTriangleCount(), scene.meshes().getGeometryBytes() / 1024,
                scene.meshes().getColours().size() * sizeof(Rtx::Shaders::GpuVertexColour) / 1024,
                spellHash(digestScene(scene)), spellHash(digestLayout(digestParts(scene)))));

        // The images the standing slots keep, off the table that keeps them.
        const Rtx::FormatCensus& formats = scene.textures().getFormats();
        for (std::size_t at = 0; at < formats.mMet.size(); ++at)
        {
            const Rtx::FormatCount& count = formats.mMet[at];
            const auto format = static_cast<Rtx::TextureFormat>(at);

            if (count.mMipped > 0)
                out += (std::format("  {} x {}, with mips\n", count.mMipped, Rtx::nameOf(format)));
            if (count.mMet > count.mMipped)
                out += (std::format("  {} x {}, one level\n", count.mMet - count.mMipped, Rtx::nameOf(format)));
            if (count.mMet > 0 && format == Rtx::TextureFormat::Unnamed)
                out += (std::format("    which was pixel format {}\n", formats.mUnnamed));
        }

        // Which materials traversal will have to stop and ask about, which of those asked for it outright, and which
        // of them a cutoff cannot answer for at all. The second and third being the small ones is the point:
        // Morrowind keeps its foliage under `NiAlphaProperty` rather than under an alpha test, and almost nothing it
        // ships is translucent in its own right. Counted off the scene and not off a walk's own account: a walk
        // reports what that walk met, and a chunk flattened once is nought in every walk after it.
        std::uint32_t cutouts = 0;
        std::uint32_t tested = 0;
        std::uint32_t translucent = 0;
        std::uint32_t media = 0;
        std::uint32_t glowing = 0;
        std::uint32_t flattened = 0;
        std::uint32_t normalMapped = 0;
        std::uint32_t specularMapped = 0;
        const auto glows = [](const Rtx::Material& material) {
            return material.mEmissiveColour.length2() > 0.0f || material.mEmissive != Rtx::sNoIndex;
        };
        for (const Rtx::Material& material : scene.materials().getRows())
        {
            cutouts += material.isCutout() ? 1 : 0;
            tested += material.mAlphaMode == Rtx::AlphaMode::Cutout ? 1 : 0;
            translucent += material.isTranslucent() ? 1 : 0;
            media += material.isMedium() ? 1 : 0;
            glowing += glows(material) ? 1 : 0;
            flattened += material.mFlatten == Rtx::GroundFlattening::Every ? 1 : 0;
            normalMapped += material.mNormal != Rtx::sNoIndex ? 1 : 0;
            specularMapped += material.mSpecular != Rtx::sNoIndex ? 1 : 0;
        }

        // The models of lights that give light, and which of them glow: what the bounce takes no glow
        // from, so a run says what the rule caught.
        std::uint32_t lampBodies = 0;
        std::uint32_t glowingBodies = 0;
        std::uint32_t ambientApart = 0;
        std::uint32_t seeThrough = 0;
        scene.placements().forEachPlaced([&](Rtx::Index, const Rtx::PlacementRow& row) {
            const Rtx::Index worn = row.mInstance.mMaterial;
            ambientApart += worn != Rtx::sNoIndex && scene.materials().getRows()[worn].hasAmbientApart() ? 1 : 0;
            seeThrough += row.mWorn.placedAt(row.mInstance.mOpacity).mTranslucent ? 1 : 0;
            if (!row.mInstance.mLampBody)
                return;
            ++lampBodies;
            const Rtx::Index material = row.mInstance.mMaterial;
            glowingBodies += material != Rtx::sNoIndex && glows(scene.materials().getRows()[material]) ? 1 : 0;
        });

        std::uint32_t sheets = 0;
        std::uint32_t pocketed = 0;
        std::uint32_t tangentMeshes = 0;
        for (const Rtx::MeshRange& mesh : scene.meshes().getRows())
        {
            sheets += mesh.mShape.mSheet ? 1 : 0;
            pocketed += mesh.mShape.mPocketed ? 1 : 0;
            tangentMeshes += std::ranges::any_of(mesh.mVertices.in(scene.meshes().getTangents()),
                                 [](const std::uint32_t word) { return word != 0; })
                ? 1
                : 0;
        }

        out
            += (std::format("  cutout materials:     {}, {} of them alpha-tested outright\n"
                            "  translucent:          {}, which a cutoff cannot answer for\n"
                            "  media:                {} of those are nowhere opaque\n"
                            "  emissive materials:   {}\n"
                            "  lamp bodies:          {} placements of a light's own model, {} of them glowing\n"
                            "  ambient apart:        {} placements, see-through {}\n"
                            "  companion maps:       {} materials wear a normal map, {} a specular map; {} meshes "
                            "carry tangents\n"
                            "  lights:               {} casting\n"
                            "  deforming drawables:  {}\n"
                            "  flattened ground:     {} cells outside the active grid\n"
                            "  emitters:             {} holding {} live particles\n",
                cutouts, tested, translucent, media, glowing, lampBodies, glowingBodies, ambientApart, seeThrough,
                normalMapped, specularMapped, tangentMeshes, scene.lights().size(), stats.mDeformed, flattened,
                stats.mEmitters, stats.mSprites));

        out
            += (std::format("\nnot placed\n"
                            "  unreadable drawables: {}\n"
                            "  unskinned rigs:       {} met before an update found their skeleton\n"
                            "  empty geometry:       {}\n"
                            "  undescribed surfaces: {} drawn as a default material\n"
                            "  sheets:               {} of the meshes, doubled for their backs\n"
                            "  pockets:              {} of the meshes, a wall of an inside-out gap dropped\n",
                stats.mSkippedUnknown, stats.mUnskinned, stats.mSkippedEmpty, stats.mUndescribedSurfaces, sheets,
                pocketed));

        // Every refusal the scene was handed since it was made, by kind: what the log names one by
        // one, counted where a run is compared.
        const Rtx::Refusals& refused = scene.refusals();
        out += (std::format(
            "\nrefused\n"
            "  meshes:               {}\n"
            "  models:               {}\n"
            "  textures:             {} drawn as a stand-in\n"
            "  sky layers:           {}\n"
            "  moons:                {}\n"
            "  lamps:                {}\n"
            "  emitters:             {} dropped whole\n"
            "  emitters' sprites:    {}\n"
            "  surfaces' state:      {}\n",
            refused.count(Rtx::Refused::Mesh), refused.count(Rtx::Refused::Model), refused.count(Rtx::Refused::Texture),
            refused.count(Rtx::Refused::SkyLayer), refused.count(Rtx::Refused::Moon), refused.count(Rtx::Refused::Lamp),
            refused.count(Rtx::Refused::Emitter), refused.count(Rtx::Refused::Sprites),
            refused.count(Rtx::Refused::Surface)));

        // Since the renderer started and not this frame's, because a place is loaded over the
        // frames before its measured ones: what arriving here has cost, pass by pass and thread by
        // thread, which is the figure a cache is judged against.
        out += ("\npreprocessed since the renderer started\n");
        const Rtx::Preprocessed& preprocessed = walked.mSession;
        for (const auto& [thread, passes] :
            { std::pair{ "frame", &preprocessed.mOnFrame }, std::pair{ "ring", &preprocessed.mOffFrame },
                std::pair{ "chains", &preprocessed.mPrepared }, std::pair{ "loading", &preprocessed.mLoading } })
            for (const auto& [pass, name] : Rtx::sContentPasses.mNames)
            {
                const Rtx::PassStats& counted = passes->at(pass);
                out
                    += (std::format("  {:<8}{:<12} {:>7} asked, {:>7} found, {:>9.1f} ms keying "
                                    "{:>8.1f} MiB, {:>9.1f} ms reading found, {:>9.1f} ms running\n",
                        thread, name, counted.mAsked, counted.mHits, counted.mKeyMs, Rtx::mebibytes(counted.mKeyBytes),
                        counted.mFoundMs, counted.mRunMs));
            }

        if (walked.mStore.has_value())
        {
            const ContentStore::Stats& store = *walked.mStore;
            out
                += (std::format("  store   {:>7} found, {:>7} missed, {:>7} written, {:>7} dropped, "
                                "{:>7} refused, {:>7} stamped, {:>7} stale, {:>7} past the memory's caps\n",
                    store.mFound, store.mMissed, store.mWritten, store.mDropped, store.mRefused, store.mStamped,
                    store.mStale, store.mUnheldInMemory));
        }

        if (walked.mAgain.has_value())
        {
            const Rtx::ExtractionStats& again = *walked.mAgain;
            out += (std::format(
                "\nsecond pass over the same graph\n"
                "  new meshes:           {} (should be 0)\n"
                "  new materials:        {} (should be 0)\n"
                "  drawables resolved:   {} to a known mesh\n"
                "  passed frozen:        {}\n"
                "  stood again:          {} (should be 0)\n",
                again.mMeshesAdded, again.mMaterialsAdded, again.mMeshesReused, again.mPassedFrozen, again.mRestood));
        }

        return out;
    }
}
