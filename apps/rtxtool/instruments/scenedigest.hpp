#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    class SceneDesc;
}

namespace RtxTool
{
    /// One thing a scene holds, and one column of a hashes file, in the order the tables are laid out: a digest each,
    /// so a run whose layout differs can say which table moved.
    enum class ScenePart : std::size_t
    {
        Positions,
        Normals,
        TexCoords,

        /// Each vertex's colour, which every hit multiplies by.
        Colours,

        Indices,

        /// One row per mesh slot: where its geometry sits and what it wears.
        Meshes,

        /// One row per placement slot: where it stands, what it is and what it wears.
        Instances,

        /// Where each placement stood last frame, which a motion vector is the difference of.
        Previous,

        Materials,
        Layers,
        Masks,
        Textures,
        Lights,
        Sprites,
        Emitters,

        /// What disturbed the water this frame, which the ripple field is stepped by.
        Ripples,

        /// What poses a mesh that deforms: the deformers, a rig's runs and influences and a
        /// morph's offsets.
        Deformers,

        /// The poses themselves, in the words both kinds are laid in.
        Poses,

        /// What the scene holds, by what each thing is and not by the slot it stands in (`digestScene`): the one
        /// part a comparison judges by, with `Frame`. Every part before it is the layout, which follows the order the
        /// content arrived in from the threads that prepare it, and says where a difference landed.
        Content,

        /// What the frame was traced with beside the scene, as `Shaders::VisibilityConstants`: the one input to a
        /// trace the scene does not hold. A picture that moved while every other column stood is a frame column, a
        /// history's, or the compiled code (`PipelineCacheSpec::mDirectory`). Nought where no frame was traced.
        Frame,

        Count,
    };

    /// What a hashes file's header spells for each part, in column order: the header is written from this table and
    /// a column is indexed by the enumerator, so the two must agree, which `Rtx::NamedEnum` checks.
    inline constexpr Rtx::NamedEnum sSceneParts{ std::array{
        std::pair{ ScenePart::Positions, std::string_view("positions") },
        std::pair{ ScenePart::Normals, std::string_view("normals") },
        std::pair{ ScenePart::TexCoords, std::string_view("texcoords") },
        std::pair{ ScenePart::Colours, std::string_view("colours") },
        std::pair{ ScenePart::Indices, std::string_view("indices") },
        std::pair{ ScenePart::Meshes, std::string_view("meshes") },
        std::pair{ ScenePart::Instances, std::string_view("instances") },
        std::pair{ ScenePart::Previous, std::string_view("previous") },
        std::pair{ ScenePart::Materials, std::string_view("materials") },
        std::pair{ ScenePart::Layers, std::string_view("layers") },
        std::pair{ ScenePart::Masks, std::string_view("masks") },
        std::pair{ ScenePart::Textures, std::string_view("textures") },
        std::pair{ ScenePart::Lights, std::string_view("lights") },
        std::pair{ ScenePart::Sprites, std::string_view("sprites") },
        std::pair{ ScenePart::Emitters, std::string_view("emitters") },
        std::pair{ ScenePart::Ripples, std::string_view("ripples") },
        std::pair{ ScenePart::Deformers, std::string_view("deformers") },
        std::pair{ ScenePart::Poses, std::string_view("poses") },
        std::pair{ ScenePart::Content, std::string_view("scene") },
        std::pair{ ScenePart::Frame, std::string_view("frame") },
    } };

    /// What a hashes file's header spells for `part`.
    constexpr std::string_view nameOf(const ScenePart part)
    {
        return sSceneParts.name(part);
    }

    /// A digest of every part, indexed by `ScenePart`.
    using ScenePartDigests = std::array<Misc::DigestWords, static_cast<std::size_t>(ScenePart::Count)>;

    /// Digests each part of a scene on its own, as the renderer reads it: every table in order and the shared
    /// geometry buffers included, which answers "is this the same buffer", and beside them the content, which
    /// answers "is this the same scene" (`ScenePart::Content`). Fields, not records, wherever a record has padding.
    ///
    /// The geometry's parts are each mesh's digests, made as it arrived where the scene keeps them
    /// (`Rtx::MeshTable::keepDigests`): a scene digested asks for them before its first mesh. Each mesh's shape is kept
    /// across frames and made again only where `MeshTable::getRevision` says so. A column of rows is hashed once, not
    /// once per field.
    class SceneDigester
    {
    public:
        /// Every part's digest of `scene` as it stands, and of `frame`, the constants it is traced
        /// with, or null where nothing is traced.
        const ScenePartDigests& digest(
            const Rtx::SceneDesc& scene, const Rtx::Shaders::VisibilityConstants* frame = nullptr);

    private:
        void digestVertices(const Rtx::SceneDesc& scene);

        /// `ScenePart::Content`, each placement's mesh by its shape in `mShapes`.
        void digestContent(const Rtx::SceneDesc& scene);

        void take(ScenePart part, const Misc::DigestWords& words) { mParts[static_cast<std::size_t>(part)] = words; }

        ScenePartDigests mParts{};

        /// Each mesh slot's shape, its triangles as a multiset and what poses it, as `MeshTable::getRevision` stood
        /// at `mShapesRevision`: what every placement of the mesh is made of, made once a mesh and not once a
        /// placement a frame.
        std::vector<Misc::DigestWords> mShapes;
        std::optional<std::uint64_t> mShapesRevision;

        /// A column's rows laid end to end, hashed once. Cleared and refilled, never freed.
        std::vector<std::byte> mScratch;
    };

    /// The one-off form `scene` and `shot` report with: a digester made and used once.
    ScenePartDigests digestParts(const Rtx::SceneDesc& scene);

    /// One number for what a scene is made of, the same for two stagings of one cell and for two runs whose content
    /// arrived in other orders. Per thing and summed, because `SceneUtil::Optimizer` merges sibling shapes in heap
    /// order and the threads that prepare content finish in no fixed one, and the vertex runs and slot numbers
    /// follow: each placement is digested from where it stands and stood the frame before, what it wears, the
    /// multiset of its triangles and its pose; each lamp, emitter with its sprites and ripple from what it is.
    /// Blind to the layout, which `digestLayout` stands beside it for. Textures by their paths.
    /// `ScenePart::Content` of `parts`.
    Misc::DigestWords digestScene(const ScenePartDigests& parts);

    /// One number for the whole layout, every part but the content, for a caller with one line to print; a report
    /// with room for the columns names them instead.
    Misc::DigestWords digestLayout(const ScenePartDigests& parts);
}
