// Derived from the FidelityFX SDK (v1.1.4), `ffx_fsr3upscaler_callbacks_glsl.h`, whose notice
// follows.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// What FSR 3.1 binds and reads, as the renderer hands it over: the SDK's callbacks, which is the file
// the SDK has an integration write, and what this one changes.
//
// - **The constant blocks are `fsr.h`'s structs**, laid out as the SDK's std140 blocks and declared
//   scalar, so the host writes one struct and not a list of floats.
// - **One linear sampler** at `FSR_BIND_SAMPLER`: the SDK's point sampler is declared and read nowhere.
// - **The depth is worked out, not bound.** The trace keeps the distance along each pixel's ray,
//   not a device depth; `LoadInputDepth` makes the reversed, infinite one the SDK is built for, on
//   the frames FSR runs, and the host sets the depth flags to say so.
// - **The motion vector is the trace's made the SDK's.** `CHANNEL_MOTION` runs from the jittered
//   sample to where the surface stood on the last frame's unjittered screen; the SDK wants it from
//   the pixel's centre. The trace samples at `+ jitter`, and FSR's `Jitter()` is the same offset
//   negated (`Upscaler` says why), so the centre is `sample - (-Jitter())` away and the vector is
//   the channel's `- Jitter()`.
// - **No reactive and no composition mask.** The SDK binds a 1×1 default for each and reads it at
//   full pixel coordinates, which Vulkan leaves undefined past the edge; here the loads answer the
//   default, nought, and nothing is bound.
// - **Formats the SDK's host creates**: the luma history is `R16G16B16A16_FLOAT` there and was
//   declared `rgba8` here, a view whose format is not the declared one; and the output is the
//   renderer's `rgba16f`.
// - The passes the renderer does not run (the reactive generators, RCAS, the debug view) are gone.

#include "camera.h"
#include "fsr.h"
#include "gbuffer.h"

#include "lib/spritelist.glsl"

#include "fsr3upscaler/ffx_fsr3upscaler_resources.h"

#if defined(FFX_GPU)
#include "ffx_core.h"
#endif // #if defined(FFX_GPU)

#if defined(FFX_GPU)
#ifndef FFX_PREFER_WAVE64
#define FFX_PREFER_WAVE64
#endif // FFX_PREFER_WAVE64

#if defined(FSR3UPSCALER_BIND_CB_FSR3UPSCALER)
layout (set = 0, binding = FSR3UPSCALER_BIND_CB_FSR3UPSCALER, scalar) uniform cbFSR3UPSCALER_t
{
    FsrConstants cbFSR3Upscaler;
};

FfxInt32x2 RenderSize() { return cbFSR3Upscaler.mRenderSize; }
FfxInt32x2 PreviousFrameRenderSize() { return cbFSR3Upscaler.mPreviousRenderSize; }
FfxInt32x2 MaxRenderSize() { return cbFSR3Upscaler.mMaxRenderSize; }
FfxInt32x2 UpscaleSize() { return cbFSR3Upscaler.mUpscaleSize; }
FfxInt32x2 PreviousFrameUpscaleSize() { return cbFSR3Upscaler.mPreviousUpscaleSize; }
FfxInt32x2 MaxUpscaleSize() { return cbFSR3Upscaler.mMaxUpscaleSize; }
FfxFloat32x2 Jitter() { return cbFSR3Upscaler.mJitter; }
FfxFloat32x2 PreviousFrameJitter() { return cbFSR3Upscaler.mPreviousJitter; }
FfxFloat32x4 DeviceToViewSpaceTransformFactors() { return cbFSR3Upscaler.mDeviceToViewDepth; }
FfxFloat32x2 MotionVectorScale() { return cbFSR3Upscaler.mMotionVectorScale; }
FfxFloat32x2 DownscaleFactor() { return cbFSR3Upscaler.mDownscaleFactor; }
FfxFloat32x2 MotionVectorJitterCancellation() { return cbFSR3Upscaler.mMotionVectorJitterCancellation; }
FfxFloat32 TanHalfFoV() { return cbFSR3Upscaler.mTanHalfFov; }
FfxFloat32 JitterSequenceLength() { return cbFSR3Upscaler.mJitterPhases; }
FfxFloat32 DeltaTime() { return cbFSR3Upscaler.mDeltaTime; }
FfxFloat32 DeltaPreExposure() { return cbFSR3Upscaler.mDeltaPreExposure; }
FfxFloat32 ViewSpaceToMetersFactor() { return cbFSR3Upscaler.mViewSpaceToMetres; }
FfxFloat32 FrameIndex() { return cbFSR3Upscaler.mFrameIndex; }
FfxFloat32 VelocityFactor() { return cbFSR3Upscaler.mVelocityFactor; }
FfxFloat32 AccumulationAddedPerFrame() { return cbFSR3Upscaler.mAccumulationAddedPerFrame; }
FfxFloat32 MinDisocclusionAccumulation() { return cbFSR3Upscaler.mMinDisocclusionAccumulation; }

