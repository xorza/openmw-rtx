#pragma once

#include <cstdint>

#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    /// What the alpha channel of a surface's diffuse texture means. `Cutout` and `Blend` are not
    /// exclusive in a NIF, and the rasterizer already resolves them this way: blending wins, and the
    /// test threshold survives for a renderer that would rather cut out.
    enum class AlphaMode
    {
        Opaque,

        /// Test against the material's own threshold.
        Cutout,

        /// Blend. This is where the foliage is: a canopy or a banner is an `NiAlphaProperty` over a
        /// texture whose alpha is all but binary, which the original renderer sorted rather than
        /// tested. `Rtx::Material::getAlphaTest` says what a renderer with no sort does.
        Blend,
    };

    /// An alpha test as OpenGL states one: a reference, and the sides of it a texel passes on
    /// (`Shaders::ALPHA_PASSES_BELOW` and its two siblings). At least the reference until the content
    /// says otherwise, which is the cut a blend with no test of its own is traced with.
    struct AlphaTest
    {
        float mReference = 0.0f;
        std::uint32_t mPasses = Shaders::ALPHA_PASSES_AT | Shaders::ALPHA_PASSES_ABOVE;

        /// Whether some alpha from nought to one fails it — `Shaders::alphaTestCuts`.
        bool cuts() const { return Shaders::alphaTestCuts(mPasses, mReference); }

        bool operator==(const AlphaTest& other) const = default;
    };

    /// What a surface's per-vertex colour is for: the mode the loader resolved
    /// (`SceneUtil::VertexColorModes`) — a NIF's emission and ambient-and-diffuse, and the diffuse and
    /// the ambient alone, which an OSG model's colour mode states. Specular is none of them: the
    /// renderer has no specular colour for it to replace.
    enum class VertexColour
    {
        /// The colours are there and mean nothing, or there are none at all.
        None,

        /// It replaces the material's diffuse and ambient colour, which is what the game's own
        /// shader reads through `getDiffuseColor` and `getAmbientColor` under
        /// `AmbientAndDiffuse`. Every piece of ground is this.
        Tint,

        /// It replaces the diffuse colour alone, and the ambient stays the material's.
        Diffuse,

        /// It replaces the ambient colour alone, and the diffuse stays the material's: what an
        /// OSG model's `AMBIENT` colour mode asks.
        Ambient,

        /// It replaces the material's emissive colour, and the light mode that goes with it takes
        /// the diffuse and the ambient to nought, so such a surface is its glow and nothing else.
        Glow,
    };

    /// How a blended surface composites over what is behind it, off its `BlendFunc`. Three and not
    /// the eleven factors OpenGL offers, because the shipped content states exactly these three:
    /// `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` on 3,808 records, `SRC_ALPHA, ONE` on 785 and
    /// `SRC_ALPHA, DST_ALPHA` on eight — which adds, since the frame's alpha is one — and
    /// `ONE, ONE` on one.
    enum class BlendKind : std::uint8_t
    {
        /// Covers what is behind it by its alpha.
        Over,

        /// Adds to what is behind it, weighted by its alpha, and covers nothing: a flame, a
        /// magic effect's sheet.
        Add,

        /// Adds whole, its alpha unread.
        AddWhole,
    };

    /// Whether a surface adds to what is behind it and covers nothing — `BlendKind::Add` or
    /// `AddWhole` under a blend. The one rule over the two facts as the content states them, so a
    /// description and a material made from it cannot answer differently.
    inline bool additiveSurface(const AlphaMode mode, const BlendKind blend)
    {
        return mode == AlphaMode::Blend && blend != BlendKind::Over;
    }
}
