#pragma once

#include <string>

#include <nvsdk_ngx_defs.h>

#include "dlss.hpp"

namespace Rtx
{
    /// NGX's own name for a result code, with the code after it.
    ///
    /// **Asked rather than switched on.** The SDK defines several dozen codes as a bitfield —
    /// success is `0x1` and every failure carries `0xBAD00000` with the reason in its low bits — and
    /// it names them all, including ones this build has not been told about.
    inline std::string describeNgxResult(NVSDK_NGX_Result result)
    {
        // `wchar_t` is 32 bits here, not the 16 it is on Windows, which is what truncates every one
        // of these names to a single character if the declaration is copied from the documentation.
        const wchar_t* wide = GetNGXResultAsString(result);
        std::string text;
        for (const wchar_t* at = wide; at != nullptr && *at != L'\0'; ++at)
            text += static_cast<char>(*at);

        return text;
    }

    /// The quality level an upscale setting is, as NGX numbers them.
    ///
    /// `Off` never reaches here: it is the absence of an upscaler rather than a mode of one, and the
    /// renderer answers it by not building a feature at all.
    inline NVSDK_NGX_PerfQuality_Value ngxQualityOf(Upscale upscale)
    {
        switch (upscale)
        {
            case Upscale::Balanced:
                return NVSDK_NGX_PerfQuality_Value_Balanced;
            case Upscale::Quality:
                return NVSDK_NGX_PerfQuality_Value_MaxQuality;
            case Upscale::Dlaa:
                return NVSDK_NGX_PerfQuality_Value_DLAA;
            case Upscale::Off:
            case Upscale::Performance:
                return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        }

        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    }
}