#endif // #if defined(FSR3UPSCALER_BIND_CB_FSR3UPSCALER)

#if defined(FSR3UPSCALER_BIND_CB_SPD)
layout (set = 0, binding = FSR3UPSCALER_BIND_CB_SPD, scalar) uniform cbSPD_t
{
    FsrPyramidConstants cbSPD;
};

FfxUInt32 MipCount() { return cbSPD.mMips; }
FfxUInt32 NumWorkGroups() { return cbSPD.mWorkGroups; }
FfxUInt32x2 WorkGroupOffset() { return cbSPD.mWorkGroupOffset; }
FfxUInt32x2 SPD_RenderSize() { return cbSPD.mRenderSize; }
#endif // #if defined(FSR3UPSCALER_BIND_CB_SPD)

layout (set = 0, binding = FSR_BIND_SAMPLER) uniform sampler s_LinearClamp;

#if defined(FSR3UPSCALER_BIND_SRV_SPD_MIPS)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_SPD_MIPS) uniform texture2D  r_spd_mips;

FfxInt32x2 GetSPDMipDimensions(FfxUInt32 uMipLevel)
{
	return textureSize(r_spd_mips, int(uMipLevel)).xy;
}

FfxFloat32x2 SampleSPDMipLevel(FfxFloat32x2 fUV, FfxUInt32 mipLevel)
{
	return textureLod(sampler2D(r_spd_mips, s_LinearClamp), fUV, float(mipLevel)).rg;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_INPUT_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_INPUT_DEPTH) uniform texture2D r_input_surface;
layout (set = 0, binding = FSR_INPUTS_BIND_PUFFS) uniform texture2D r_input_puffs;
layout (set = 0, binding = FSR_INPUTS_BIND_INPUTS, scalar) uniform cbInputs_t
{
    FsrInputConstants cbInputs;
};

/// The device depth a rasterizer would have written at `iPxPos`, reversed and with its far plane at
/// infinity, worked out from `CHANNEL_SURFACE`'s distance along the pixel's own ray: the view depth
/// is that distance times the ray's cosine to the eye's forward axis, and the device depth the near
/// plane over it. Nought, the infinitely far, where the ray met nothing. Through the eye the ray
/// left, `eyeOfPixel`.
FfxFloat32 LoadInputDepth(FfxInt32x2 iPxPos)
{
    const vec2 seen = texelFetch(r_input_surface, iPxPos, 0).rg;
    const Camera eye = eyeOfPixel(texelFetch(r_input_puffs, iPxPos, 0), cbInputs.mCamera, cbInputs.mArms);
    const float along = seen.y * dot(rayAt(eye, vec2(iPxPos)).mDirection, normalize(eye.mForward));
    return seen.x == SURFACE_NO_NORMAL ? 0.0 : cbInputs.mNear / max(along, cbInputs.mNear);
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_REACTIVE_MASK)
FfxFloat32 LoadReactiveMask(FfxInt32x2 iPxPos)
{
    return 0.0;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_TRANSPARENCY_AND_COMPOSITION_MASK)
FfxInt32x2 GetTransparencyAndCompositionMaskResourceDimensions()
{
    return RenderSize();
}

FfxFloat32 SampleTransparencyAndCompositionMask(FfxFloat32x2 fUV)
{
    return 0.0;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_INPUT_COLOR)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_INPUT_COLOR) uniform texture2D  r_input_color_jittered;

FfxFloat32x3 LoadInputColor(FfxInt32x2 iPxPos)
{
	return texelFetch(r_input_color_jittered, iPxPos, 0).rgb;
}

