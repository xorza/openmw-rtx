#ifndef OPENMW_COMPONENTS_RTX_SHADERS_CAMERA_H
#define OPENMW_COMPONENTS_RTX_SHADERS_CAMERA_H

#include "hosttypes.h"
#include "portable.h"

// How a pixel becomes a ray, and nothing else about the frame.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Which way an eye looks and how wide its image plane is: what a previous frame's eye is kept as,
    /// where its jitter, extent and projection are this frame's.
    struct Basis
    {
        /// `mRight` and `mUp` are already scaled by the half-extents of the image plane at unit
        /// distance, so a ray is `mForward + mRight * x - mUp * y` for `x` and `y` in [-1, 1] less
        /// `mCentre`, and no trigonometry in the shader. **`y` runs down the image**, because it is
        /// the pixel index the jitter is added to, which is why it is subtracted: `mUp` points the
        /// other way.
        ///
        /// **`mForward` is unit**, as `Rtx::viewBasisOf` normalises it.
        vec3 mForward;
        vec3 mRight;
        vec3 mUp;

        /// Where the eye's axis crosses the picture, in the minus-one-to-one `rayAt` reads, x to the
        /// right and y down: a script's `camera.setProjectionOffset`, which the rasterizer applies as
        /// a translation after its projection. Nought everywhere else, and then every rule below
        /// computes the bits it computed without one: a subtraction or an addition of nought is
        /// exact.
        vec2 mCentre RTX_ZERO;
    };

    /// Everything needed to turn a pixel into a ray — **and not where the eye is.**
    ///
    /// **The origin is deliberately absent, and that absence is what makes this shared.** The trace
    /// needs the eye's place in the world and the wavelet does not: every use it makes of a
    /// reconstructed position is a *difference* between two of them, and the origin cancels in a
    /// subtraction. Carrying it here would put twelve bytes of a world coordinate into a push
    /// constant that has no use for one, and would invite a filter to reconstruct absolute positions
    /// at Morrowind's six-figure distances, where a float has nothing left to say. `mNear` and
    /// `mFar` are out for the same reason: they are what a depth is written against, which is the
    /// trace's business alone.
    ///
    /// **Shared because two shaders build the same rays and one of them must build them exactly.**
    /// The wavelet's edge tests compare world positions reconstructed from the surface channel's distance, and
    /// a position reconstructed from a ray that differs from the one that was shaded — by the
    /// jitter, by the projection, by half a pixel — is not the position of the surface it is
    /// filtering. Two copies of one derivation, a frame at a time, is how the two come to differ.
    struct Camera
    {
        /// Which way the eye looks, how wide its image plane is and where its axis crosses it.
        Basis mBasis;

        /// Where inside its pixel the ray is sent, in pixels, `(0, 0)` being the centre.
        ///
        /// **The same axes the pixel index uses**: x to the right and y *down* the image. That is
        /// not the world's up, and writing it the other way round is the mistake to make here — the
        /// reference implementation shipped this with the sign wrong on both axes and the frame
        /// looked entirely plausible, because a wrong jitter still antialiases. It rides along with
        /// the pixel index in the shader for exactly that reason: added to the same number, it
        /// cannot disagree with it.
        ///
        /// Zero is a ray through the pixel's centre, which is every frame that is not being
        /// upscaled or averaged.
        vec2 mJitter RTX_ZERO;

        /// The angle one pixel subtends, in radians.
        ///
        /// A primary ray is the axis of a cone this wide, and the cone's width where it lands is
        /// what decides which mip a texture is read from. Without it every fetch is level zero, the
        /// mip chains are carried and never read, and everything in the distance crawls.
        ///
        /// **Nought under a parallel projection, and that is not a missing answer.** A parallel
        /// ray's cone never widens; its footprint is one pixel of the box for the whole of its
        /// length, which is what `coneAt` reads off `mRight` instead.
        float mSpreadAngle;

        /// Non-zero for a parallel projection rather than a pinhole one.
        ///
        /// **What a map is, and what a viewpoint straight down needs.** With this set, `mRight` and
        /// `mUp` carry the half-extents of a box in world units rather than of the image plane at
        /// unit distance; a pixel's ray *starts* at `mRight * x - mUp * y` from the eye and every
        /// one of them travels along `mForward`. The eye is then a plane and not a point, which is
        /// why the motion vector has no answer under it.
        uint mOrthographic;

        uint mWidth;
        uint mHeight;
    };

    /// The two eyes a pixel's ray can have left: the world's, and the one the player's own arms are
    /// seen through — the same place and the same basis at `first person field of view`, which
    /// `NpcAnimation`'s `OverrideFieldOfViewCallback` swaps the projection to under
    /// `Mask_FirstPerson`. The world's where nobody widened it, which is what every pair built here
    /// starts as. Which of the two cast a pixel's ray the surface channel says, `eyeOfPixel`.
    struct Eyes
    {
        Camera mWorld;
        Camera mArms;
    };

    /// A basis read the other way, which is what `screenOf` projects a point through: the forward
    /// and the centre as the basis has them, the right over its own squared length and the up over
    /// its own, turned to point down the image as `rayAt`'s `y` runs. **Worked out once where a
    /// block is written** (`Rtx::screenBasisOf`), where every projection took four dot products
    /// and two divisions of what is a constant of the frame. Nought where the basis is, which is
    /// what a frame with no eye before it carries.
    struct ScreenBasis
    {
        vec3 mForward;
        vec3 mAcross;
        vec3 mDown;
        vec2 mCentre RTX_ZERO;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(ScreenBasis) == 44, "ScreenBasis must be scalar-packed on every side");
    static_assert(sizeof(Camera) == 68, "Camera must be scalar-packed on every side");
    static_assert(sizeof(Eyes) == 136, "Eyes must be scalar-packed on every side");
    static_assert(sizeof(Basis) == 44, "Basis must be scalar-packed on every side");
