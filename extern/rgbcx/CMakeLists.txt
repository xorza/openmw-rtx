# rgbcx, Rich Geldreich's BC1 to BC5 encoders and decoders, bc7enc, his BC7 encoder, and bc7decomp, his BC7 decoder,
# which the tests alone read: bc7enc_rdo at b9438627eef73a1157e84201b6fa6eb2ffd6d9f0, rgbcx.h, rgbcx.cpp,
# rgbcx_table4.h, bc7enc.h, bc7enc.cpp, bc7decomp.h and bc7decomp.cpp as the repository holds them, but for each
# source's include of its header, spelled from the include directory. MIT or public domain, at the end of each; the
# MIT texts ship as files/licenses/rgbcx.txt and, for bc7enc's later copyright year, files/licenses/bc7enc.txt.
# Third-party code, built with its own warnings off, as the other fetched libraries are.
add_library(rgbcx STATIC rgbcx.cpp include/rgbcx/rgbcx.h bc7enc.cpp include/rgbcx/bc7enc.h bc7decomp.cpp
    include/rgbcx/bc7decomp.h)
target_include_directories(rgbcx SYSTEM PUBLIC include)
target_include_directories(rgbcx PRIVATE .)
if (MSVC)
    target_compile_options(rgbcx PRIVATE /w)
else()
    target_compile_options(rgbcx PRIVATE -w)
endif()