FfxFloat32x3 SampleInputColor(FfxFloat32x2 fUV)
{
	return textureLod(sampler2D(r_input_color_jittered, s_LinearClamp), fUV, 0.0).rgb;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_INPUT_MOTION_VECTORS)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_INPUT_MOTION_VECTORS) uniform texture2D  r_input_motion_vectors;

FfxFloat32x2 LoadInputMotionVector(FfxInt32x2 iPxDilatedMotionVectorPos)
{
	FfxFloat32x2 fSrcMotionVector = texelFetch(r_input_motion_vectors, iPxDilatedMotionVectorPos, 0).xy - Jitter();

	FfxFloat32x2 fUvMotionVector = fSrcMotionVector * MotionVectorScale();

#if FFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS
	fUvMotionVector -= MotionVectorJitterCancellation();
#endif

	return fUvMotionVector;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_INTERNAL_UPSCALED)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_INTERNAL_UPSCALED) uniform texture2D  r_internal_upscaled_color;

FfxFloat32x4 LoadHistory(FfxInt32x2 iPxHistory)
{
	return texelFetch(r_internal_upscaled_color, iPxHistory, 0);
}

FfxFloat32x4 SampleHistory(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_internal_upscaled_color, s_LinearClamp), fUV, 0.0);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_LUMA_HISTORY)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_LUMA_HISTORY, rgba16f) uniform image2D  rw_luma_history;

void StoreLumaHistory(FfxInt32x2 iPxPos, FfxFloat32x4 fLumaHistory)
{
	imageStore(rw_luma_history, iPxPos, fLumaHistory);
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_LUMA_HISTORY)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_LUMA_HISTORY) uniform texture2D  r_luma_history;

FfxFloat32x4 LoadLumaHistory(FfxInt32x2 iPxPos)
{
    return texelFetch(r_luma_history, iPxPos, 0);
}

FfxFloat32x4 SampleLumaHistory(FfxFloat32x2 fUV)
{
	return textureLod(sampler2D(r_luma_history, s_LinearClamp), fUV, 0.0);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_INTERNAL_UPSCALED)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_INTERNAL_UPSCALED, rgba16f) writeonly uniform image2D  rw_internal_upscaled_color;

void StoreReprojectedHistory(FfxInt32x2 iPxHistory, FfxFloat32x4 fHistory)
{
	imageStore(rw_internal_upscaled_color, iPxHistory, fHistory);
}

void StoreInternalColorAndWeight(FfxInt32x2 iPxPos, FfxFloat32x4 fColorAndWeight)
{
	imageStore(rw_internal_upscaled_color, iPxPos, fColorAndWeight);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_UPSCALED_OUTPUT)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_UPSCALED_OUTPUT, rgba16f) writeonly uniform image2D  rw_upscaled_output;

void StoreUpscaledOutput(FfxInt32x2 iPxPos, FfxFloat32x3 fColor)
{
    imageStore(rw_upscaled_output, iPxPos, FfxFloat32x4(fColor, 1.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_ACCUMULATION)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_ACCUMULATION) uniform texture2D  r_accumulation;

FfxFloat32 SampleAccumulation(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_accumulation, s_LinearClamp), fUV, 0.0).x;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_ACCUMULATION)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_ACCUMULATION, r8) uniform image2D  rw_accumulation;

void StoreAccumulation(FfxInt32x2 iPxPos, FfxFloat32 fAccumulation)
{
    imageStore(rw_accumulation, iPxPos, vec4(fAccumulation, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_SHADING_CHANGE)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_SHADING_CHANGE) uniform texture2D  r_shading_change;

FfxFloat32 LoadShadingChange(FfxInt32x2 iPxPos)
{
    return texelFetch(r_shading_change, iPxPos, 0).x * cbFSR3Upscaler.mShadingChangeScale;
}

FfxFloat32 SampleShadingChange(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_shading_change, s_LinearClamp), fUV, 0.0).x * cbFSR3Upscaler.mShadingChangeScale;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_SHADING_CHANGE)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SHADING_CHANGE, r8) uniform image2D  rw_shading_change;