#endif

    /// The traced camera on the grid a pass draws at once: the same basis, `width` by `height`
    /// pixels, no jitter, and the spread angle brought down with the pixel. `rayAt` divides by
    /// the camera's own extent, so this is what turns a pixel of the shown picture into the ray it
    /// shows. The display pass takes it from the host and the sprite composite works it out in
    /// the shader, both from here, so the two cannot draw different rays.
    RTX_SHADER Camera cameraOnGrid(Camera traced, uint width, uint height)
    {
        Camera shown = traced;
        shown.mJitter = vec2(0.0, 0.0);
        shown.mSpreadAngle = traced.mSpreadAngle * float(traced.mHeight) / float(height);
        shown.mWidth = width;
        shown.mHeight = height;

        return shown;
    }

    /// The traced pixel under a pixel of the shown extent, along one axis: the one its centre lands
    /// on. Nearest and not filtered, because a depth is not a quantity that averages and a tile's
    /// list is a list.
    ///
    /// **In integers, because two shaders ask it about one pixel and must land on the same traced
    /// one**: the puff composite and the display curve — `puffsCoverNothing` says what they agree
    /// about. The centre is at `(p + 1/2) * T / E`, which is `((2p + 1) * T) / (2E)` floored, and a
    /// product of two extents fits a word with room to spare. As a float product it sat exactly on a
    /// boundary for every third column at `quality`'s two to three, and which side it fell on was
    /// the compiler's rounding — different in a launch and in a dispatch. Never past the traced
    /// extent: `2p + 1 < 2E`, so the quotient is under `T`.
    RTX_SHADER uint tracedPixelUnder(uint pixel, uint extent, uint tracedExtent)
    {
        return ((2u * pixel + 1u) * tracedExtent) / (2u * extent);
    }

    /// The first shown pixel `tracedPixelUnder` puts at `traced` or past it, so the shown pixels
    /// whose centre lands on `traced` run from this of `traced` to this of `traced + 1`. A traced
    /// pixel narrower than a shown one may hold none.
    ///
    /// **The floor above inverted, in the same integers.** `tracedPixelUnder(p)` is `t` exactly
    /// where `2tE <= (2p + 1)T < 2(t + 1)E`, so the least `p` is `ceil((2tE - T) / 2T)`, which is
    /// `floor((2tE + T - 1) / 2T)`: nought at `t = 0` and `E` at `t = T`, so the runs cover the
    /// picture once.
    RTX_SHADER uint shownPixelsFrom(uint traced, uint extent, uint tracedExtent)
    {
        return (2u * traced * extent + tracedExtent - 1u) / (2u * tracedExtent);
    }

    /// Where a pixel's ray starts relative to the eye, and which way it points.
    ///
    /// **Where it starts is what tells the two projections apart, not where it points.** A pinhole fans
    /// every ray out of one point, so a pixel's offset turns the direction and the origin is nothing; a
    /// parallel one sends them all the same way from wherever on the box the pixel sits, so the same
    /// offset is a position instead.
    struct Ray
    {
        /// From the eye to where this ray begins. Zero under a pinhole projection.
        vec3 mOffset;

        /// Unit.
        vec3 mDirection;
    };

    /// Which way the point `uv` of a pinhole eye's picture looks, minus one to one across and down
    /// it: the rule `rayAcross` traces by, over a basis alone, so an eye kept from a previous frame
    /// looks the way it did. `spread` widens the image plane about the centre, per axis — one for
    /// the eye, `VisibilityConstants::mArmsSpread` for the arms' plane over the same basis. Its
    /// components are read as `uv[i]`, which both sides spell alike.
    RTX_SHADER vec3 directionAcross(Basis basis, vec2 uv, vec2 spread)
    {
        // **The sum is written out rather than hoisted into a shared term, and that is not an
        // oversight.** Floating-point addition does not associate: `f + (a - b)` and `(f + a) - b`
        // differ in the last place, and a direction that differs in the last place is a hit a texel
        // over once it has been carried thirty thousand units — a trace that sums left to right and a
        // wavelet that hoists never reconstruct quite the positions that were shaded. This is the
        // trace's association, because the trace is what everything else is judged against.
        //
        // **And `precise`, because every shader that recomputes the trace's ray has to land on the
        // trace's bits.** The wavelet, the sprite composite, the tone pass and the fog each call
        // `rayAt` for the ray the trace shot, and each is a module of its own. The build fuses a
        // multiply into the one add that reads it (`Rtx::pinFloatArithmetic`), and whether a multiply
        // here has one reader is a fact of how each module was optimised and not of this line — fused
        // in one module and not in another, a direction an ulp away is a hit distance an ulp away on
        // the surfaces it reaches and another sample on a few hundred pixels. `precise` keeps these
        // steps apart in every module, so each computes the written sum; the `normalize` under it the
        // build writes out in one order for all of them.
        RTX_PRECISE float across = (uv[0] - basis.mCentre[0]) * spread[0];
        RTX_PRECISE float down = (uv[1] - basis.mCentre[1]) * spread[1];
        RTX_PRECISE vec3 summed = basis.mForward + basis.mRight * across - basis.mUp * down;
        return normalize(summed);
    }

    /// The ray through `uv`, minus one to one across and down the image plane: the one rule for which
    /// way a point of the picture looks, for the trace, every shader that recomputes its ray, and a
    /// pick on the processor.
    RTX_SHADER Ray rayAcross(Camera camera, vec2 uv)
    {
        Ray ray;
        if (camera.mOrthographic != 0u)
        {
            // The same pair of multiply-adds `directionAcross` pins, for the same reason. The forward
            // is unit already, as `Camera` states.
            RTX_PRECISE float across = uv[0] - camera.mBasis.mCentre[0];
            RTX_PRECISE float down = uv[1] - camera.mBasis.mCentre[1];
            RTX_PRECISE vec3 offset = camera.mBasis.mRight * across - camera.mBasis.mUp * down;
            ray.mOffset = offset;
            ray.mDirection = camera.mBasis.mForward;

            return ray;
        }

        ray.mOffset = vec3(0.0, 0.0, 0.0);
        ray.mDirection = directionAcross(camera.mBasis, uv, vec2(1.0, 1.0));

        return ray;
    }

