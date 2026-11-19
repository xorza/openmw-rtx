#include "scenedigest.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Matrixf>

#include <components/misc/hashstate.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/scene/deformertable.hpp>
#include <components/rtx/scene/foldedshape.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/placementtable.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/skinning.h>
#include <components/vfs/pathutil.hpp>

namespace RtxTool
{
    namespace
    {
        /// A digest that no order of its parts can tell: each part's words are added into the whole.
        class Unordered
        {
        public:
            void add(const Misc::HashState& part)
            {
                mWords[0] += part.getWords()[0];
                mWords[1] += part.getWords()[1];
            }

            const Misc::DigestWords& getWords() const { return mWords; }

        private:
            Misc::DigestWords mWords{};
        };

        void addTexture(Misc::HashState& digest, const Rtx::SceneDesc& scene, const Rtx::TextureIndex texture)
        {
            digest.add(texture == Rtx::sNoRow);
            if (texture == Rtx::sNoRow)
                return;

            const Rtx::TextureRow& row = scene.textures().getRows()[texture];
            const std::string_view path = row.mPath.value();
            digest.add(std::span<const char>(path.data(), path.size()));
            digest.add(row.mWrap);

            // Only where it is not a colour, so a scene of colours digests as on record.
            if (row.mEncoding != Rtx::TextureEncoding::Colour)
                digest.add(row.mEncoding);
        }

        /// Hands every field of `material` to one of three callables. One list serves both digests, which spell a
        /// texture and a layer run differently: `digestParts` takes the slot and the run's offset (which table a
        /// material points into is what a layout is), `digestContent` the file named and the run's length alone. A
        /// field added to `Material` and not named here does not compile, which the two hand-kept lists it replaced
        /// did not guarantee; `sCounters` in `extractionstats.cpp` argues the same.
        template <class Texture, class Layers, class Value>
        void forEachMaterialField(const Rtx::Material& material, Texture texture, Layers layers, Value value)
        {
            const auto& [kind, diffuse, emissive, emissiveUnit, environment, environmentColour, dark, darkUnit, normal,
                specular, detail, detailUnit, decal, decalUnit, bump, bumpUnit, bumpMatrix, bumpLuma, gloss, glossUnit,
                relief, diffuseColour, ambientColour, emissiveColour, opacity, alphaTest, alphaMode, blend,
                vertexColour, twoSided, lampLit, groundcover, textureTransform, run, flatten, layersMapped, animated,
                neverSolid, diffuseMean] = material;

            texture(diffuse);
            texture(emissive);
            texture(environment);
            texture(dark);

            // Companion maps only where there are any, each behind its own tag, so a scene with none digests as on
            // record.
            if (normal != Rtx::sNoRow)
            {
                value(std::uint8_t{ 1 });
                texture(normal);
            }
            if (specular != Rtx::sNoRow)
            {
                value(std::uint8_t{ 2 });
                texture(specular);
            }
            if (layersMapped)
                value(std::uint8_t{ 3 });
            if (relief.has_value())
            {
                value(std::uint8_t{ 4 });
                value(relief->mLow);
                value(relief->mHigh);
            }
            if (emissiveUnit != 0)
            {
                value(std::uint8_t{ 5 });
                value(emissiveUnit);
            }
            if (!lampLit)
                value(std::uint8_t{ 7 });
            if (groundcover)
                value(std::uint8_t{ 13 });

            // The maps a mod's surfaces lay in beside the diffuse, each behind its own tag, as the companions are.
            if (detail != Rtx::sNoRow)
            {
                value(std::uint8_t{ 9 });
                texture(detail);
                value(detailUnit);
            }
            if (decal != Rtx::sNoRow)
            {
                value(std::uint8_t{ 10 });
                texture(decal);
                value(decalUnit);
            }
            if (bump != Rtx::sNoRow)
            {
                value(std::uint8_t{ 11 });
                texture(bump);
                value(bumpUnit);
                value(bumpMatrix);
                value(bumpLuma);
            }
            if (gloss != Rtx::sNoRow)
            {
                value(std::uint8_t{ 12 });
                texture(gloss);
                value(glossUnit);
            }

            layers(run);

            value(kind);
            value(environmentColour);
            value(darkUnit);
            value(diffuseColour);
            value(material.getAmbientColour());
            value(emissiveColour);
            value(opacity);
            value(alphaTest);
            value(alphaMode);
            value(blend);
            value(vertexColour);
            value(twoSided);
            value(textureTransform);
            value(flatten);
            value(animated);
            value(neverSolid);
            value(diffuseMean);
        }