void StoreShadingChange(FfxInt32x2 iPxPos, FfxFloat32 fShadingChange)
{
    imageStore(rw_shading_change, iPxPos, vec4(fShadingChange, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_FARTHEST_DEPTH)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_FARTHEST_DEPTH) uniform texture2D  r_farthest_depth;

FfxInt32x2 GetFarthestDepthResourceDimensions()
{
	return textureSize(r_farthest_depth, 0).xy;
}

FfxFloat32 LoadFarthestDepth(FfxInt32x2 iPxPos)
{
    return texelFetch(r_farthest_depth, iPxPos, 0).x;
}

FfxFloat32 SampleFarthestDepth(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_farthest_depth, s_LinearClamp), fUV, 0.0).x;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_FARTHEST_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_FARTHEST_DEPTH, r16f) uniform image2D  rw_farthest_depth;

void StoreFarthestDepth(FfxInt32x2 iPxPos, FfxFloat32 fDepth)
{
    imageStore(rw_farthest_depth, iPxPos, vec4(fDepth, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_FARTHEST_DEPTH_MIP1)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_FARTHEST_DEPTH_MIP1) uniform texture2D  r_farthest_depth_mip1;

FfxInt32x2 GetFarthestDepthMip1ResourceDimensions()
{
	return textureSize(r_farthest_depth_mip1, 0).xy;
}

FfxFloat32 LoadFarthestDepthMip1(FfxInt32x2 iPxPos)
{
    return texelFetch(r_farthest_depth_mip1, iPxPos, 0).x;
}

FfxFloat32 SampleFarthestDepthMip1(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_farthest_depth_mip1, s_LinearClamp), fUV, 0.0).x;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_FARTHEST_DEPTH_MIP1)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_FARTHEST_DEPTH_MIP1, r16f) uniform image2D  rw_farthest_depth_mip1;

void StoreFarthestDepthMip1(FfxInt32x2 iPxPos, FfxFloat32 fDepth)
{
    imageStore(rw_farthest_depth_mip1, iPxPos, vec4(fDepth, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_CURRENT_LUMA)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_CURRENT_LUMA) uniform texture2D  r_current_luma;

FfxFloat32 LoadCurrentLuma(FfxInt32x2 iPxPos)
{
    return texelFetch(r_current_luma, iPxPos, 0).r;
}

FfxFloat32 SampleCurrentLuma(FfxFloat32x2 uv)
{
    return textureLod(sampler2D(r_current_luma, s_LinearClamp), uv, 0.0).r;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_CURRENT_LUMA)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_CURRENT_LUMA, r16f) uniform image2D  rw_current_luma;

void StoreCurrentLuma(FfxInt32x2 iPxPos, FfxFloat32 fLuma)
{
    imageStore(rw_current_luma, iPxPos, vec4(fLuma, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_LUMA_INSTABILITY)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_LUMA_INSTABILITY) uniform texture2D  r_luma_instability;

FfxFloat32 SampleLumaInstability(FfxFloat32x2 uv)
{
    return textureLod(sampler2D(r_luma_instability, s_LinearClamp), uv, 0.0).x;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_LUMA_INSTABILITY)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_LUMA_INSTABILITY, r16f) uniform image2D  rw_luma_instability;

void StoreLumaInstability(FfxInt32x2 iPxPos, FfxFloat32 fLumaInstability)
{
    imageStore(rw_luma_instability, iPxPos, vec4(fLumaInstability, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_PREVIOUS_LUMA)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_PREVIOUS_LUMA) uniform texture2D  r_previous_luma;

FfxFloat32 LoadPreviousLuma(FfxInt32x2 iPxPos)
{
    return texelFetch(r_previous_luma, iPxPos, 0).r;
}

FfxFloat32 SamplePreviousLuma(FfxFloat32x2 uv)
{
    return textureLod(sampler2D(r_previous_luma, s_LinearClamp), uv, 0.0).r;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_NEW_LOCKS)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_NEW_LOCKS) uniform texture2D  r_new_locks;

FfxFloat32 LoadNewLocks(FfxInt32x2 iPxPos)
{
	return texelFetch(r_new_locks, iPxPos, 0).r;
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_NEW_LOCKS)
layout(set = 0, binding = FSR3UPSCALER_BIND_UAV_NEW_LOCKS, r8) uniform image2D  rw_new_locks;

FfxFloat32 LoadRwNewLocks(FfxInt32x2 iPxPos)
{
	return imageLoad(rw_new_locks, iPxPos).r;
}

