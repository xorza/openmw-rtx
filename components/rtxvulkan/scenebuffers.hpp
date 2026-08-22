#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vector>

#include <components/rtx/instancerecord.hpp>
#include <components/rtx/lightgrid.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/wavespectrum.hpp>

#include "buffer.hpp"
#include "hostbuffer.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;
    class SceneDesc;

    /// The tables a shader reads at a hit: what the triangle was, and how it is shaded.
    ///
    /// Positions and indices are already on the GPU for the acceleration structure to be built from,
    /// but a hit needs the *attributes* — and the mesh, instance and material tables to find them
    /// through. Position fetch covered a normal; nothing covers a texture coordinate.
    class SceneBuffers
    {
    public:
        /// @param indices the buffer `SceneAcceleration` already built from, borrowed rather than
        ///        uploaded again. It must outlive this.
        /// @param sea what the water is doing, which belongs to no cell: one table for the whole
        ///        world, animated by the time in the frame's constants rather than rebuilt. A state
        ///        with no height in it is a flat sea, which is what a test asserting an exact
        ///        transmittance needs.
        SceneBuffers(const Device& device, CommandPool& pool, const SceneDesc& scene, VkBuffer indices,
            const SeaState& sea = SeaState{});

        /// Rewrites what a moving world changes, leaving what it is made of alone.
        ///
        /// **The split is the whole point of this class having two entry points.** Rebuilding all of
        /// it per frame was the largest single cost in the renderer — measured at twenty to
        /// twenty-seven milliseconds on a nine-by-nine region — and almost none of it had changed:
        /// the texture coordinates, the mesh table, the materials and the masks are what the scene is
        /// made of and only `setScene` can alter them.
        ///
        /// What does change is where things are, what is lit, and the vertices of anything skinned.
        /// Those live in memory the host writes straight into, so this is a `memcpy` and not a
        /// staging buffer, a copy command, a submit and a wait on the queue.
        ///
        /// `scene` must be the one the constructor was given.
        void place(const SceneDesc& scene, const SeaState& sea);

        SceneBuffers(const SceneBuffers&) = delete;
        SceneBuffers& operator=(const SceneBuffers&) = delete;

        VkBuffer getNormals() const { return mNormals.getHandle(); }
        VkBuffer getTexCoords() const { return mTexCoords.getHandle(); }
        VkBuffer getIndices() const { return mIndices; }
        VkBuffer getMeshes() const { return mMeshes.getHandle(); }
        VkBuffer getInstances() const { return mInstances.getHandle(); }
        VkBuffer getMaterials() const { return mMaterials.getHandle(); }
        VkBuffer getLayers() const { return mLayers.getHandle(); }
        VkBuffer getMasks() const { return mMasks.getHandle(); }
        VkBuffer getLights() const { return mLights.getHandle(); }
        VkBuffer getLightOffsets() const { return mLightOffsets.getHandle(); }
        VkBuffer getLightIndices() const { return mLightIndices.getHandle(); }
        VkBuffer getSprites() const { return mSprites.getHandle(); }
        VkBuffer getEmitters() const { return mEmitters.getHandle(); }

        /// How many emitters the last `place` wrote, which is what the shader's loop runs to. The
        /// buffer's own length cannot say: it never shrinks, and a table that stood at forty
        /// emitters would keep drawing them in the cell after.
        std::uint32_t getEmitterCount() const { return mEmitterCount; }

        /// Where the lamps were binned, for the constants the pass pushes.
        const LightGrid& getLightGrid() const { return mLightGrid; }

        /// The grid's geometry, as the shader reads it.
        VkBuffer getGrid() const { return mGrid.getHandle(); }
        VkBuffer getWaves() const { return mWaves.getHandle(); }

        VkDeviceSize getBytes() const;

    private:
        /// Makes `held` at least `bytes` long, keeping it where it is already big enough.
        ///
        /// A frame that placed more than the last one is a cell that arrived, and that goes through
        /// `setScene` — so this is the rare path and never shrinks. Growing rather than sizing
        /// exactly is what keeps a light appearing from reallocating every buffer behind it.
        void reserve(HostBuffer& held, VkDeviceSize bytes);

        const Device* mDevice = nullptr;

        // What the scene is made of. Written once, through a staging copy, because nothing rewrites
        // them and device-only memory is the faster place for the device to read.
        Buffer mTexCoords;
        Buffer mMeshes;
        Buffer mMaterials;
        Buffer mLayers;
        Buffer mMasks;

        VkBuffer mIndices = VK_NULL_HANDLE;

        // What a moving world changes. Written straight into video memory every frame.
        //
        // The normals are here for the sake of a few dozen of them: a skinned body's are recomputed
        // per frame and the rest of a cell's never change, so the buffer is filled once and then
        // written a mesh at a time.
        HostBuffer mNormals;
        HostBuffer mInstances;
        HostBuffer mLights;
        HostBuffer mLightOffsets;
        HostBuffer mGrid;
        HostBuffer mLightIndices;
        HostBuffer mWaves;
        HostBuffer mSprites;
        HostBuffer mEmitters;

        // Refilled per placement rather than reallocated: a scene is tens of thousands of instances
        // and this is the frame path.
        std::vector<Shaders::GpuInstance> mInstanceScratch;
        std::vector<InstanceRecord> mRecordScratch;
        std::vector<Shaders::GpuLight> mLightScratch;

        std::vector<Shaders::GpuSprite> mSpriteScratch;
        std::vector<Shaders::GpuEmitter> mEmitterScratch;

        std::uint32_t mEmitterCount = 0;

        /// Kept because the pass needs its geometry, which no buffer carries.
        LightGrid mLightGrid;
    };
}