        void addMaterial(Misc::HashState& digest, const Rtx::SceneDesc& scene, const Rtx::MaterialIndex index)
        {
            digest.add(index == Rtx::sNoRow);
            if (index == Rtx::sNoRow)
                return;

            const Rtx::Material& material = scene.materials().getRows()[index];
            forEachMaterialField(
                material, [&](const Rtx::TextureIndex slot) { addTexture(digest, scene, slot); },
                [&](const Rtx::Run& layers) { digest.add(layers.mCount); },
                [&](const auto& field) { digest.add(field); });

            for (const Rtx::MaterialLayer& layer : material.mLayers.in(scene.materials().getLayers()))
            {
                addTexture(digest, scene, Rtx::TextureIndex(layer.mDiffuse));
                digest.add(layer.mDiffuseTransform);
                digest.add(layer.mMaskTransform);
                // A layer that covers its chunk whole has no mask.
                const Rtx::Run mask = Rtx::maskOf(layer);
                digest.add(mask.empty() ? Misc::DigestWords{} : scene.materials().getMaskDigest(mask));

                // Only where a layer has them, each behind its tag, as a material's companion maps.
                if (layer.mNormal != Rtx::Shaders::NO_TEXTURE)
                {
                    digest.add(std::uint8_t{ 1 });
                    addTexture(digest, scene, Rtx::TextureIndex(layer.mNormal));
                }
                if (layer.mFlags != 0)
                {
                    digest.add(std::uint8_t{ 2 });
                    digest.add(layer.mFlags);
                }
            }
        }

        /// Every field of one row, as one list: `digestParts` reads these whole, so a field not named would escape
        /// the gate, and one added and not named here does not compile. Whether the mesh brought tangents is left
        /// out; the meshes column adds it where set, so a scene with none digests to the words on record. So are its
        /// key, the key of the indices it shares and its UV density, which its vertices and indices make and their
        /// columns digest, and its place in the rest, which no frame reads; whether it rests and what it stands are
        /// digested.
        auto fieldsOf(const Rtx::MeshRange& mesh)
        {
            const auto& [vertices, indices, sharedIndexKey, secondTexCoords, unitStreams, tangents, shape, deformer,
                bindOffset, poseOffset, posed, bounds, uvDensity, key, stoodBytes, rest] = mesh;
            return std::tie(vertices, indices, secondTexCoords, unitStreams, shape, deformer, bindOffset, poseOffset,
                posed, bounds, stoodBytes, rest.mResting);
        }

        auto fieldsOf(const Rtx::MeshInstance& instance)
        {
            const auto& [transform, mesh, material, opacity, instanceClass, clockwise, lampBody, stander] = instance;
            return std::tie(transform, mesh, material, opacity, instanceClass, clockwise, lampBody, stander);
        }

        /// Field by field, because the kind is a byte and the row carries padding after it.
        auto fieldsOf(const Rtx::Deformer& deformer)
        {
            const auto& [kind, runs, influences, offsets, rows] = deformer;
            return std::tie(kind, runs, influences, offsets, rows);
        }

        void addFields(Misc::HashState& digest, const auto& fields)
        {
            std::apply([&digest](const auto&... field) { (digest.add(field), ...); }, fields);
        }

