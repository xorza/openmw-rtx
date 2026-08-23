#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>

namespace RtxBridge::Testing
{
    /// A renderer that records which of the three calls it was given, and nothing else.
    ///
    /// **The decision is what is under test, so nothing here draws.** What `extendScene` and
    /// `setScene` do with the descriptions has its own tests against a real device
    /// (`apps/components_tests/rtx/visibilitypass.cpp`); what nothing else covers is which of
    /// them a frame picks, and that answer is the same one on every machine.
    class CountingRenderer final : public Rtx::Renderer
    {
    public:
        std::string describeDevice() const override { return "a renderer that counts rather than draws"; }
        bool isValidating() const override { return false; }

        void setScene(
            const Rtx::SceneDesc& scene, std::span<const Rtx::TextureData> textures, const Rtx::SeaState&) override
        {
            ++mRebuilt;
            mDescribed = textures.size();

            // What the backend does: the array is made again and ends where the scene's table
            // does, whatever it held before.
            mTextures = static_cast<std::uint32_t>(scene.getTextures().size());
        }

        void extendScene(
            const Rtx::SceneDesc& scene, std::span<const Rtx::TextureData> arrived, const Rtx::SeaState&) override
        {
            ++mExtended;
            mDescribed = arrived.size();
            mTextures += static_cast<std::uint32_t>(arrived.size());

            // The contract `extendScene` is given rather than one it checks: appending only the
            // arrivals has to leave the array exactly as long as the scene's table.
            mAppendedToWrongEnd |= mTextures != scene.getTextures().size();
        }

        void placeScene(const Rtx::SceneDesc&, const Rtx::SeaState&) override
        {
            ++mPlaced;
            mDescribed = 0;
        }

        std::uint32_t getTextureCount() const override { return mTextures; }

        const Rtx::SceneStats& getSceneStats() const override { return mStats; }
        void resize(std::uint32_t, std::uint32_t) override {}
        Rtx::FrameExtents getExtents() const override { return {}; }
        Rtx::FrameResult renderFrame(const Rtx::Shaders::VisibilityConstants&, const Rtx::FrameOptions&) override
        {
            return {};
        }
        Rtx::SharedFrame shareFrame() override { return {}; }
        bool presentFrame() override { return true; }

        /// The GUI is not what this counts. Slots go up and nothing is drawn.
        std::uint32_t addGuiTexture(std::uint32_t, std::uint32_t) override { return mGuiTextures++; }
        void writeGuiTexture(std::uint32_t, std::span<const std::uint8_t>) override {}
        void dropGuiTexture(std::uint32_t) override {}
        void drawGui(std::span<const Rtx::GuiVertex>, std::span<const Rtx::GuiBatch>) override {}
        void traceGuiTexture(
            std::uint32_t, const Rtx::Shaders::VisibilityConstants&, const Rtx::GuiTraceOptions&) override
        {
        }
        std::uint32_t addViewScene() override { return mViewScenes++; }
        void setViewScene(std::uint32_t, const Rtx::SceneDesc&, std::span<const Rtx::TextureData>) override {}
        void dropViewScene(std::uint32_t) override {}
        void readGuiTexture(std::uint32_t, std::vector<std::uint8_t>&) override {}
        void readPixels(std::vector<std::uint8_t>&) override {}
        void readChannel(Rtx::Channel, std::vector<float>&) override {}
        void takeValidationErrors(std::vector<std::string>&) override {}

        std::uint32_t mViewScenes = 0;
        std::uint32_t mPlaced = 0;
        std::uint32_t mExtended = 0;
        std::uint32_t mRebuilt = 0;

        /// How many descriptions the last call was handed, which is what says whether a texture
        /// already uploaded was decoded and shading-estimated a second time.
        std::size_t mDescribed = 0;

        std::uint32_t mTextures = 0;
        bool mAppendedToWrongEnd = false;

    private:
        Rtx::SceneStats mStats;
        std::uint32_t mGuiTextures = 0;
    };
}
