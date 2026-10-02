#pragma once

#include <cstdint>
#include <optional>

#include <osg/Vec3f>

#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    class SceneDesc;
}

namespace Resource
{
    class ResourceSystem;
}

namespace MWRender
{
    class RtxRenderer;
    class WorldMirror;

    /// What a frame's walk found, and what a second walk over the same graph added where a run
    /// asked for one.
    ///
    /// **The second half is optional, because a walk that did not happen has no count.** Two
    /// members with the second default-constructed read as a walk that resolved nothing, which is
    /// the answer a check for "the second walk adds nothing" cannot tell from a pass.
    struct WalkReport
    {
        Rtx::ExtractionStats mFound;
        std::optional<Rtx::ExtractionStats> mAgain;

        /// What every walk since the renderer started computed from the content, on the frame's
        /// thread and on the ring's: what loading the places visited so far has cost, where a
        /// frame's own row is only that frame's share.
        Rtx::Preprocessed mSession;
    };

    /// What one traced frame came to, handed to whoever measures it: what it spent, what the device
    /// answered for the frame behind, what put the picture back together, and what the walk found.
    struct FrameReport
    {
        /// What this fork owns of the frame, by phase. `Rtx::Timing` says which figure is a share
        /// of which. `Timing::Frame` is the whole frame, measured from one trace to the next:
        /// everything the game does between them, which is the number a player feels and the one
        /// `FrameResult::mWaitMs` cannot see.
        Rtx::FrameSpend mSpend{};

        /// What the hand-over did — rebuilt the scene from nothing, which a crossing is counted by,
        /// appended what arrived, or placed what was there — and what it built and described.
        Rtx::SceneUpload mUpload{};

        /// How many cells the walk left to stand, `Rtx::CellRing::getCellsToStand`.
        std::uint32_t mCellsToStand = 0;

        /// Whether the world stood paused for this frame: the game's own flag, as the frame was
        /// described with it, and not the one the game will have set by the time anybody asks.
        bool mPaused = false;

        /// What the device answered for a frame behind, or nothing where it had finished none
        /// when this frame asked — the first frames of a run, and any frame the card was still
        /// busy for. Which frame it answers for is `FrameResult::mFrame`, never this one.
        std::optional<Rtx::FrameResult> mResult{};

        /// What the backend numbered this frame — `Renderer::getFrameCount` as it stood before the
        /// frame was drawn — which is the number `mResult->mFrame` carries when this frame's own
        /// answer comes back, a frame or two from now.
        std::uint64_t mFrame = 0;

        /// What put this frame back together.
        Rtx::Reconstruction mReconstruction{};

        /// What the frame was traced with beside the scene — the camera, the sky, the air, the sea
        /// and the sample — for the run's hashes to name when a picture moves and the scene did not.
        Rtx::Shaders::VisibilityConstants mConstants{};

        /// Where the air's clocks stood for this frame — what the constants' drift and churn were
        /// taken off, kept whole because the constants hold them reduced against the fog's tiles.
        Rtx::AirClock mAir{};

        WalkReport mWalked{};

        /// Whether this frame drew the whole world: nothing of its reach left to stand, and
        /// nothing arrived. What a stop starts measuring after, because the frame after it draws
        /// what this one drew.
        bool isWhole() const { return mCellsToStand == 0 && mUpload.mKind == Rtx::SceneUpload::Kind::Placed; }
    };

    /// What a measured stop may reach beyond the frame's own report. Borrowed and valid for one stop:
    /// everything here is the renderer's own.
    struct FrameContext
    {
        /// The seam a picture inside the interface is made through, and what draws those pictures
        /// for a stop that wants one before the next frame: its phase machine is the renderer's.
        RtxRenderer& mRenderer;

        /// The world as the last walk left it: the scene it handed over, the reach and the eye it
        /// stood, the grid that disc is counted in, the ring, and what the content holds on the
        /// host. Not const, because the ring's reader is asked under its lock.
        WorldMirror& mMirror;

        /// The backend the frames and the pictures are traced into: the profile, and the reads.
        Rtx::Renderer& mBackend;

        /// The world's, because a frame is reported only while there is a world.
        Resource::ResourceSystem& mResources;
    };
}