        /// One table hashed where it lies.
        template <class T>
        Misc::DigestWords wordsOf(const std::span<const T> table)
        {
            Misc::HashState whole;
            whole.add(table);
            return whole.getWords();
        }

        /// A column of rows: every field of every row laid end to end in one buffer, so the column
        /// is one hash over its bytes and not one hash per field.
        class Column
        {
        public:
            explicit Column(std::vector<std::byte>& scratch)
                : mScratch(scratch)
            {
                mScratch.clear();
            }

            template <class T>
            void add(const T& value)
            {
                const auto bytes = std::as_bytes(std::span<const T>(&value, 1));
                mScratch.insert(mScratch.end(), bytes.begin(), bytes.end());
            }

            template <class T>
            void add(const std::span<const T> values)
            {
                const auto bytes = std::as_bytes(values);
                mScratch.insert(mScratch.end(), bytes.begin(), bytes.end());
            }

            void addFields(const auto& fields)
            {
                std::apply([&](const auto&... field) { (add(field), ...); }, fields);
            }

            /// The column's digest, over everything added.
            Misc::DigestWords take() const { return wordsOf(std::span<const std::byte>(mScratch)); }

        private:
            std::vector<std::byte>& mScratch;
        };

        /// A mesh's shape: the multiset of its triangles, and what deforms it where something does: its deformer's
        /// kind, rows, runs, influences and morph offsets, which the device poses it by.
        Misc::DigestWords shapeOf(const Rtx::SceneDesc& scene, const Rtx::MeshIndex at)
        {
            const Rtx::MeshRange& mesh = scene.meshes().getRows()[at];
            Misc::HashState shape;
            shape.add(mesh.mVertices.empty() ? Misc::DigestWords{} : scene.meshes().getDigest(at).mTriangles);
            const Rtx::DeformerTable& deformers = scene.deformers();
            shape.add(deformers.kindOf(mesh));
            if (mesh.deforms())
            {
                const Rtx::Deformer& deformer = deformers.getRows()[mesh.mDeformer];
                shape.add(deformer.mRows);
                shape.add(deformer.mRuns.in(deformers.getRuns()));
                shape.add(deformer.mInfluences.in(deformers.getInfluences()));
                shape.add(deformer.mOffsets.in(deformers.getMorphOffsets()));
            }
            return shape.getWords();
        }
    }

