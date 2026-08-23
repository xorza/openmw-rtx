#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "shaders/visibility.h"
#include "texturedata.hpp"
#include "upscale.hpp"
#include "wavespectrum.hpp"

struct SDL_Window;

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

        /// The size the frame is **presented** at. What it is traced at follows from `mUpscale`.
        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;

        /// Fixed for the renderer's lifetime: an upscaler is brought up once and sized per
        /// resolution, and a build that has none refuses anything but `Off` at construction.
        Upscale mUpscale = Upscale::Off;

        /// Where the frame is shown, or null for a renderer that only reads pixels back.
        ///
        /// **An `SDL_Window*` and not a surface**, because a surface is a thing an API has and SDL
        /// is what both of them are windowed through. The backend asks SDL what its instance needs
        /// and makes the surface itself, so nothing above this line has to know which API it is.
        ///
        /// A windowed renderer sizes itself to the window: `mWidth` and `mHeight` are ignored, and
        /// `resize` is what a resize event calls.
        SDL_Window* mWindow = nullptr;

        ValidationOptions mValidation;
    };

    /// One vertex of the GUI: a position already in clip space, a colour packed a byte a channel,
    /// and a texture coordinate.
    ///
    /// **MyGUI's own layout rather than one chosen here.** It fills these itself, by the thousand
    /// every frame the interface is up, and a backend that wanted them any other way would have to
    /// walk every batch and rewrite it. The position arrives in clip space because MyGUI multiplies
    /// widget pixels by the view size for itself.
    struct GuiVertex
    {
        float mX;
        float mY;
        float mZ;

        /// Red in the low byte, alpha in the high one — MyGUI's `ColourABGR`.
        std::uint32_t mColour;

        float mU;
        float mV;
    };

    /// **Trivial and twenty-four bytes**, because a frame of GUI is copied out of the buffer MyGUI
    /// filled rather than walked. Default member initialisers would cost that copy its memcpy.
    static_assert(sizeof(GuiVertex) == 24, "a GUI vertex is what MyGUI writes, and the buffer is read as its own");
    static_assert(std::is_trivial_v<GuiVertex>);

    /// One run of vertices drawn with one texture.
    ///
    /// **A run and not an index range**, because MyGUI hands over triangle lists and no indices: a
    /// batch is a stretch of the vertex buffer and a texture to read while drawing it.
    struct GuiBatch
    {
        /// A slot from `addGuiTexture`.
        std::uint32_t mTexture = 0;
        std::uint32_t mFirstVertex = 0;
        std::uint32_t mVertexCount = 0;
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

    /// What the renderer traces at, and what it presents at. Equal wherever nothing is upscaling.
    struct FrameExtents
    {
        /// The trace's own resolution, and so the size of every G-buffer channel, of the camera the
        /// trace is handed, and of what `readChannel` gives back.
        std::uint32_t mRenderWidth = 0;
        std::uint32_t mRenderHeight = 0;

        /// The size of what `readPixels` gives back, which is what `resize` was asked for.
        std::uint32_t mOutputWidth = 0;
        std::uint32_t mOutputHeight = 0;
    };

    /// The traced frame's allocation, as another API can import it.
    ///
    /// **A pair and not two calls**, so a descriptor cannot be handed over with the wrong size
    /// beside it — an import at the arithmetic size rather than the driver's padded one either fails
    /// or gives back a torn picture.
    struct SharedFrame
    {
        /// A POSIX file descriptor. **The caller owns it**, and OpenGL's importer closes it whether
        /// or not the import succeeds. Negative where this renderer cannot share.
        int mMemory = -1;

        /// How large the allocation is, which is not width times height times four.
        std::uint64_t mBytes = 0;
    };

    /// A frame's float channels, which are what an upscaler reads and what a test can check.
    enum class Channel
    {
        /// Two floats a pixel: where a surface is now, less where it was, in pixels. Zero where the
        /// ray hit nothing, and zero where the surface stood behind the previous eye — which is not
        /// a place a screen position exists for.
        Motion,

        /// Two floats a pixel: what a rasterizer with this frustum would have written — zero at the
        /// near plane, one at the far one and at every miss — and beside it the distance from the
        /// eye in world units.
        ///
        /// **Two answers because they are two questions.** An upscaler's disocclusion test wants
        /// the clip value it expects of a depth buffer; a filter comparing one surface against
        /// another wants world units, because a tolerance measured against a clip value would mean
        /// something different at every distance — most of that range is spent within a few units
        /// of the eye.
        Depth,
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
        ///
        /// **Ignored while the renderer is upscaling**, which always jitters: reconstruction from
        /// several frames of the same sample point is reconstruction from one sample.
        bool mJitter = false;

        /// Whether the denoiser runs over the indirect channel.
        ///
        /// **Off is how the answer it is judged against gets made.** A converged reference is the
        /// average of enough unbiased samples, and a filtered sample is not one of those — so a
        /// thousand filtered frames converge on the filter's opinion rather than on the truth.
        ///
        /// **Ignored while the renderer is upscaling**, which denoises for itself: Ray
        /// Reconstruction reconstructs detail from the raw bounce, and handing it a frame already
        /// blurred is asking it to recover what was thrown away.
        bool mFilter = true;

        /// What to scale the frame by before the display curve, or nothing to measure it off the
        /// frame itself.
        ///
        /// **One by default, and that default is what makes a pixel test possible.** A measured
        /// exposure makes every expected value depend on the whole frame's histogram, which is not
        /// a number anybody can hand-compute — and a converged reference wants the exposure held
        /// still across the frames it averages. A picture wants it measured, so the harness turns it
        /// on and the tests leave it alone.
        std::optional<float> mExposure = 1.0f;
    };

    /// What one traced frame came to.
    /// One stretch of a frame, measured by the device's own clock.
    ///
    /// **What a wall clock around a submit cannot tell you.** A frame is a handful of dispatches and
    /// two structure builds, and the CPU sees one number for all of them; these are what each of
    /// them cost, timed by the GPU between the commands that bracket it.
    struct GpuSpan
    {
        /// A literal owned by the backend. Valid as long as the span it came in is.
        std::string_view mName;
        double mMs = 0.0;
    };

    struct FrameResult
    {
        /// Primary rays that hit something, which is what tells "the cell rendered" from "the camera
        /// faced away from it" without anyone opening the image.
        std::uint32_t mHits = 0;

        /// The submit and the wait for it. Timed by the backend because only it knows where that
        /// boundary is.
        double mTraceMs = 0.0;

        /// Where the device spent this frame, in the order the work was recorded — the structure
        /// builds this frame asked for, then the passes that drew it. Empty where the device cannot
        /// write timestamps.
        ///
        /// **Borrowed from the renderer and valid until the next frame**, because a frame path that
        /// allocated a vector to report its own cost would be measuring itself.
        std::span<const GpuSpan> mGpu;
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

        /// The same scene with more in it: geometry and textures appended, nothing renumbered.
        ///
        /// **What a cell arriving costs, and it must not be what `setScene` costs.** Rebuilding
        /// measured at 12 ms for every acceleration structure in the scene and **150 to 225 ms for
        /// the texture array**, because the array was made again from nothing whenever one body
        /// texture appeared. Appending leaves every image where it is.
        ///
        /// `arrived` describes the textures the scene has gained since the last call, in scene
        /// order and starting at the count this already holds — never the whole table, or the
        /// describing and the shading estimate are paid twice for what has not changed.
        ///
        /// Only for a scene whose tables **grew**. A `retain` that closed the gaps renumbers every
        /// index, and the answer to that is still `setScene`.
        virtual void extendScene(const SceneDesc& scene, std::span<const TextureData> arrived, const SeaState& sea) = 0;

        /// How many textures the renderer holds, which is where `extendScene`'s `arrived` begins.
        virtual std::uint32_t getTextureCount() const = 0;

        /// The same scene, with its instances and lights somewhere else and its actors in a new pose.
        ///
        /// **What a frame does when the world has moved.** `setScene` rebuilds everything: the
        /// bottom-level acceleration structures, the vertex buffers and the texture array. None of
        /// that changes when a door swings — the geometry is the same geometry — so this rebuilds
        /// only what says where things are, plus the structure of each mesh `getDeformed` names,
        /// which is where a skinned body and a morphed face come in: their triangles are the same
        /// triangles and their vertices are new ones.
        ///
        /// `scene` must be the one `setScene` was given, with `clearPlacement` called and the
        /// instances re-walked: the placements index into structures this already holds.
        virtual void placeScene(const SceneDesc& scene, const SeaState& sea) = 0;

        /// Only meaningful once `setScene` has been called.
        virtual const SceneStats& getSceneStats() const = 0;

        /// Resizes the **presented** image. What the trace runs at follows from the upscaler, and
        /// `getExtents` is what says. Kept by the backend, so nothing here allocates per frame.
        virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

        /// What the last `resize` settled on. **The camera has to be built for the render extent**,
        /// because the trace's per-pixel ray spread comes from it.
        virtual FrameExtents getExtents() const = 0;

        /// Traces one frame. `setScene` first, which is a contract and so an assert.
        virtual FrameResult renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options) = 0;

        /// The traced frame's allocation, for another API to import.
        ///
        /// **This is how the frame reaches the game**, which does not present through a swapchain:
        /// the SDL window stays OpenGL's, and Vulkan renders offscreen into an image OpenGL imports
        /// and draws under the GUI. The character doll, both maps and video playback are all OSG
        /// render-to-texture users, and a Vulkan window would mean reimplementing every one of them
        /// before the game was playable again (`docs/rtx/plan.md` §3).
        ///
        /// A new descriptor every call, so this is asked once per resize rather than per frame.
        virtual SharedFrame shareFrame() = 0;

        /// Shows the frame `renderFrame` just produced, where this renderer was given a window.
        ///
        /// False means the surface stopped matching the window — a resize, a monitor change, a
        /// compositor restart — and the caller should `resize` and carry on. None of those is an
        /// error, which is why this is not one.
        ///
        /// **A contract and so an assert**: a renderer built without a window has nothing to
        /// present into, and asking it to is a caller's mistake rather than a condition.
        virtual bool presentFrame() = 0;

        /// A texture the GUI draws with, sized once and written whenever it changes.
        ///
        /// **Its own table, separate from the scene's.** That one is indexed by material, sized to
        /// the world and appended to when a cell arrives; a font atlas has nothing to do with either
        /// and outlives every scene the renderer is given.
        ///
        /// Slots a texture gave back are taken over before the table grows.
        virtual std::uint32_t addGuiTexture(std::uint32_t width, std::uint32_t height) = 0;

        /// The whole texture, four bytes a pixel, tightly packed, row zero first.
        ///
        /// **All of it, because MyGUI's interface has no way to say less**: it hands out a buffer to
        /// fill and takes it back filled, and there is no rectangle in that.
        virtual void writeGuiTexture(std::uint32_t texture, std::span<const std::uint8_t> rgba) = 0;

        virtual void dropGuiTexture(std::uint32_t texture) = 0;

        /// Everything the GUI asked to draw, over the finished picture, in one call.
        ///
        /// **After the frame and before it is presented or read.** The GUI's colours are
        /// display-referred — they were picked and drawn against a monitor — so they go on after the
        /// tone curve; putting them through a curve meant for radiance is how a menu comes out grey.
        ///
        /// Vertices are in clip space with +Y up, which is what MyGUI produces; a backend whose own
        /// clip space disagrees answers that for itself.
        virtual void drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches) = 0;

        /// Copies the traced image into `pixels`, four bytes per pixel, tightly packed.
        /// Not const: it submits a copy and waits for it.
        virtual void readPixels(std::vector<std::uint8_t>& pixels) = 0;

        /// Copies one of the last frame's float channels into `values`, tightly packed.
        ///
        /// The channels an upscaler is handed, and the only way anything outside the backend can
        /// look at them. Not const: it submits a copy and waits for it.
        virtual void readChannel(Channel channel, std::vector<float>& values) = 0;

        /// Moves whatever the API has complained about since the last call into `errors`.
        ///
        /// **Draining, not peeking**, so that clearing before a test and reading after it are the
        /// same call. Empty where nothing is instrumented, which is the only reason a suite can ask
        /// unconditionally.
        virtual void takeValidationErrors(std::vector<std::string>& errors) = 0;

    protected:
        Renderer() = default;
    };

    /// The SDL window flag a window must be created with for this build's backend to make a
    /// surface on it — `SDL_WINDOW_VULKAN` or `SDL_WINDOW_METAL`, as an SDL flag rather than an
    /// enum of our own, because ORing it into `SDL_CreateWindow` is the only thing anyone does
    /// with it.
    ///
    /// **Asked rather than assumed.** The two are not interchangeable and a window made with the
    /// wrong one cannot be given a surface at all. `RendererOptions::mWindow` says nothing above
    /// this line has to know which API it is; without this, opening the window was the one place
    /// that did.
    std::uint32_t surfaceWindowFlag();

    /// Builds a renderer, or nothing where this machine cannot run the backend asked for.
    ///
    /// **Null and a reason rather than a throw.** Bring-up failure is the one failure a caller always
    /// wants to act on — a harness skips its GPU tests, the game keeps its rasterizer — and it is the
    /// case that would otherwise oblige this fork to keep exceptions.
    std::unique_ptr<Renderer> createRenderer(const RendererOptions& options, std::string& reason);
}
