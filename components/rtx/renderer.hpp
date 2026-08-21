#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "shaders/visibility.h"
#include "texturedata.hpp"
#include "wavespectrum.hpp"

namespace Rtx
{
    class SceneDesc;

    /// Developer instrumentation. Nobody enables any of this in a run they care about the frame rate
    /// of, and a backend reads whichever of it its API offers.
    struct ValidationOptions
    {
        bool mEnabled = false;

        /// Catch missing barriers and wrong stage masks. Costs enough to be opt-in among developers.
        bool mSynchronization = false;

        /// Instrument shaders to catch out-of-bounds access. Costs a great deal.
        bool mGpuAssisted = false;

        /// Stop the process on the first error. Off for a test suite, which provokes errors
        /// deliberately and would otherwise take the whole run down with the first one.
        bool mAbortOnError = true;
    };

    struct RendererOptions
    {
        /// Where the build wrote the compiled shaders for whichever backend this is.
        std::filesystem::path mShaderDirectory;

        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;

        ValidationOptions mValidation;
    };

    /// What a backend reports about the scene it took. The harness's summary line, as a struct.
    struct SceneStats
    {
        std::uint32_t mInstances = 0;

        /// How many of those traversal has to stop and ask about — the cost of the cutout, as a
        /// number, so a material change that marks half a cell non-opaque shows up before a frame
        /// time does.
        std::uint32_t mCutoutInstances = 0;

        std::uint64_t mStructureBytes = 0;
        std::uint64_t mTableBytes = 0;
        std::uint32_t mTextureCount = 0;
        std::uint64_t mTextureBytes = 0;
    };

    /// What a frame is asked for, beyond where the camera stands.
    struct FrameOptions
    {
        /// How many frames have gone into the running sum, this one included. Zero is no averaging.
        ///
        /// **A field here and not of `camera`, because the trace does not read it.** What is being
        /// averaged is the finished picture, which is the last pass's business; a number in the
        /// struct the trace is handed would say it belonged to the trace. The sum is kept in
        /// floating point rather than by averaging the images afterwards: eight bits per channel
        /// would round every sample before adding it, and worse, clip the sun's disc and a water
        /// glint, which are exactly the pixels a filter is most likely to get wrong.
        std::uint32_t mAccumulate = 0;

        /// Whether to move the primary ray inside its pixel, by where the frame index falls in a
        /// Halton sequence.
        ///
        /// Overwrites the camera's own `mJitter`, which is otherwise left as the caller wrote it —
        /// zero for anything `makeCamera` made, and an exact offset where something wants one.
        ///
        /// **Off unless something is putting the frames back together.** A jittered frame on its own
        /// is the same picture sampled slightly wrong; it is only worth anything to an upscaler
        /// reconstructing from several, or to a sum that averages them into an antialiased one.
        bool mJitter = false;

        /// Whether the denoiser runs over the indirect channel.
        ///
        /// **Off is how the answer it is judged against gets made.** A converged reference is the
        /// average of enough unbiased samples, and a filtered sample is not one of those — so a
        /// thousand filtered frames converge on the filter's opinion rather than on the truth.
        bool mFilter = true;
    };

    /// What one traced frame came to.
    struct FrameResult
    {
        /// Primary rays that hit something, which is what tells "the cell rendered" from "the camera
        /// faced away from it" without anyone opening the image.
        std::uint32_t mHits = 0;

        /// The submit and the wait for it. Timed by the backend because only it knows where that
        /// boundary is.
        double mTraceMs = 0.0;
    };

    /// One traced image, whichever API produced it.
    ///
    /// Six methods, none of them called more than once per frame. **Nothing below this line is
    /// abstracted:** buffers, images, memory, command buffers, descriptors and pipelines belong to a
    /// backend outright and are shared with nothing. An interface drawn tight enough to hide both
    /// would be a mini-Vulkan that Metal does not fit, and would put a virtual call inside a frame.
    ///
    /// Presentation is not here yet. It arrives with the window path, which still drives a swapchain
    /// directly — a method no backend implements would be a guess at a caller that does not exist.
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        /// Multi-line report: the device and what it can trace with.
        virtual std::string describeDevice() const = 0;

        /// Whether instrumentation is actually running, which is not the same as having asked for
        /// it — a layer can be missing. Anything quoting a frame time has to say so, because a
        /// figure measured under validation is not one to compare against anything.
        virtual bool isValidating() const = 0;

        /// Builds everything a scene needs, replacing whatever was there.
        ///
        /// `textures` are described rather than loaded — the bridge decodes and the backend uploads,
        /// which is what keeps `openmw-rtx-bridge` free of a graphics API. They are indexed by the
        /// scene's texture index, so their order is the scene's, and they must outlive the call.
        virtual void setScene(const SceneDesc& scene, std::span<const TextureData> textures, const SeaState& sea) = 0;

        /// Only meaningful once `setScene` has been called.
        virtual const SceneStats& getSceneStats() const = 0;

        /// Resizes the traced image. Kept by the backend, so nothing here allocates per frame.
        virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

        /// Traces one frame. `setScene` first, which is a contract and so an assert.
        virtual FrameResult renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options) = 0;

        /// Copies the traced image into `pixels`, four bytes per pixel, tightly packed.
        /// Not const: it submits a copy and waits for it.
        virtual void readPixels(std::vector<std::uint8_t>& pixels) = 0;

        /// Copies the last frame's motion vectors into `motion`, two floats a pixel, tightly packed.
        ///
        /// **In pixels, from where a surface is now to where it was.** Zero where the ray hit
        /// nothing, and zero where the surface stood behind the previous eye, which is not a place
        /// a screen position exists for. Not const: it submits a copy and waits for it.
        virtual void readMotion(std::vector<float>& motion) = 0;

        /// Moves whatever the API has complained about since the last call into `errors`.
        ///
        /// **Draining, not peeking**, so that clearing before a test and reading after it are the
        /// same call. Empty where nothing is instrumented, which is the only reason a suite can ask
        /// unconditionally.
        virtual void takeValidationErrors(std::vector<std::string>& errors) = 0;

    protected:
        Renderer() = default;
    };

    /// Builds a renderer, or nothing where this machine cannot run the backend asked for.
    ///
    /// **Null and a reason rather than a throw.** Bring-up failure is the one failure a caller always
    /// wants to act on — a harness skips its GPU tests, the game keeps its rasterizer — and it is the
    /// case that would otherwise oblige this fork to keep exceptions.
    std::unique_ptr<Renderer> createRenderer(const RendererOptions& options, std::string& reason);
}
