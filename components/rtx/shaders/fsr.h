#ifndef OPENMW_COMPONENTS_RTX_SHADERS_FSR_H
#define OPENMW_COMPONENTS_RTX_SHADERS_FSR_H

#include "camera.h"
#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// What the FSR 3.1.4 port's passes are handed, for both sides that have to agree. Included verbatim
// by both sides, for the reason `visibility.h` is. The passes are AMD's (`extern/fidelityfx/`); what
// they bind and read is the port's (`components/rtxvulkan/shaders/upscale/fsrcallbacks.glsl`).

// **Each image's layout, once for both sides** (`storageformat.h`): the shader's qualifier and the
// host's format are one line each, which the luma history's once were not — `rgba8` declared over a
// half-float image, which only a validated run reports. `FSR_INTERMEDIATE_FORMAT` is the one image
// the SDK aliases, as the farthest depth and the luma instability both.
#define FSR_ACCUMULATION_FORMAT STORAGE_R8
#define FSR_LUMA_FORMAT STORAGE_R16F
#define FSR_INTERMEDIATE_FORMAT STORAGE_R16F
#define FSR_SHADING_CHANGE_FORMAT STORAGE_R8
#define FSR_NEW_LOCKS_FORMAT STORAGE_R8
#define FSR_HISTORY_FORMAT STORAGE_RGBA16F
#define FSR_SPD_MIPS_FORMAT STORAGE_RG16F
#define FSR_FARTHEST_DEPTH_MIP1_FORMAT STORAGE_R16F
#define FSR_LUMA_HISTORY_FORMAT STORAGE_RGBA16F
#define FSR_SPD_ATOMIC_FORMAT STORAGE_R32UI
#define FSR_DILATED_MASKS_FORMAT STORAGE_RGBA8
#define FSR_FRAME_INFO_FORMAT STORAGE_RGBA32F
#define FSR_DILATED_DEPTH_FORMAT STORAGE_R32F
#define FSR_DILATED_MOTION_FORMAT STORAGE_RG16F
#define FSR_PREVIOUS_DEPTH_FORMAT STORAGE_R32UI
#define FSR_OUTPUT_FORMAT STORAGE_RGBA16F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The SDK's main constant block, `cbFSR3UPSCALER_t`, field for field and in its order: the
    /// order is what the SDK's host writes and its shaders read, and it packs to the same bytes under
    /// the scalar layout the port declares it with as under the SDK's std140.
    struct FsrConstants
    {
        ivec2 mRenderSize;
        ivec2 mPreviousRenderSize;
        ivec2 mUpscaleSize;
        ivec2 mPreviousUpscaleSize;
        ivec2 mMaxRenderSize;
        ivec2 mMaxUpscaleSize;

        /// What turns a device depth into a view depth and back, `setupDeviceDepthToViewSpaceDepthParams`.
        vec4 mDeviceToViewDepth;

        /// Where the frame sampled inside its pixel, in FSR's sign — `Upscaler` says which that is.
        vec2 mJitter;
        vec2 mPreviousJitter;

        /// What a motion vector is multiplied by to be in the render extent's UV.
        vec2 mMotionVectorScale;
        vec2 mDownscaleFactor;
        vec2 mMotionVectorJitterCancellation;

        float mTanHalfFov;
        float mJitterPhases;
        float mDeltaTime;
        float mDeltaPreExposure;
        float mViewSpaceToMetres;
        float mFrameIndex;
        float mVelocityFactor;
        float mReactivenessScale;
        float mShadingChangeScale;
        float mAccumulationAddedPerFrame;
        float mMinDisocclusionAccumulation;
    };

    /// The SDK's `cbSPD_t`, for the luma pyramid.
    struct FsrPyramidConstants
    {
        uint mMips;
        uint mWorkGroups;
        uvec2 mWorkGroupOffset;
        uvec2 mRenderSize;
    };

    /// What the port's input pass reads beside the SDK's block: the two eyes a pixel's ray can have
    /// left, which a device depth is worked out through, and the near plane that depth is written
    /// against.
    struct FsrInputConstants
    {
        Camera mCamera;
        Camera mArms;
        float mNear;
    };

    /// Where the three blocks sit in the frame's uniform buffer: each at a multiple of 256 bytes,
    /// the largest `minUniformBufferOffsetAlignment` Vulkan allows.
    const uint FSR_BLOCK_CONSTANTS = 0;
    const uint FSR_BLOCK_PYRAMID = 256;
    const uint FSR_BLOCK_INPUTS = 512;
    const uint FSR_BLOCKS_BYTES = 768;

    /// The bindings of each pass, in the SDK's own order, and the port's two over it: the linear
    /// sampler every pass samples through and, for the input pass, what it reads the trace's
    /// channels with. A pass's shader and its layout are numbered by these and nothing else.
    const uint FSR_BIND_SAMPLER = 20;

    const uint FSR_INPUTS_BIND_MOTION = 0;
    const uint FSR_INPUTS_BIND_SURFACE = 1;
    const uint FSR_INPUTS_BIND_COLOUR = 2;
    const uint FSR_INPUTS_BIND_DILATED_MOTION = 3;
    const uint FSR_INPUTS_BIND_DILATED_DEPTH = 4;
    const uint FSR_INPUTS_BIND_PREVIOUS_DEPTH = 5;
    const uint FSR_INPUTS_BIND_FARTHEST_DEPTH = 6;
    const uint FSR_INPUTS_BIND_CURRENT_LUMA = 7;
    const uint FSR_INPUTS_BIND_CONSTANTS = 8;
    const uint FSR_INPUTS_BIND_PUFFS = 9;
    const uint FSR_INPUTS_BIND_INPUTS = 10;

    const uint FSR_PYRAMID_BIND_CURRENT_LUMA = 0;
    const uint FSR_PYRAMID_BIND_FARTHEST_DEPTH = 1;
    const uint FSR_PYRAMID_BIND_ATOMIC = 2;
    const uint FSR_PYRAMID_BIND_FRAME_INFO = 3;
    const uint FSR_PYRAMID_BIND_MIP_0 = 4;

    /// How many levels of the SPD image the luma pyramid writes, each a binding of its own from
    /// `FSR_PYRAMID_BIND_MIP_0`, and the bindings after them follow the last.
    const uint FSR_PYRAMID_MIPS = 6;
    const uint FSR_PYRAMID_BIND_FARTHEST_DEPTH_MIP1 = FSR_PYRAMID_BIND_MIP_0 + FSR_PYRAMID_MIPS;
    const uint FSR_PYRAMID_BIND_CONSTANTS = FSR_PYRAMID_BIND_FARTHEST_DEPTH_MIP1 + 1;
    const uint FSR_PYRAMID_BIND_SPD = FSR_PYRAMID_BIND_CONSTANTS + 1;

    const uint FSR_CHANGE_PYRAMID_BIND_CURRENT_LUMA = 0;
    const uint FSR_CHANGE_PYRAMID_BIND_PREVIOUS_LUMA = 1;
    const uint FSR_CHANGE_PYRAMID_BIND_DILATED_MOTION = 2;
    const uint FSR_CHANGE_PYRAMID_BIND_EXPOSURE = 3;
    const uint FSR_CHANGE_PYRAMID_BIND_ATOMIC = 4;
    const uint FSR_CHANGE_PYRAMID_BIND_MIP_0 = 5;
    const uint FSR_CHANGE_PYRAMID_BIND_CONSTANTS = 11;
    const uint FSR_CHANGE_PYRAMID_BIND_SPD = 12;

    const uint FSR_CHANGE_BIND_MIPS = 0;
    const uint FSR_CHANGE_BIND_SHADING_CHANGE = 1;
    const uint FSR_CHANGE_BIND_CONSTANTS = 2;

    const uint FSR_REACTIVITY_BIND_PREVIOUS_DEPTH = 0;
    const uint FSR_REACTIVITY_BIND_DILATED_MOTION = 1;
    const uint FSR_REACTIVITY_BIND_DILATED_DEPTH = 2;
    const uint FSR_REACTIVITY_BIND_MASKS = 3;
    const uint FSR_REACTIVITY_BIND_ACCUMULATION = 5;
    const uint FSR_REACTIVITY_BIND_SHADING_CHANGE = 6;
    const uint FSR_REACTIVITY_BIND_CURRENT_LUMA = 7;
    const uint FSR_REACTIVITY_BIND_EXPOSURE = 8;
    const uint FSR_REACTIVITY_BIND_DILATED_MASKS = 9;
    const uint FSR_REACTIVITY_BIND_NEW_LOCKS = 10;
    const uint FSR_REACTIVITY_BIND_ACCUMULATION_OUT = 11;
    const uint FSR_REACTIVITY_BIND_CONSTANTS = 12;

    const uint FSR_INSTABILITY_BIND_EXPOSURE = 0;
    const uint FSR_INSTABILITY_BIND_DILATED_MASKS = 1;
    const uint FSR_INSTABILITY_BIND_DILATED_MOTION = 2;
    const uint FSR_INSTABILITY_BIND_FRAME_INFO = 3;
    const uint FSR_INSTABILITY_BIND_LUMA_HISTORY = 4;
    const uint FSR_INSTABILITY_BIND_FARTHEST_DEPTH_MIP1 = 5;
    const uint FSR_INSTABILITY_BIND_CURRENT_LUMA = 6;
    const uint FSR_INSTABILITY_BIND_LUMA_HISTORY_OUT = 7;
    const uint FSR_INSTABILITY_BIND_INSTABILITY = 8;
    const uint FSR_INSTABILITY_BIND_CONSTANTS = 9;

    const uint FSR_ACCUMULATE_BIND_EXPOSURE = 0;
    const uint FSR_ACCUMULATE_BIND_DILATED_MASKS = 1;
    const uint FSR_ACCUMULATE_BIND_DILATED_MOTION = 2;
    const uint FSR_ACCUMULATE_BIND_HISTORY = 3;
    const uint FSR_ACCUMULATE_BIND_FARTHEST_DEPTH_MIP1 = 5;
    const uint FSR_ACCUMULATE_BIND_CURRENT_LUMA = 6;
    const uint FSR_ACCUMULATE_BIND_INSTABILITY = 7;
    const uint FSR_ACCUMULATE_BIND_COLOUR = 8;
    const uint FSR_ACCUMULATE_BIND_HISTORY_OUT = 9;
    const uint FSR_ACCUMULATE_BIND_OUTPUT = 10;
    const uint FSR_ACCUMULATE_BIND_NEW_LOCKS = 11;
    const uint FSR_ACCUMULATE_BIND_CONSTANTS = 12;

    /// The SDK's workgroup sides: 8×8 for every pass but the two pyramids, which SPD runs 256 wide.
    const uint FSR_WORKGROUP = 8;
    const uint FSR_PYRAMID_WORKGROUP = 256;

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(FsrConstants) == 148, "FsrConstants must be scalar-packed on every side");
    static_assert(sizeof(FsrPyramidConstants) == 24, "FsrPyramidConstants must be scalar-packed on every side");
    static_assert(sizeof(FsrInputConstants) == 124, "FsrInputConstants must be scalar-packed on every side");
    static_assert(sizeof(FsrConstants) <= FSR_BLOCK_PYRAMID - FSR_BLOCK_CONSTANTS);
    static_assert(sizeof(FsrPyramidConstants) <= FSR_BLOCK_INPUTS - FSR_BLOCK_PYRAMID);
    static_assert(sizeof(FsrInputConstants) <= FSR_BLOCKS_BYTES - FSR_BLOCK_INPUTS);
#endif

#ifdef RTX_HOST
}
#endif

#endif