#ifdef RTX_HOST
}
#endif

// What the shading language reads and the host does not, for the reason `RTX_SHADER` gives.
//
// **Every result here is built by assigning fields rather than by calling a constructor.** GLSL has
// `Ray(offset, direction)` and no brace initialiser, so assignment is the form a shared header can
// write and a second reader could still compile.
#ifndef RTX_HOST

/// The ray through `pixel`, whose `xy` is the integer index and to which the jitter and the half
/// are added here — added to *one* number, so which way is down cannot be disagreed about.
RTX_SHADER Ray rayAt(Camera camera, vec2 pixel)
{
    RTX_PRECISE vec2 uv
        = (pixel + 0.5 + camera.mJitter) / vec2(float(camera.mWidth), float(camera.mHeight)) * 2.0 - 1.0;

    return rayAcross(camera, uv);
}

/// Where a point lands on the image plane a basis spans, and how far ahead of the eye it stands.
struct Screen
{
    /// Minus one to one across and down the plane, as `rayAt` reads its `uv` — before the divide by
    /// `mAhead`, which the caller makes, because a point behind the eye has no place on the plane
    /// and only the caller knows what it answers there.
    vec2 mAt;

    /// Along the forward, in world units. Not positive for a point on or behind the eye's plane.
    float mAhead;
};