void StoreNewLocks(FfxInt32x2 iPxPos, FfxFloat32 newLock)
{
	imageStore(rw_new_locks, iPxPos, vec4(newLock, 0, 0, 0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_RECONSTRUCTED_PREV_NEAREST_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_RECONSTRUCTED_PREV_NEAREST_DEPTH) uniform utexture2D r_reconstructed_previous_nearest_depth;

FfxFloat32 LoadReconstructedPrevDepth(FfxInt32x2 iPxPos)
{
	return uintBitsToFloat(texelFetch(r_reconstructed_previous_nearest_depth, iPxPos, 0).r);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_RECONSTRUCTED_PREV_NEAREST_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_RECONSTRUCTED_PREV_NEAREST_DEPTH, r32ui) uniform uimage2D  rw_reconstructed_previous_nearest_depth;

void StoreReconstructedDepth(FfxInt32x2 iPxSample, FfxFloat32 fDepth)
{
	FfxUInt32 uDepth = floatBitsToUint(fDepth);

	#if FFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH
		imageAtomicMax(rw_reconstructed_previous_nearest_depth, iPxSample, uDepth);
	#else
		imageAtomicMin(rw_reconstructed_previous_nearest_depth, iPxSample, uDepth); // min for standard, max for inverted depth
	#endif
}

void SetReconstructedDepth(FfxInt32x2 iPxSample, FfxUInt32 uValue)
{
	imageStore(rw_reconstructed_previous_nearest_depth, iPxSample, uvec4(uValue, 0, 0, 0));
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_DILATED_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_DILATED_DEPTH, r32f) writeonly uniform image2D  rw_dilated_depth;

void StoreDilatedDepth(FFX_PARAMETER_IN FfxInt32x2 iPxPos, FFX_PARAMETER_IN FfxFloat32 fDepth)
{
	imageStore(rw_dilated_depth, iPxPos, vec4(fDepth, 0.0, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_DILATED_MOTION_VECTORS)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_DILATED_MOTION_VECTORS, rg16f) writeonly uniform image2D  rw_dilated_motion_vectors;

void StoreDilatedMotionVector(FFX_PARAMETER_IN FfxInt32x2 iPxPos, FFX_PARAMETER_IN FfxFloat32x2 fMotionVector)
{
	imageStore(rw_dilated_motion_vectors, iPxPos, vec4(fMotionVector, 0.0, 0.0));
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_DILATED_MOTION_VECTORS)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_DILATED_MOTION_VECTORS) uniform texture2D  r_dilated_motion_vectors;

FfxFloat32x2 LoadDilatedMotionVector(FfxInt32x2 iPxInput)
{
	return texelFetch(r_dilated_motion_vectors, iPxInput, 0).xy;
}

FfxFloat32x2 SampleDilatedMotionVector(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_dilated_motion_vectors, s_LinearClamp), fUV, 0.0).xy;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_DILATED_DEPTH)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_DILATED_DEPTH) uniform texture2D  r_dilated_depth;

FfxFloat32 LoadDilatedDepth(FfxInt32x2 iPxInput)
{
	return texelFetch(r_dilated_depth, iPxInput, 0).r;
}

FfxFloat32 SampleDilatedDepth(FfxFloat32x2 fUV)
{
    return textureLod(sampler2D(r_dilated_depth, s_LinearClamp), fUV, 0.0).r;
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_INPUT_EXPOSURE)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_INPUT_EXPOSURE) uniform texture2D  r_input_exposure;

FfxFloat32 Exposure()
{
	FfxFloat32 exposure = texelFetch(r_input_exposure, FfxInt32x2(0, 0), 0).x;

	if (exposure == 0.0) {
		exposure = 1.0;
	}

	return exposure;
}
#endif

// BEGIN: FSR3UPSCALER_BIND_SRV_LANCZOS_LUT
#if defined(FSR3UPSCALER_BIND_SRV_LANCZOS_LUT)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_LANCZOS_LUT) uniform texture2D  r_lanczos_lut;
#endif

FfxFloat32 SampleLanczos2Weight(FfxFloat32 x)
{
#if defined(FSR3UPSCALER_BIND_SRV_LANCZOS_LUT)
	return textureLod(sampler2D(r_lanczos_lut, s_LinearClamp), FfxFloat32x2(x / 2.0, 0.5), 0.0).x; 
#else
    return 0.f;
#endif
}
// END: FSR3UPSCALER_BIND_SRV_LANCZOS_LUT

