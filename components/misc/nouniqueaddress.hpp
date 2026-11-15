#pragma once

// `[[no_unique_address]]` as the compiler takes it: MSVC's ABI accepts the standard spelling and ignores it, and gives
// an empty member no storage only under its own name, which Clang reads in that ABI too (clang-cl).
#if defined(_MSC_VER)
#define OMW_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define OMW_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
