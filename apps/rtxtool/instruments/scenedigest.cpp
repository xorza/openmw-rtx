#include "scenedigest.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/common/hashstate.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/scene/deformertable.hpp>
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
            void add(const Rtx::HashState& part)
            {
                mWords[0] += part.getWords()[0];
                mWords[1] += part.getWords()[1];
            }

            const Rtx::DigestWords& getWords() const { return mWords; }

        private:
            Rtx::DigestWords mWords{};
        };

        void addTexture(Rtx::HashState& digest, const Rtx::SceneDesc& scene, const Rtx::Index texture)
        {
            digest.add(texture == Rtx::sNoIndex);
            if (texture == Rtx::sNoIndex)
                return;

            const Rtx::TextureRow& row = scene.textures().getRows()[texture];
            const std::string_view path = row.mPath.value();
            digest.add(std::span<const char>(path.data(), path.size()));
            digest.add(row.mWrap);

            // Only where it is not a colour, so a scene of colours digests as it did before a slot had
            // an encoding.
            if (row.mEncoding != Rtx::TextureEncoding::Colour)
                digest.add(row.mEncoding);
        }

        /// Hands every field of `material` to one of three callables.
        ///
        /// **One list and three kinds, because the two digests spell two of the kinds
        /// differently.** A texture reaches `digestParts` as the slot it landed in, which is what a
        /// material's index means, and reaches `digestScene` as the file it names, which is what
        /// the same material is wherever the slots fell. A run reaches the first whole, offset
        /// included, and the second by its length alone — an offset is where a chunk's layers were
        /// put and not what they are.
        ///
        /// **A field added to `Material` and not named here does not compile**, which is the whole
        /// of why this exists: the two lists it replaces were kept by hand, held different subsets,
        /// and a field added to neither would have left the gate quietly.
        /// `sCounters` in `extractionstats.cpp` makes the same argument for the same reason.
        template <class Texture, class Layers, class Value>
        void forEachMaterialField(const Rtx::Material& material, Texture texture, Layers layers, Value value)
        {
            const auto& [kind, diffuse, emissive, emissiveUnit, environment, environmentColour, dark, darkUnit, normal,
                specular, specularClassic, parallax, diffuseColour, ambientColour, emissiveColour, opacity, alphaTest,
                alphaMode, blend, vertexColour, twoSided, lampLit, textureTransform, run, flatten, layersMapped,
                animated, neverSolid, diffuseMean]
                = material;

            texture(diffuse);
            texture(emissive);
            texture(environment);
            texture(dark);

            // The companion maps only where there are any, each behind its own tag, so a scene with
            // none digests as it did before a material could have them.
            if (normal != Rtx::sNoIndex)
            {
                value(std::uint8_t{ 1 });
                texture(normal);
            }
            if (specular != Rtx::sNoIndex)
            {
                value(std::uint8_t{ 2 });
                texture(specular);
            }
            if (layersMapped)
                value(std::uint8_t{ 3 });
            if (parallax)
                value(std::uint8_t{ 4 });
            if (emissiveUnit != 0)
            {
                value(std::uint8_t{ 5 });
                value(emissiveUnit);
            }
            if (specularClassic)
                value(std::uint8_t{ 6 });
            if (!lampLit)
                value(std::uint8_t{ 7 });

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

        void addMaterial(Rtx::HashState& digest, const Rtx::SceneDesc& scene, const Rtx::Index index)
        {
            digest.add(index == Rtx::sNoIndex);
            if (index == Rtx::sNoIndex)
                return;

            const Rtx::Material& material = scene.materials().getRows()[index];
            forEachMaterialField(
                material, [&](const Rtx::Index slot) { addTexture(digest, scene, slot); },
                [&](const Rtx::Run& layers) { digest.add(layers.mCount); },
                [&](const auto& field) { digest.add(field); });

            for (const Rtx::MaterialLayer& layer : material.mLayers.in(scene.materials().getLayers()))
            {
                addTexture(digest, scene, layer.mDiffuse);
                digest.add(layer.mDiffuseTransform);
                digest.add(layer.mMaskTransform);
                digest.add(Rtx::maskOf(layer).in(scene.materials().getMasks()));

                // Only where a layer has them, each behind its tag, as a material's companion maps.
                if (layer.mNormal != Rtx::sNoIndex)
                {
                    digest.add(std::uint8_t{ 1 });
                    addTexture(digest, scene, layer.mNormal);
                }
                if (layer.mFlags != 0)
                {
                    digest.add(std::uint8_t{ 2 });
                    digest.add(layer.mFlags);
                }
            }
        }

        /// Every field of one row, as one list.
        ///
        /// **A field added and not named here does not compile.** `digestParts` reads these whole,
        /// so a field it did not name would be one the gate stopped watching — silently, and on
        /// the one report every determinism argument in this fork rests on.
        ///
        /// **Whether it brought tangents is the one field left out**, and the meshes column adds it
        /// where it is set: a scene where no mesh has any digests to the words on record.
        auto fieldsOf(const Rtx::MeshRange& mesh)
        {
            const auto& [vertices, indices, secondTexCoords, unitStreams, tangents, shape, deformer, bindOffset,
                poseOffset, posed, bounds]
                = mesh;
            return std::tie(vertices, indices, secondTexCoords, unitStreams, shape, deformer, bindOffset, poseOffset,
                posed, bounds);
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

        void addFields(Rtx::HashState& digest, const auto& fields)
        {
            std::apply([&digest](const auto&... field) { (digest.add(field), ...); }, fields);
        }

        /// One table hashed where it lies.
        template <class T>
        Rtx::DigestWords wordsOf(const std::span<const T> table)
        {
            Rtx::HashState whole;
            whole.add(table);
            return whole.getWords();
        }

        /// A table in blocks hashed where it lies, a block at a time. A table of one block or none
        /// is hashed as the one span it would be laid out flat, so its words are the flat table's;
        /// past one block the spans chain through the seed, which no flat hash can say.
        template <class T>
        void addBlocks(Rtx::HashState& digest, const Rtx::BlockedValues<T>& table)
        {
            if (table.size() == 0)
                digest.add(std::span<const T>());
            table.forEachBlock([&](const std::span<const T> block) { digest.add(block); });
        }

        template <class T>
        Rtx::DigestWords wordsOf(const Rtx::BlockedValues<T>& table)
        {
            Rtx::HashState whole;
            addBlocks(whole, table);
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
            Rtx::DigestWords take() const { return wordsOf(std::span<const std::byte>(mScratch)); }

        private:
            std::vector<std::byte>& mScratch;
        };

        /// One corner of a triangle, as the picture sees it.
        struct Corner
        {
            osg::Vec3f mPosition;
            osg::Vec3f mNormal;
            osg::Vec2f mTexCoord;
            osg::Vec3f mColour;
            std::uint32_t mTangent = 0;

            bool operator<(const Corner& other) const
            {
                return std::tie(mPosition, mNormal, mTexCoord, mColour, mTangent)
                    < std::tie(other.mPosition, other.mNormal, other.mTexCoord, other.mColour, other.mTangent);
            }
        };

        void addCorner(Rtx::HashState& digest, const Corner& corner)
        {
            digest.add(corner.mPosition);
            digest.add(corner.mNormal);
            digest.add(corner.mTexCoord);
            digest.add(corner.mColour);

            // Only where there is one, so a corner with none digests to the words on record.
            if (corner.mTangent != 0)
                digest.add(corner.mTangent);
        }

        /// A shape as the multiset of its triangles, each turned to start at its least corner so
        /// that the winding survives and the corner it happens to be spelt from does not.
        Unordered digestTriangles(const Rtx::SceneDesc& scene, const Rtx::MeshRange& mesh)
        {
            Unordered triangles;
            const std::span<const std::uint32_t> indices = mesh.mIndices.in(scene.meshes().getIndices());
            for (std::size_t at = 0; at + 2 < indices.size(); at += 3)
            {
                std::array<Corner, 3> corners;
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint32_t vertex = mesh.mVertices.mOffset + indices[at + corner];
                    corners[corner] = Corner{ scene.meshes().getPositions()[vertex],
                        scene.meshes().getNormals()[vertex], scene.meshes().getTexCoords()[vertex],
                        scene.meshes().getColours()[vertex], scene.meshes().getTangents()[vertex] };
                }

                const std::size_t least
                    = static_cast<std::size_t>(std::min_element(corners.begin(), corners.end()) - corners.begin());

                Rtx::HashState triangle;
                for (std::size_t corner = 0; corner < 3; ++corner)
                    addCorner(triangle, corners[(least + corner) % 3]);
                triangles.add(triangle);
            }

            return triangles;
        }

        void addMesh(Rtx::HashState& digest, const Rtx::SceneDesc& scene, const Rtx::Index index)
        {
            const Rtx::MeshRange& mesh = scene.meshes().getRows()[index];
            digest.add(digestTriangles(scene, mesh).getWords());
            digest.add(scene.deformers().kindOf(mesh));
        }
    }

    Rtx::DigestWords digestScene(const Rtx::SceneDesc& scene)
    {
        Unordered whole;

        for (const Rtx::PlacementRow& row : scene.placements().getRows())
        {
            const Rtx::MeshInstance& instance = row.mInstance;
            if (instance.mMesh == Rtx::sNoIndex)
                continue;

            Rtx::HashState placement;
            placement.add(std::span<const float>(instance.mTransform.ptr(), 16));
            placement.add(instance.mOpacity);
            placement.add(static_cast<std::uint32_t>(instance.mClass));
            addMaterial(placement, scene, instance.mMaterial);
            addMesh(placement, scene, instance.mMesh);
            whole.add(placement);
        }

        for (const Rtx::Light& light : scene.lights())
        {
            Rtx::HashState lamp;
            lamp.add(light.mPosition);
            lamp.add(light.mIntensity);
            lamp.add(light.mReach);
            whole.add(lamp);
        }

        for (const Rtx::SpriteEmitter& emitter : scene.emitters())
        {
            Rtx::HashState plume;
            plume.add(emitter.mCentre);
            plume.add(emitter.mReach);
            plume.add(emitter.isAdditive());
            addTexture(plume, scene, emitter.mTexture);
            const Rtx::Run run = Rtx::spritesOf(emitter);
            for (const Rtx::Sprite& sprite : run.in(scene.sprites()))
            {
                plume.add(sprite.mPosition);
                plume.add(sprite.mRadius);
                plume.add(sprite.mColour);
                plume.add(sprite.mAlpha);
            }
            whole.add(plume);
        }
        return whole.getWords();
    }

    /// The tables `digestParts` reads whole, held to having nothing between their fields.
    ///
    /// **A field added later that opens a gap trips this rather than the digest.** The bytes a
    /// record pads with are whatever the allocator left, so a table read whole through one of them
    /// would call two identical runs different — once, unrepeatably, and for a reason nothing in
    /// the report could name.
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
        const Rtx::MeshTable& meshes = scene.meshes();
        const Vertices now{
            .mRevision = meshes.getRevision(),
            .mLengths = { meshes.getPositions().size(), meshes.getNormals().size(), meshes.getTexCoords().size(),
                meshes.getSecondTexCoords().size(), meshes.getColours().size(), meshes.getIndices().size() },
        };
        if (mVertices == now)
            return;

        take(ScenePart::Positions, wordsOf(meshes.getPositions()));

        // **The tangents are the normals' part, and only where a mesh has any**, so a scene where
        // no vertex has one digests to the words on record, and a report keeps its columns.
        Rtx::HashState normals;
        addBlocks(normals, meshes.getNormals());
        bool tangents = false;
        meshes.getTangents().forEachBlock([&](const std::span<const std::uint32_t> block) {
            tangents = tangents || std::ranges::any_of(block, [](const std::uint32_t word) { return word != 0; });
        });
        if (tangents)
            addBlocks(normals, meshes.getTangents());
        take(ScenePart::Normals, normals.getWords());

        Rtx::HashState texCoords;
        addBlocks(texCoords, meshes.getTexCoords());
        addBlocks(texCoords, meshes.getSecondTexCoords());
        take(ScenePart::TexCoords, texCoords.getWords());

        Rtx::HashState colours;
        addBlocks(colours, meshes.getColours());
        take(ScenePart::Colours, colours.getWords());

        take(ScenePart::Indices, wordsOf(meshes.getIndices()));

        mVertices = now;
        ++mVertexHashes;
    }

    const ScenePartDigests& SceneDigester::digest(
        const Rtx::SceneDesc& scene, const Rtx::Shaders::VisibilityConstants* frame)
    {
        digestVertices(scene);

        // **Every slot, standing or free.** A free one keeps the room and the offsets its last
        // occupant left, so it is part of the state a run has to repeat — and a slot order that
        // moved is exactly what `digestScene` sums away.
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
                material, [&](const Rtx::Index slot) { materials.add(slot); },
                [&](const Rtx::Run& layers) { materials.add(layers); },
                [&](const auto& field) { materials.add(field); });
        take(ScenePart::Materials, materials.take());

        // **Each row as the 48 bytes it was before it could name a normal map, and the two fields
        // since only where a layer sets them**, so a scene whose ground has no map digests to the
        // words on record. Bound whole, so a field added and not named here does not compile; the
        // padding is no field.
        Column layers(mScratch);
        for (const Rtx::MaterialLayer& layer : scene.materials().getLayers())
        {
            const auto& [diffuse, maskOffset, maskWidth, maskHeight, diffuseTransform, maskTransform, normal, flags,
                padding]
                = layer;
            layers.addFields(std::tie(diffuse, maskOffset, maskWidth, maskHeight, diffuseTransform, maskTransform));
            if (normal != Rtx::sNoIndex)
                layers.addFields(std::tuple(std::uint8_t{ 1 }, normal));
            if (flags != 0)
                layers.addFields(std::tuple(std::uint8_t{ 2 }, flags));
        }
        take(ScenePart::Layers, layers.take());
        take(ScenePart::Masks, wordsOf(scene.materials().getMasks()));

        // By their names and by their slots both, which is the difference from `digestScene`: which
        // slot a texture landed in is what a material's index means. A file and a sprite's bake name
        // their file, and a ground its material; the kind tells the three apart. Each path ends with
        // its length, so two laid end to end cannot be read as one.
        //
        // **And every other field of the row, bound whole**, as `forEachMaterialField` binds a
        // material: the wrap reaches the sampler and the encoding and format how the texels decode,
        // so a slot read another way is a trace that moves over this column. A field added to
        // `TextureRow` does not compile here until it is named. The image is the file's bytes, which
        // the path already names.
        Column textures(mScratch);
        for (const Rtx::TextureRow& row : scene.textures().getRows())
        {
            const auto& [kind, path, groundOf, wrap, encoding, image, format] = row;
            const std::string_view name = path.value();
            textures.add(std::span<const char>(name.data(), name.size()));
            textures.add(static_cast<std::uint32_t>(name.size()));

            textures.add(static_cast<std::uint32_t>(kind));
            textures.add(static_cast<std::uint32_t>(groundOf));
            textures.add(static_cast<std::uint32_t>(wrap));
            textures.add(static_cast<std::uint32_t>(encoding));
            textures.add(static_cast<std::uint32_t>(format));
            static_cast<void>(image);
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

        // Whole, because the block is scalar-packed on every side, which `visibility.h` pins.
        take(ScenePart::Frame,
            frame != nullptr ? wordsOf(std::span<const Rtx::Shaders::VisibilityConstants>(frame, 1))
                             : Rtx::DigestWords{});

        return mParts;
    }

    ScenePartDigests digestParts(const Rtx::SceneDesc& scene)
    {
        SceneDigester once;
        return once.digest(scene);
    }

    Rtx::DigestWords digestLayout(const ScenePartDigests& parts)
    {
        Rtx::HashState whole;
        for (const Rtx::DigestWords& part : parts)
            whole.add(part);

        return whole.getWords();
    }
}
