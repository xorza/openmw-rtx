# rgbcx, Rich Geldreich's BC1 to BC5 encoders and decoders, bc7enc, his BC7 encoder, and bc7decomp, his BC7 decoder,
# which the tests and the benchmark alone read: bc7enc_rdo at b9438627eef73a1157e84201b6fa6eb2ffd6d9f0, rgbcx.h,
# rgbcx.cpp, rgbcx_table4.h, bc7enc.h, bc7enc.cpp, bc7decomp.h and bc7decomp.cpp as the repository holds them, but for
# each source's include of its header, spelled from the include directory, and for two encoders made to make the same
# blocks sooner (`RtxChainBuildTest`, docs/research/2026-11-13-preparation-cpu-by-codec.md) with the vector
# instructions the tree's x86-64-v2 has:
# - rgbcx.cpp's BC1 search: its two selector searches walk a block four texels at a time, and a search over total
#   orderings orders the texels by counting, fits and rounds every ordering's endpoints a channel a lane before any
#   is searched, and searches each pair of them once;
# - bc7enc.cpp: a cell's selectors are found sixteen pixels at once, in RGB(A) error and in YCbCr(A); its sums, its
#   projection and its RGB least squares a lane a sum; mode 5's alphas sixteen at once; a partition's estimate both
#   its subsets and all sixteen pixels at once, with no pixel gathered; and a block's fields are written into two
#   64-bit halves. It holds the weights of an error to 128 each, which its own defaults are within and bc7enc.h says.
# MIT or public domain, at the end of each; the MIT texts ship as files/licenses/rgbcx.txt and, for bc7enc's later
# copyright year, files/licenses/bc7enc.txt.
add_library(rgbcx STATIC)
target_sources(
    rgbcx
    PRIVATE
        rgbcx.cpp
        bc7enc.cpp
        bc7decomp.cpp
        include/rgbcx/rgbcx.h
        include/rgbcx/bc7enc.h
        include/rgbcx/bc7decomp.h
)
target_include_directories(rgbcx SYSTEM PUBLIC include)
target_include_directories(rgbcx PRIVATE .)
