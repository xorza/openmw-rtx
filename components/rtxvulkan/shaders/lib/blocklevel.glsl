#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOCKLEVEL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOCKLEVEL_GLSL

#include "shared/blockencode.h"

/// The workgroup's place in a dispatch laid in rows of `gl_NumWorkGroups.x` groups
/// (`Rtx::Groups::linear`): a count of groups past what one axis is sure to take.
uint linearWorkGroup()
{
    return gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
}

/// Which block of a chain a lane encodes, every level's blocks one after another and row by row
/// (`Rtx::BlockChain`), each level's lanes whole workgroups of their own.
struct ChainBlock
{
    uint mLevel;

    /// The block's place among every block of the chain, and among its level's.
    uint mIndex;
    uint mWithin;

    /// The level's texels across and down, and its blocks across.
    uvec2 mExtent;
    uint mAcross;
};

/// The block this lane encodes in a chain of `levels` from `extent` down, or false for a lane past
/// its level's last block or past the chain: a dispatch launches whole groups.
bool chainBlock(uvec2 extent, uint levels, out ChainBlock block)
{
    uint level = 0u;
    uint group = linearWorkGroup();
    uint first = 0u;
    uint across = (extent.x + BLOCK_SIDE - 1u) / BLOCK_SIDE;
    uint levelBlocks = across * ((extent.y + BLOCK_SIDE - 1u) / BLOCK_SIDE);
    [[dont_unroll]] for (; level + 1u < levels; ++level)
    {
        const uint groups = (levelBlocks + BLOCK_ENCODE_WORKGROUP - 1u) / BLOCK_ENCODE_WORKGROUP;
        if (group < groups)
            break;
        group -= groups;
        first += levelBlocks;
        extent = max(extent >> 1u, uvec2(1u));
        across = (extent.x + BLOCK_SIDE - 1u) / BLOCK_SIDE;
        levelBlocks = across * ((extent.y + BLOCK_SIDE - 1u) / BLOCK_SIDE);
    }

    const uint within = group * BLOCK_ENCODE_WORKGROUP + gl_LocalInvocationID.x;
    block = ChainBlock(level, first + within, within, extent, across);
    return within < levelBlocks;
}

#endif
