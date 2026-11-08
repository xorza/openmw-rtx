# rgbcx, Rich Geldreich's BC1 to BC5 encoders and decoders: bc7enc_rdo at
# b9438627eef73a1157e84201b6fa6eb2ffd6d9f0, rgbcx.h, rgbcx.cpp and rgbcx_table4.h as the repository holds them, but
# for rgbcx.cpp's include of its header, spelled from the include directory. MIT or public domain, at the end of
# rgbcx.h; the MIT text ships as files/licenses/rgbcx.txt. Third-party code, built with its own warnings off, as
# the other fetched libraries are.
add_library(rgbcx STATIC rgbcx.cpp include/rgbcx/rgbcx.h)
target_include_directories(rgbcx SYSTEM PUBLIC include)
target_include_directories(rgbcx PRIVATE .)
if (MSVC)
    target_compile_options(rgbcx PRIVATE /w)
else()
    target_compile_options(rgbcx PRIVATE -w)
endif()