#if defined(FSR3UPSCALER_BIND_SRV_DILATED_REACTIVE_MASKS)
layout (set = 0, binding = FSR3UPSCALER_BIND_SRV_DILATED_REACTIVE_MASKS) uniform texture2D  r_dilated_reactive_masks;

FfxFloat32x4 SampleDilatedReactiveMasks(FfxFloat32x2 fUV)
{
	return textureLod(sampler2D(r_dilated_reactive_masks, s_LinearClamp), fUV, 0.0);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_DILATED_REACTIVE_MASKS)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_DILATED_REACTIVE_MASKS, rgba8) writeonly uniform image2D  rw_dilated_reactive_masks;

void StoreDilatedReactiveMasks(FFX_PARAMETER_IN FfxInt32x2 iPxPos, FFX_PARAMETER_IN FfxFloat32x4 fDilatedReactiveMasks)
{
    imageStore(rw_dilated_reactive_masks, iPxPos, fDilatedReactiveMasks);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_FRAME_INFO)
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_FRAME_INFO, rgba32f) uniform image2D  rw_frame_info;

FfxFloat32x4 LoadFrameInfo()
{
    return imageLoad(rw_frame_info, ivec2(0, 0));
}

void StoreFrameInfo(FfxFloat32x4 fInfo)
{
    imageStore(rw_frame_info, ivec2(0, 0), fInfo);
}
#endif

#if defined(FSR3UPSCALER_BIND_SRV_FRAME_INFO)
layout(set = 0, binding = FSR3UPSCALER_BIND_SRV_FRAME_INFO) uniform texture2D  r_frame_info;

FfxFloat32x4 FrameInfo()
{
    return texelFetch(r_frame_info, ivec2(0, 0), 0);
}
#endif

#if defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_0)    && \
    defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_1)    && \
    defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_2)    && \
    defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_3)    && \
    defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_4)    && \
    defined(FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_5)

layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_0, rg16f)          uniform image2D  rw_spd_mip0;
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_1, rg16f)          uniform image2D  rw_spd_mip1;
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_2, rg16f)          uniform image2D  rw_spd_mip2;
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_3, rg16f)          uniform image2D  rw_spd_mip3;
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_4, rg16f)          uniform image2D  rw_spd_mip4;
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_MIPS_LEVEL_5, rg16f) coherent uniform image2D  rw_spd_mip5;

FfxFloat32x2 RWLoadPyramid(FFX_PARAMETER_IN FfxInt32x2 iPxPos, FFX_PARAMETER_IN FfxUInt32 index)
{
#define LOAD(idx)                                 \
            if (index == idx)                             \
            {                                             \
                return imageLoad(rw_spd_mip##idx, iPxPos).xy; \
            }
    LOAD(0);
    LOAD(1);
    LOAD(2);
    LOAD(3);
    LOAD(4);
    LOAD(5);

    return FfxFloat32x2(0.0, 0.0);

#undef LOAD
}

void StorePyramid(FFX_PARAMETER_IN FfxInt32x2 iPxPos, FFX_PARAMETER_IN FfxFloat32x2 outValue, FFX_PARAMETER_IN FfxUInt32 index)
{
#define STORE(idx)                   \
            if (index == idx)                \
            {                                \
                imageStore(rw_spd_mip##idx, iPxPos, vec4(outValue, 0.0, 0.0)); \
            }

    STORE(0);
    STORE(1);
    STORE(2);
    STORE(3);
    STORE(4);
    STORE(5);

#undef STORE
}
#endif

#if defined FSR3UPSCALER_BIND_UAV_SPD_GLOBAL_ATOMIC
layout (set = 0, binding = FSR3UPSCALER_BIND_UAV_SPD_GLOBAL_ATOMIC, r32ui) coherent uniform uimage2D  rw_spd_global_atomic;

void SPD_IncreaseAtomicCounter(inout FfxUInt32 spdCounter)
{
    spdCounter = imageAtomicAdd(rw_spd_global_atomic, ivec2(0, 0), 1);
}

void SPD_ResetAtomicCounter()
{
    imageStore(rw_spd_global_atomic, ivec2(0, 0), uvec4(0));
}
#endif

#endif // #if defined(FFX_GPU)