/// The inverse of the generation in `rayAt`, for one basis and one point `offset` from its eye.
///
/// **Over a basis and not a `Camera`**, so the previous frame's eye passes through it as this
/// frame's does. The basis carries the image plane's half extents over their own squares, so one
/// dot product undoes the direction and the scale together; a plane `spread` times wider puts the
/// same point that much nearer its middle, and the centre moves it with the picture, scaled by the
/// distance ahead because the caller divides by that. **The association is the reprojection's**,
/// because a motion vector is what the answer is judged by: every caller lands on the same bits.
RTX_SHADER Screen screenOf(ScreenBasis basis, vec3 offset, vec2 spread)
{
    Screen screen;
    screen.mAhead = dot(offset, basis.mForward);
    screen.mAt = vec2(dot(offset, basis.mAcross) / spread.x, dot(offset, basis.mDown) / spread.y)
        + basis.mCentre * screen.mAhead;

    return screen;
}

/// Where `screen` lands on a grid `extent` pixels wide and high, in the units `rayAt` reads a pixel
/// in — its index, plus the half, plus the jitter it was sent with. The one way back from the plane
/// to the grid, for a point in front of the eye: `screen.mAhead` must be positive.
vec2 pixelOfScreen(Screen screen, vec2 extent)
{
    return (screen.mAt / screen.mAhead * 0.5 + 0.5) * extent;
}

/// How wide a pixel's cone is where the ray starts, and how much wider it gets per unit travelled.
struct Cone
{
    float mWidth;
    float mSpread;
};

/// What a pixel covers, as a cone.
///
/// **A pinhole's starts at a point and widens; a parallel projection's does neither.** Under one, a
/// ray's cone is one pixel of the box wide for the whole of its length, and `mSpreadAngle` is
/// nought — so a caller that reached for the angle instead would read every texture at level zero
/// and reject every filter tap but its own, which is a map tile drawn wrong. Said once here
/// because the trace and the wavelet both ask, and a rule two shaders each state is a rule they can
/// each get wrong.
RTX_SHADER Cone coneAt(Camera camera)
{
    Cone cone;
    cone.mWidth = camera.mOrthographic != 0u ? 2.0 * length(camera.mBasis.mRight) / float(camera.mWidth) : 0.0;
    cone.mSpread = camera.mOrthographic != 0u ? 0.0 : camera.mSpreadAngle;

    return cone;
}

/// How far a ray's cone has spread from its axis where it starts, in radians.
///
/// **Half of `mSpreadAngle`, and the half matters.** Everywhere else that number grows a cone's
/// *width* — `resolved` compares it against a wavelength and `coneLod` against a texel area — so a
/// place that wants an angle from the axis has to take half of it, and a disc whose brightness goes
/// as one over the radius squared would be four times wrong otherwise.
RTX_SHADER float pixelBlur(Camera camera)
{
    return 0.5 * camera.mSpreadAngle;
}

#endif

#endif