    void SceneDigester::digestContent(const Rtx::SceneDesc& scene)
    {
        const auto meshes = scene.meshes().getRows();
        if (mShapesRevision != scene.meshes().getRevision() || mShapes.size() != meshes.size())
        {
            mShapes.resize(meshes.size());
            for (const Rtx::MeshIndex mesh : Rtx::everyRow<Rtx::MeshIndex>(meshes.size()))
                mShapes[mesh] = shapeOf(scene, mesh);
            mShapesRevision = scene.meshes().getRevision();
        }

        // Every record bound whole, as `fieldsOf` binds the layout's, so a field added and not named here does not
        // compile. What names a slot is read through it, or left out where the record's place says it already.
        Unordered whole;

        for (const Rtx::PlacementRow& row : scene.placements().getRows())
        {
            // The worn traits are the material's, read through it; the links chain slots, which are the layout.
            const auto& [instance, previous, worn, wearing, placing] = row;

            // Who stood it says who may move it, which no frame draws.
            const auto& [transform, mesh, material, opacity, instanceClass, clockwise, lampBody, stander] = instance;
            if (mesh == Rtx::sNoRow)
                continue;

            Misc::HashState placement;
            placement.add(std::span<const float>(transform.ptr(), 16));
            placement.add(std::span<const float>(previous.ptr(), 16));
            placement.add(opacity);
            placement.add(instanceClass);
            placement.add(clockwise);
            placement.add(lampBody);
            addMaterial(placement, scene, material);
            placement.add(mShapes[mesh]);
            const Rtx::MeshRange& range = meshes[mesh];
            if (range.deforms())
                placement.add(scene.deformers().getMeshPose(range));
            whole.add(placement);
        }

        // A lamp names no slot, so its record is its content.
        for (const Rtx::Light& light : scene.lights())
        {
            Misc::HashState lamp;
            lamp.add(light);
            whole.add(lamp);
        }

        for (const Rtx::SpriteEmitter& emitter : scene.emitters())
        {
            // The run is the sprites hashed in it, in the order the emitter placed them.
            const auto& [centre, reach, first, count, texture, flags, width, lighting] = emitter;
            Misc::HashState plume;
            plume.add(centre);
            plume.add(reach);
            plume.add(flags);
            plume.add(width);
            addTexture(plume, scene, Rtx::TextureIndex(texture));
            addTexture(plume, scene, Rtx::TextureIndex(lighting));
            for (const Rtx::Sprite& sprite : Rtx::spritesOf(emitter).in(scene.sprites()))
            {
                // Its emitter's slot is the run it stands in; the layers are the device's, nought on the host.
                const auto& [position, radius, axis, colour, alpha, emitterSlot, sunLayers, skyLayers] = sprite;
                plume.add(position);
                plume.add(radius);
                plume.add(axis);
                plume.add(colour);
                plume.add(alpha);
            }
            whole.add(plume);
        }

        for (const Rtx::RippleImpulse& impulse : scene.ripples())
        {
            const auto& [at, size] = impulse;
            Misc::HashState ripple;
            ripple.add(at);
            ripple.add(size);
            whole.add(ripple);
        }

        take(ScenePart::Content, whole.getWords());
    }

    /// The tables `digestParts` reads whole, held to having nothing between their fields: the bytes a record pads
    /// with are whatever the allocator left, so two identical runs would differ once, unrepeatably.
    static_assert(sizeof(Rtx::Light) == 40, "Rtx::Light is read whole and must have no padding");
    static_assert(sizeof(Rtx::Sprite) == 56, "Rtx::Sprite is read whole and must have no padding");
    static_assert(sizeof(Rtx::SpriteEmitter) == 40, "Rtx::SpriteEmitter is read whole and must have no padding");
    static_assert(sizeof(Rtx::Shaders::GpuBone) == 48, "GpuBone is read whole and must have no padding");
    static_assert(sizeof(Rtx::Shaders::GpuInfluence) == 8, "GpuInfluence is read whole and must have no padding");

    /// The same, for the field types the lists above hand over as one value each.
    static_assert(sizeof(Rtx::Run) == 8, "Rtx::Run is read whole and must have no padding");
    static_assert(sizeof(Rtx::FoldedShape) == 3, "Rtx::FoldedShape is read whole and must have no padding");
    static_assert(sizeof(osg::BoundingBoxf) == 24, "a bounding box is read whole and must have no padding");
    static_assert(sizeof(osg::Matrixf) == 64, "a transform is read whole and must have no padding");

    void SceneDigester::digestVertices(const Rtx::SceneDesc& scene)
    {
        // **Each stream as the meshes standing in it digested as they arrived** (`Rtx::MeshTable::keepDigests`), by
        // slot: the host keeps no geometry past its hand-over. A resting mesh stands in its runs too; a slot given
        // back holds nothing a frame reads.
        const Rtx::MeshTable& meshes = scene.meshes();
        assert(meshes.keepsDigests() && "a scene digested whose meshes keep no digest");
        const auto rows = meshes.getRows();
        const auto column = [&](const ScenePart part, Misc::DigestWords Rtx::MeshDigest::* const stream) {
            Column digests(mScratch);
            for (const Rtx::MeshIndex at : Rtx::everyRow<Rtx::MeshIndex>(rows.size()))
                if (!rows[at].mVertices.empty())
                    digests.addFields(std::tuple(at.at(), meshes.getDigest(at).*stream));
            take(part, digests.take());
        };
        column(ScenePart::Positions, &Rtx::MeshDigest::mPositions);
        column(ScenePart::Normals, &Rtx::MeshDigest::mNormals);
        column(ScenePart::TexCoords, &Rtx::MeshDigest::mTexCoords);
        column(ScenePart::Colours, &Rtx::MeshDigest::mColours);
        column(ScenePart::Indices, &Rtx::MeshDigest::mIndices);
    }

