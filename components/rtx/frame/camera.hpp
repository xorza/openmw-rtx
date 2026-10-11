#pragma once

#include <cstdint>
#include <optional>

#include <osg/Matrixd>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    /// Far enough to cross any cell. Every camera's reach (`VisibilityConstants::mReach`), and the
    /// world camera's clip as well, so a harness that traced to a different one measured a
    /// different frame from the game.
    constexpr float sFarPlane = 200000.0f;

    /// Where the depth buffer's zero sits: one unit, of the 8192 a cell is wide. Nothing is
    /// clipped against it — `VisibilityConstants::mNear` says why a ray tracer has one at all — so
    /// it only has to be nearer than anything the eye can find itself inside of.
    constexpr float sNearPlane = 1.0f;

    /// A viewpoint's axes in world coordinates, unit, which is what a view matrix holds the inverse
    /// of. What a camera is built from, and what a walk hands the nodes that turn toward whoever
    /// is looking — a `NiBillboardNode` — in place of the cull stack the rasterizer hands them.
    /// A fresh one looks along +Y with +Z up, which is where OpenSceneGraph's identity view looks.
    struct ViewBasis
    {
        osg::Vec3f mOrigin;
        osg::Vec3f mForward{ 0.0f, 1.0f, 0.0f };
        osg::Vec3f mRight{ 1.0f, 0.0f, 0.0f };
        osg::Vec3f mUp{ 0.0f, 0.0f, 1.0f };
    };

    /// The basis the inverse of a view matrix stands: its translation, and its own +X, -Z and +Y
    /// as OpenSceneGraph's eye space has them — +X right, +Y up and the view down -Z. Normalised
    /// rather than assumed, because a view matrix with a scale in it is a legal one. Nothing where
    /// an axis collapsed: a camera nobody filled in arrives every frame, and a frame skips rather
    /// than unwinds.
    std::optional<ViewBasis> viewBasisOf(const osg::Matrixd& world);

    /// Where a frame or a picture is seen from: the eye's place, how far it sees and how near it
    /// starts, and the two eyes — the world's and the arms' — it looks through. What the camera
    /// builders below make, and the camera's half of a frame's block: `constantsFor` lays it over
    /// a block nothing else has described yet.
    struct Viewpoint
    {
        osg::Vec3f mOrigin;
        float mNear = sNearPlane;
        float mFar = sFarPlane;

        /// `VisibilityConstants::mReach`.
        float mReach = sFarPlane;

        Shaders::Eyes mEyes{};
    };

    /// The block a frame starts from: `view`, and every field a world describes at what a frame
    /// with no world over it reads — no sky, no water and no fog. A host hands the viewpoint and the
    /// world, and the renderer lays one over the other (`describeWorld`).
    Shaders::VisibilityConstants constantsFor(const Viewpoint& view);

    /// Whether a camera is built at `verticalFovDegrees`: inside (0°, 180°). At nought the image
    /// plane has no extent and every ray the same direction, at 180° it stands at infinity, and
    /// past it the picture turns over. The settings hold the field of view to [1, 179], but a
    /// script's override and the content's werewolf one reach the camera as they were written.
    bool isFieldOfView(float verticalFovDegrees);

    /// The same eye at another field of view: the basis kept and the image plane's half extents
    /// taken from `verticalFovDegrees`, at the camera's own aspect. What the player's own arms are
    /// seen through — `Shaders::Eyes::mArms` — since `first person field of view`
    /// and `field of view` are two settings. Every camera built below starts with the arms' eye
    /// equal to its own. Nothing for an angle that is not a field of view (`isFieldOfView`).
    std::optional<Shaders::Camera> cameraAtFieldOfView(const Shaders::Camera& camera, float verticalFovDegrees);

    /// Moves `camera`'s picture by `shift`, in clip units with x right and y up: what a projection
    /// post-multiplied by that translation draws, which is how the game applies a script's
    /// `camera.setProjectionOffset`. `Shaders::Camera::mCentre` holds it in the picture's own axes.
    void shiftPicture(Shaders::Camera& camera, const osg::Vec2f& shift);

    /// `basis` read the other way, `Shaders::ScreenBasis`: what a block a projection reads is
    /// handed beside the basis it was worked out from. Nought for a basis of nought.
    Shaders::ScreenBasis screenBasisOf(const Shaders::Basis& basis);

    /// A camera from a view matrix in OpenSceneGraph's convention: row vectors, and an eye space
    /// looking down its own -Z. The basis comes out of the matrix rather than from the world's up,
    /// which is what lets a map look straight down. Nothing for a matrix that cannot be inverted
    /// or whose basis collapsed, as `viewBasisOf` says, nor for an angle that is not a field of view.
    std::optional<Viewpoint> makeCameraFromView(const osg::Matrixd& view, float verticalFovDegrees, std::uint32_t width,
        std::uint32_t height, float near, float far);

    /// The same viewpoint with no perspective in it: every ray travels the view direction, and
    /// which one a pixel sends comes from where it sits on a box `worldWidth` by `worldHeight`
    /// centred on the eye. A box with no extent is a caller's contract and not a matrix's, and
    /// `Crash::contract` holds it.
    std::optional<Viewpoint> makeOrthographicCameraFromView(const osg::Matrixd& view, float worldWidth,
        float worldHeight, std::uint32_t width, std::uint32_t height, float near, float far);

    /// Where inside its pixel frame `index` should sample, in pixels and centred on zero, in the
    /// image's axes. Halton in bases two and three — 1/2, 1/4, 3/4, 1/8 and 1/3, 2/3, 1/9 —
    /// because an upscaler reconstructs from the positions a few frames covered between them, and
    /// random draws clump.
    osg::Vec2f haltonJitter(std::uint32_t index);
}
