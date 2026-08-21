#include "vulkanrenderer.hpp"

#include <cassert>
#include <chrono>

#include <components/rtx/error.hpp>
#include <components/rtx/scenedesc.hpp>

#include "image.hpp"
#include "physicaldevice.hpp"
#include "requirements.hpp"
#include "sceneacceleration.hpp"
#include "scenebuffers.hpp"
#include "texture.hpp"
#include "visibilitypass.hpp"

namespace Rtx
{
    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mInstance(toInstanceOptions(options.mValidation))
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle()))
        , mPool(mDevice)
        , mShaderDirectory(options.mShaderDirectory)
    {
        mHitCount = Buffer(mDevice, sizeof(std::uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        createTargets(options.mWidth, options.mHeight);
    }

    // Out of line because the members it destroys are only forward declared in the header.
    VulkanRenderer::~VulkanRenderer() = default;

    void VulkanRenderer::createTargets(std::uint32_t width, std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        mWidth = width;
        mHeight = height;

        mTarget = std::make_unique<Image>(mDevice, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        // Sixteen bytes a pixel, which is 33 MiB at 1080p and the price of a sum that neither rounds
        // nor clips. Made even for a run that is not averaging: the shader leaves it alone then, but
        // the descriptor still has to point at an image the pass will accept.
        mHistory = std::make_unique<Image>(
            mDevice, width, height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);

        mHistoryWritten = false;
    }

    std::string VulkanRenderer::describeDevice() const
    {
        std::string report = "loader:            Vulkan " + versionString(mInstance.getApiVersion()) + '\n'
            + "validation:        " + (mInstance.getValidationLog() != nullptr ? "on" : "off") + '\n'
            + "debug utils:       " + (mInstance.hasDebugUtils() ? "on" : "off") + '\n';

        report += mDevice.getPhysicalDevice().describe();

        // Reaching here is the part that proves the rest: the device resolved every entry point the
        // required extensions promise, and a driver advertising one it cannot dispatch fails before
        // this line rather than at the first frame that needed it.
        report += "\nlogical device and every required entry point: ok\n";

        return report;
    }

    bool VulkanRenderer::isValidating() const
    {
        // The log exists only where the layer was found, so this answers "loaded" and not "asked".
        return mInstance.getValidationLog() != nullptr;
    }

    void VulkanRenderer::setScene(const SceneDesc& scene, std::span<const TextureData> textures, const SeaState& sea)
    {
        // Torn down before anything is built, so a second scene does not hold two of everything at
        // once — a cell's structures and textures are most of what this renderer occupies.
        mPass.reset();
        mTextures.reset();
        mBuffers.reset();
        mAcceleration.reset();

        mAcceleration = std::make_unique<SceneAcceleration>(mDevice, mPool, scene);
        mBuffers = std::make_unique<SceneBuffers>(mDevice, mPool, scene, mAcceleration->getIndices(), sea);
        mTextures = std::make_unique<TextureArray>(mDevice, mPool, textures);
        mPass = std::make_unique<VisibilityPass>(mDevice, mPool, mShaderDirectory, mTextures->getLayout());

        mStats = SceneStats{
            .mInstances = mAcceleration->getInstanceCount(),
            .mCutoutInstances = mAcceleration->getCutoutInstanceCount(),
            .mStructureBytes = mAcceleration->getStructureBytes(),
            .mTableBytes = mBuffers->getBytes(),
            .mTextureCount = mTextures->getCount(),
            .mTextureBytes = mTextures->getBytes(),
        };
    }

    void VulkanRenderer::resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == mWidth && height == mHeight)
            return;

        // The images about to be replaced may still be in flight.
        mDevice.waitIdle();
        createTargets(width, height);
    }

    FrameResult VulkanRenderer::renderFrame(const Shaders::VisibilityConstants& camera)
    {
        assert(mPass != nullptr && "renderFrame before setScene");

        // The count is an atomic sum over the frame, so it starts each one at nothing.
        *static_cast<std::uint32_t*>(mHitCount.map()) = 0;
        mHitCount.unmap();

        const VisibilityInputs inputs{
            .mScene = mAcceleration->getTopLevel(),
            .mBuffers = mBuffers.get(),
            .mTextures = mTextures->getSet(),
        };

        const bool fresh = !mHistoryWritten;
        const auto start = std::chrono::steady_clock::now();

        mPool.submitAndWait([&](VkCommandBuffer commands) {
            mTarget->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            // The first write needs no contents and nothing to wait on; every one after reads what
            // the last left, which the fence orders and does not make visible.
            mHistory->transition(commands, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL,
                fresh ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                fresh ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            mPass->record(commands, inputs, *mTarget, *mHistory, mHitCount, camera);
        });

        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        mHistoryWritten = true;

        const std::uint32_t hits = *static_cast<const std::uint32_t*>(mHitCount.map());
        mHitCount.unmap();

        return FrameResult{ .mHits = hits, .mTraceMs = ms };
    }

    void VulkanRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        assert(mTarget != nullptr);
        mTarget->read(mPool, VK_IMAGE_LAYOUT_GENERAL, pixels);
    }

    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options, std::string& reason)
    {
        // **Where this backend's exceptions stop.** Everything below throws `Error` on a machine that
        // cannot run it — no loader, no driver, a device that does not qualify — and every caller
        // wants that as an answer rather than as an unwind.
        try
        {
            return std::make_unique<VulkanRenderer>(options);
        }
        catch (const Error& error)
        {
            reason = error.what();
            return nullptr;
        }
    }
}