    const ScenePartDigests& SceneDigester::digest(
        const Rtx::SceneDesc& scene, const Rtx::Shaders::VisibilityConstants* frame)
    {
        digestVertices(scene);

        // Every slot, standing or free: a free one keeps the room and offsets its last occupant left, which a run
        // has to repeat.
        Column meshes(mScratch);
        for (const Rtx::MeshRange& mesh : scene.meshes().getRows())
        {
            meshes.addFields(fieldsOf(mesh));
            if (mesh.mTangents)
                meshes.add(mesh.mTangents);
        }
        take(ScenePart::Meshes, meshes.take());

        Column instances(mScratch);
        for (const Rtx::PlacementRow& row : scene.placements().getRows())
            instances.addFields(fieldsOf(row.mInstance));
        take(ScenePart::Instances, instances.take());

        Column previous(mScratch);
        for (const Rtx::PlacementRow& row : scene.placements().getRows())
            previous.add(row.mPrevious);
        take(ScenePart::Previous, previous.take());

        // The slot a texture landed in and the offset a layer run was placed at, because that is
        // what this digest is for: which table a material points into is what a layout is.
        Column materials(mScratch);
        for (const Rtx::Material& material : scene.materials().getRows())
            forEachMaterialField(
                material, [&](const Rtx::TextureIndex slot) { materials.add(slot.at()); },
                [&](const Rtx::Run& layers) { materials.add(layers); },
                [&](const auto& field) { materials.add(field); });
        take(ScenePart::Materials, materials.take());

        // Each row as its first 48 bytes, and the normal and flags only where a layer sets them, so a scene with no
        // map digests to the words on record. Bound whole, so a field not named here does not compile.
        Column layers(mScratch);
        for (const Rtx::MaterialLayer& layer : scene.materials().getLayers())
        {
            const auto& [diffuse, maskOffset, maskWidth, maskHeight, diffuseTransform, maskTransform, normal, flags,
                padding] = layer;
            layers.addFields(std::tie(diffuse, maskOffset, maskWidth, maskHeight, diffuseTransform, maskTransform));
            if (normal != Rtx::Shaders::NO_TEXTURE)
                layers.addFields(std::tuple(std::uint8_t{ 1 }, normal));
            if (flags != 0)
                layers.addFields(std::tuple(std::uint8_t{ 2 }, flags));
        }
        take(ScenePart::Layers, layers.take());
        // Each standing mask's digest from its arrival, by where it stands, since the host keeps no weights past a
        // hand-over.
        mMaskOffsets.clear();
        for (const auto& [offset, words] : scene.materials().getMaskDigests())
            mMaskOffsets.push_back(offset);
        std::ranges::sort(mMaskOffsets);
        Column masks(mScratch);
        for (const std::uint32_t offset : mMaskOffsets)
            masks.addFields(std::tuple(offset, scene.materials().getMaskDigests().at(offset)));
        take(ScenePart::Masks, masks.take());

        // By names and slots both, unlike the content: which slot a texture landed in is what a material's index
        // means. A file and a sprite's bake name their file, a ground its material; the kind tells them apart, and
        // each path ends with its length so two laid end to end cannot read as one. Every other field is bound whole,
        // as `forEachMaterialField` does: the wrap and encoding reach the sampler and the decode. The image and
        // partner are files' bytes, which the path names; of what the device stands its bytes, the level it stands
        // from, and whether it is the chain made again, which moves every level under the finest. Its asks, its last
        // want and what it rests above
        // it are left out: they are the placements' and the ring's distances, and the level they make stood is
        // digested where it lands.
        Column textures(mScratch);
        for (const Rtx::TextureRow& row : scene.textures().getRows())
        {
            const auto& [kind, path, groundOf, wrap, encoding, image, partner, format, stoodBytes, stoodMade, stoodFrom,
                rest, asks, lastWant, restAbove] = row;
            const auto& [resting, waitingAt, restedBefore, restedAfter, filedAt, order, fit, fitAlignment, fitBefore,
                fitAfter] = rest;
            const std::string_view name = path.value();
            textures.add(std::span<const char>(name.data(), name.size()));
            textures.add(static_cast<std::uint32_t>(name.size()));

            textures.add(static_cast<std::uint32_t>(kind));
            textures.add(groundOf.at());
            textures.add(static_cast<std::uint32_t>(wrap));
            textures.add(static_cast<std::uint32_t>(encoding));
            textures.add(static_cast<std::uint32_t>(format));
            textures.add(stoodBytes);
            textures.add(static_cast<std::uint32_t>(stoodMade ? 1 : 0));
            textures.add(static_cast<std::uint32_t>(stoodFrom));
            textures.add(static_cast<std::uint32_t>(resting ? 1 : 0));
            textures.add(static_cast<std::uint32_t>(waitingAt));
            textures.add(restedBefore.at());
            textures.add(restedAfter.at());
            textures.add(filedAt);
            textures.add(order);
            static_cast<void>(image);
            static_cast<void>(partner);
            // What the device says a slot gives back once it rests, and its class's neighbours by it, are its answers,
            // landing whenever it answers: a want's eviction reads them, and the scene it leaves is what is bound.
            static_cast<void>(fit);
            static_cast<void>(fitAlignment);
            static_cast<void>(fitBefore);
            static_cast<void>(fitAfter);
        }
        take(ScenePart::Textures, textures.take());

        take(ScenePart::Lights, wordsOf(scene.lights()));
        take(ScenePart::Sprites, wordsOf(scene.sprites()));
        take(ScenePart::Emitters, wordsOf(scene.emitters()));

        Column ripples(mScratch);
        for (const Rtx::RippleImpulse& impulse : scene.ripples())
        {
            ripples.add(impulse.mAt);
            ripples.add(impulse.mSize);
        }
        take(ScenePart::Ripples, ripples.take());

        // What poses a mesh that deforms, and the pose itself. The trace reads the posed vertices,
        // which live on the device and nowhere here, so these are what stands for them.
        Column deformers(mScratch);
        for (const Rtx::Deformer& deformer : scene.deformers().getRows())
            deformers.addFields(fieldsOf(deformer));
        deformers.add(scene.deformers().getRuns());
        deformers.add(scene.deformers().getInfluences());
        deformers.add(scene.deformers().getMorphOffsets());
        take(ScenePart::Deformers, deformers.take());

        take(ScenePart::Poses, wordsOf(scene.deformers().getPoses()));

        digestContent(scene);

        // Whole, because the block is scalar-packed on every side, which `visibility.h` pins.
        take(ScenePart::Frame,
            frame != nullptr ? wordsOf(std::span<const Rtx::Shaders::VisibilityConstants>(frame, 1))
                             : Misc::DigestWords{});

        return mParts;
    }

    ScenePartDigests digestParts(const Rtx::SceneDesc& scene)
    {
        SceneDigester once;
        return once.digest(scene);
    }

    Misc::DigestWords digestScene(const ScenePartDigests& parts)
    {
        return parts[static_cast<std::size_t>(ScenePart::Content)];
    }

    Misc::DigestWords digestLayout(const ScenePartDigests& parts)
    {
        Misc::HashState whole;
        for (std::size_t part = 0; part < parts.size(); ++part)
            if (part != static_cast<std::size_t>(ScenePart::Content))
                whole.add(parts[part]);

        return whole.getWords();
    }
}
